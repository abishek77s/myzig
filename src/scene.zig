//! Display lists, what a frame paints, and their diff into damage.
//!
//! A frame paints into a Scene. The renderer compares it with the scene it
//! drew last: an op whose content, place and clip are unchanged hashes the
//! same and costs nothing, and only the bounds of ops that came, went or
//! changed are redrawn and presented. This is the retained part of an
//! immediate-mode toolkit: the view is rebuilt, the pixels are not.

const std = @import("std");
const geom = @import("geom.zig");
const text = @import("text.zig");
const raster = @import("raster.zig");
const Rect = geom.Rect;
const IRect = geom.IRect;
const Color = geom.Color;

pub const Op = struct {
    kind: enum(u8) { rect, text },
    weight: text.Weight = .regular,
    color: Color,
    border_color: Color = .transparent,
    rect: Rect, // logical; for text: x = pen origin, y = baseline, w = width
    radius: f32 = 0,
    border: f32 = 0,
    size: f32 = 0,
    str_off: u32 = 0,
    str_len: u32 = 0,
    clip: IRect,
    bounds: IRect, // device pixels, clipped
    hash: u64 = 0,
};

pub const Scene = struct {
    gpa: std.mem.Allocator,
    ops: std.ArrayListUnmanaged(Op) = .empty,
    strings: std.ArrayListUnmanaged(u8) = .empty,
    scale: f32 = 1,
    clip_stack: std.ArrayListUnmanaged(IRect) = .empty,
    full: IRect = .{},
    background: Color = Color.white,

    pub fn deinit(s: *Scene) void {
        s.ops.deinit(s.gpa);
        s.strings.deinit(s.gpa);
        s.clip_stack.deinit(s.gpa);
    }

    pub fn reset(s: *Scene, width: i32, height: i32, scale: f32, bg: Color) void {
        s.ops.clearRetainingCapacity();
        s.strings.clearRetainingCapacity();
        s.clip_stack.clearRetainingCapacity();
        s.scale = scale;
        s.full = .{ .x0 = 0, .y0 = 0, .x1 = width, .y1 = height };
        s.background = bg;
    }

    pub fn clip(s: *const Scene) IRect {
        return if (s.clip_stack.items.len > 0) s.clip_stack.items[s.clip_stack.items.len - 1] else s.full;
    }

    pub fn pushClip(s: *Scene, r: Rect) void {
        const c = IRect.fromRect(r, s.scale).intersect(s.clip());
        s.clip_stack.append(s.gpa, c) catch {};
    }

    pub fn popClip(s: *Scene) void {
        _ = s.clip_stack.pop();
    }

    pub fn rect(s: *Scene, r: Rect, color: Color, radius: f32, border: f32, border_color: Color) void {
        if (color.a == 0 and (border == 0 or border_color.a == 0)) return;
        const c = s.clip();
        const b = IRect.fromRect(r, s.scale).intersect(c);
        if (b.isEmpty()) return;
        var op = Op{ .kind = .rect, .color = color, .rect = r, .radius = radius, .border = border, .border_color = border_color, .clip = c, .bounds = b };
        op.hash = hashOp(&op, "");
        s.ops.append(s.gpa, op) catch {};
    }

    /// text paints str with its pen at x and its baseline at y.
    pub fn textRun(s: *Scene, fonts: *text.Fonts, x: f32, y: f32, str: []const u8, size: f32, weight: text.Weight, color: Color) void {
        if (str.len == 0 or color.a == 0) return;
        const m = fonts.metrics(weight, size);
        const w = fonts.measure(weight, size, str);
        const c = s.clip();
        const box = Rect{ .x = x - 2, .y = y - m.ascent - 2, .w = w + 4, .h = m.ascent + m.descent + 4 };
        const b = IRect.fromRect(box, s.scale).intersect(c);
        if (b.isEmpty()) return;
        const off: u32 = @intCast(s.strings.items.len);
        s.strings.appendSlice(s.gpa, str) catch return;
        var op = Op{ .kind = .text, .color = color, .rect = .{ .x = x, .y = y, .w = w }, .size = size, .weight = weight, .str_off = off, .str_len = @intCast(str.len), .clip = c, .bounds = b };
        op.hash = hashOp(&op, str);
        s.ops.append(s.gpa, op) catch {};
    }

    pub fn string(s: *const Scene, op: *const Op) []const u8 {
        return s.strings.items[op.str_off..][0..op.str_len];
    }
};

fn hashOp(op: *const Op, str: []const u8) u64 {
    var h = std.hash.Wyhash.init(@intFromEnum(op.kind));
    h.update(std.mem.asBytes(&op.color));
    h.update(std.mem.asBytes(&op.border_color));
    h.update(std.mem.asBytes(&op.rect));
    h.update(std.mem.asBytes(&op.radius));
    h.update(std.mem.asBytes(&op.border));
    h.update(std.mem.asBytes(&op.size));
    h.update(std.mem.asBytes(&op.weight));
    h.update(std.mem.asBytes(&op.clip));
    h.update(str);
    return h.final();
}

/// Renderer keeps the last scene drawn into a canvas and redraws only
/// what changed.
pub const Renderer = struct {
    gpa: std.mem.Allocator,
    last: std.ArrayListUnmanaged(Op) = .empty,
    last_hashes: std.AutoHashMapUnmanaged(u64, void) = .empty,
    cur_hashes: std.AutoHashMapUnmanaged(u64, void) = .empty,
    damage: std.ArrayListUnmanaged(IRect) = .empty,
    valid: bool = false,
    last_bg: Color = Color.white,
    threads: usize = 1,
    pool: ?*std.Thread.Pool = null,
    stats: Stats = .{},

    pub const Stats = struct { frames: u64 = 0, pixels: u64 = 0, ops_drawn: u64 = 0 };

    pub fn deinit(r: *Renderer) void {
        r.last.deinit(r.gpa);
        r.last_hashes.deinit(r.gpa);
        r.cur_hashes.deinit(r.gpa);
        r.damage.deinit(r.gpa);
    }

    pub fn invalidate(r: *Renderer) void {
        r.valid = false;
    }

    /// render draws scene into canvas and returns the rectangles it changed.
    pub fn render(r: *Renderer, s: *const Scene, fonts: *text.Fonts, cv: *const raster.Canvas) []const IRect {
        r.damage.clearRetainingCapacity();
        r.stats.frames += 1;
        if (!r.valid or !r.last_bg.eql(s.background)) {
            r.addDamage(cv.bounds());
        } else {
            r.cur_hashes.clearRetainingCapacity();
            r.cur_hashes.ensureTotalCapacity(r.gpa, @intCast(s.ops.items.len)) catch {};
            for (s.ops.items) |*op| r.cur_hashes.putAssumeCapacity(op.hash, {});
            for (s.ops.items) |*op| if (!r.last_hashes.contains(op.hash)) r.addDamage(op.bounds);
            for (r.last.items) |*op| if (!r.cur_hashes.contains(op.hash)) r.addDamage(op.bounds);
        }
        r.mergeDamage(cv.bounds());
        for (r.damage.items) |d| r.drawArea(s, fonts, cv, d);
        // Keep this scene for the next diff.
        r.last.clearRetainingCapacity();
        r.last.appendSlice(r.gpa, s.ops.items) catch {
            r.valid = false;
            return r.damage.items;
        };
        r.last_hashes.clearRetainingCapacity();
        r.last_hashes.ensureTotalCapacity(r.gpa, @intCast(s.ops.items.len)) catch {};
        for (s.ops.items) |*op| r.last_hashes.putAssumeCapacity(op.hash, {});
        r.valid = true;
        r.last_bg = s.background;
        return r.damage.items;
    }

    fn addDamage(r: *Renderer, d: IRect) void {
        if (d.isEmpty()) return;
        r.damage.append(r.gpa, d) catch {};
    }

    /// mergeDamage joins rectangles that overlap or whose union wastes
    /// little, so an area is never drawn twice and presents stay few.
    fn mergeDamage(r: *Renderer, full: IRect) void {
        var items = r.damage.items;
        var n = items.len;
        var changed = true;
        while (changed) {
            changed = false;
            var i: usize = 0;
            while (i < n) : (i += 1) {
                var j = i + 1;
                while (j < n) {
                    const u = items[i].unite(items[j]);
                    if (items[i].overlaps(items[j]) or u.area() <= @divTrunc((items[i].area() + items[j].area()) * 3, 2)) {
                        items[i] = u;
                        items[j] = items[n - 1];
                        n -= 1;
                        changed = true;
                    } else j += 1;
                }
            }
        }
        if (n > 24) {
            var u = IRect{};
            for (items[0..n]) |d| u = u.unite(d);
            items[0] = u;
            n = 1;
        }
        for (items[0..n]) |*d| d.* = d.intersect(full);
        r.damage.shrinkRetainingCapacity(n);
    }

    fn drawArea(r: *Renderer, s: *const Scene, fonts: *text.Fonts, cv: *const raster.Canvas, area: IRect) void {
        if (area.isEmpty()) return;
        r.stats.pixels += @intCast(area.area());
        cv.fill(area, s.background);
        for (s.ops.items) |*op| {
            if (!op.bounds.overlaps(area)) continue;
            r.stats.ops_drawn += 1;
            drawOp(s, fonts, cv, op, op.clip.intersect(area));
        }
    }
};

pub fn drawOp(s: *const Scene, fonts: *text.Fonts, cv: *const raster.Canvas, op: *const Op, clip: IRect) void {
    const k = s.scale;
    switch (op.kind) {
        .rect => {
            const x = op.rect.x * k;
            const y = op.rect.y * k;
            const w = op.rect.w * k;
            const h = op.rect.h * k;
            const rad = op.radius * k;
            if (op.border > 0 and op.border_color.a > 0) {
                const bw = op.border * k;
                if (op.color.a == 255) {
                    cv.rrect(x, y, w, h, rad, op.border_color, clip);
                    cv.rrect(x + bw, y + bw, w - 2 * bw, h - 2 * bw, @max(0, rad - bw), op.color, clip);
                } else {
                    cv.rrect(x + bw, y + bw, w - 2 * bw, h - 2 * bw, @max(0, rad - bw), op.color, clip);
                    cv.ring(x, y, w, h, rad, bw, op.border_color, clip);
                }
            } else {
                cv.rrect(x, y, w, h, rad, op.color, clip);
            }
        },
        .text => {
            const px = op.size * k;
            const face = fonts.face(op.weight);
            const sc = face.scale(px);
            var pen = op.rect.x * k;
            const base: i32 = @intFromFloat(@round(op.rect.y * k));
            var prev: u32 = 0;
            var it = text.Utf8Iter{ .s = s.string(op) };
            while (it.next()) |cp| {
                const g = face.glyph(cp);
                if (prev != 0) pen += fonts.kern(op.weight, prev, g) * sc;
                prev = g;
                const fl = @floor(pen);
                const phase: u2 = @intFromFloat(@min(3, @floor((pen - fl) * 4)));
                if (fonts.glyphAt(op.weight, px, g, phase)) |gl| {
                    if (gl.w > 0) {
                        const gx = @as(i32, @intFromFloat(fl)) + gl.x0;
                        const gy = base + gl.y0;
                        cv.mask(fonts.coverage.items[gl.off..][0 .. @as(usize, gl.w) * gl.h], gl.w, gl.h, gx, gy, op.color, clip);
                    }
                }
                pen += face.advance(cp, g) * sc;
            }
        },
    }
}
