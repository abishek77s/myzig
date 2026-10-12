//! What the GPU renderers draw: a scene as one array of instances, every
//! box and every glyph a quad of the same shader, in painting order, so a
//! frame is a single instanced draw. Boxes are rounded rectangles with
//! borders, shaded from their signed distance; glyphs sample a coverage
//! atlas the renderer keeps on the GPU.

const std = @import("std");
const scene = @import("../scene.zig");
const text = @import("../text.zig");

pub const Instance = extern struct {
    /// rect is the quad in device pixels: x, y, width, height.
    rect: [4]f32,
    /// clip is x0, y0, x1, y1 in device pixels.
    clip: [4]f32,
    color: [4]f32,
    border_color: [4]f32,
    /// params are the radius and the border width in device pixels, and
    /// the kind: 0 for a box, 1 for a glyph.
    params: [4]f32,
    /// uv is the glyph's place in the atlas, normalized: uv_x, uv_y, u1, v1.
    uv: [4]f32,
};

/// Upload is a glyph to copy into the atlas texture: its coverage in
/// Fonts.coverage at off, to x, y.
pub const Upload = struct { x: u32, y: u32, w: u32, h: u32, off: u32 };

/// Atlas places glyphs in a square texture of one byte a texel, on shelves.
pub const Atlas = struct {
    gpa: std.mem.Allocator,
    size: u32 = 1024,
    x: u32 = 1,
    y: u32 = 1,
    shelf: u32 = 0,
    places: std.AutoHashMapUnmanaged(u32, [2]u16) = .empty,
    uploads: std.ArrayListUnmanaged(Upload) = .empty,
    /// grown is set when the texture must be made again at size: every
    /// glyph is uploaded anew.
    grown: bool = true,

    pub fn deinit(a: *Atlas) void {
        a.places.deinit(a.gpa);
        a.uploads.deinit(a.gpa);
    }

    fn reset(a: *Atlas, size: u32) void {
        a.size = size;
        a.x = 1;
        a.y = 1;
        a.shelf = 0;
        a.places.clearRetainingCapacity();
        a.uploads.clearRetainingCapacity();
        a.grown = true;
    }

    /// place returns where glyph gl is, placing it on first use; null when
    /// the atlas is full.
    fn place(a: *Atlas, gl: text.Glyph) ?[2]u16 {
        if (a.places.get(gl.off)) |p| return p;
        const w: u32 = gl.w;
        const h: u32 = gl.h;
        if (a.x + w + 1 > a.size) {
            a.x = 1;
            a.y += a.shelf + 1;
            a.shelf = 0;
        }
        if (a.y + h + 1 > a.size or w + 2 > a.size) return null;
        const p = [2]u16{ @intCast(a.x), @intCast(a.y) };
        a.uploads.append(a.gpa, .{ .x = a.x, .y = a.y, .w = w, .h = h, .off = gl.off }) catch return null;
        a.places.put(a.gpa, gl.off, p) catch return null;
        a.x += w + 1;
        a.shelf = @max(a.shelf, h);
        return p;
    }
};

fn rgba(c: anytype) [4]f32 {
    return .{ @as(f32, @floatFromInt(c.r)) / 255, @as(f32, @floatFromInt(c.g)) / 255, @as(f32, @floatFromInt(c.b)) / 255, @as(f32, @floatFromInt(c.a)) / 255 };
}

fn clipOf(c: anytype) [4]f32 {
    return .{ @floatFromInt(c.x0), @floatFromInt(c.y0), @floatFromInt(c.x1), @floatFromInt(c.y1) };
}

/// build writes the instances of s into out, placing new glyphs in atlas
/// (whose uploads the renderer then makes). When the atlas fills it grows,
/// up to 8192 texels a side, and the scene is built again.
pub fn build(s: *const scene.Scene, fonts: *text.Fonts, atlas: *Atlas, out: *std.ArrayListUnmanaged(Instance)) !void {
    while (true) {
        out.clearRetainingCapacity();
        if (try buildOnce(s, fonts, atlas, out)) return;
        if (atlas.size >= 8192) {
            atlas.reset(atlas.size); // a frame of more glyphs than fit: start over
            _ = try buildOnce(s, fonts, atlas, out);
            return;
        }
        atlas.reset(atlas.size * 2);
    }
}

fn buildOnce(s: *const scene.Scene, fonts: *text.Fonts, atlas: *Atlas, out: *std.ArrayListUnmanaged(Instance)) !bool {
    const k = s.scale;
    try out.ensureTotalCapacity(atlas.gpa, s.ops.items.len);
    for (s.ops.items) |*op| switch (op.kind) {
        .rect => try out.append(atlas.gpa, .{
            .rect = .{ op.rect.x * k, op.rect.y * k, op.rect.w * k, op.rect.h * k },
            .clip = clipOf(op.clip),
            .color = rgba(op.color),
            .border_color = rgba(op.border_color),
            .params = .{ @min(op.radius, @min(op.rect.w, op.rect.h) / 2) * k, op.border * k, 0, 0 },
            .uv = .{ 0, 0, 0, 0 },
        }),
        .text => {
            const Ctx = struct {
                out: *std.ArrayListUnmanaged(Instance),
                atlas: *Atlas,
                op: *const scene.Op,
                full: *bool,
                fn glyph(c: @This(), gx: i32, gy: i32, gl: text.Glyph) void {
                    const p = c.atlas.place(gl) orelse {
                        c.full.* = true;
                        return;
                    };
                    const sz: f32 = @floatFromInt(c.atlas.size);
                    const uv_x = @as(f32, @floatFromInt(p[0])) / sz;
                    const uv_y = @as(f32, @floatFromInt(p[1])) / sz;
                    c.out.append(c.atlas.gpa, .{
                        .rect = .{ @floatFromInt(gx), @floatFromInt(gy), @floatFromInt(gl.w), @floatFromInt(gl.h) },
                        .clip = clipOf(c.op.clip),
                        .color = rgba(c.op.color),
                        .border_color = .{ 0, 0, 0, 0 },
                        .params = .{ 0, 0, 1, 0 },
                        .uv = .{ uv_x, uv_y, uv_x + @as(f32, @floatFromInt(gl.w)) / sz, uv_y + @as(f32, @floatFromInt(gl.h)) / sz },
                    }) catch {
                        c.full.* = true;
                    };
                }
            };
            var full = false;
            scene.glyphs(s, fonts, op, Ctx{ .out = out, .atlas = atlas, .op = op, .full = &full }, Ctx.glyph);
            if (full) return false;
        },
    };
    return true;
}

/// The shaders, in GLSL; the Vulkan renderer's SPIR-V is compiled from the
/// same source (gpu/shaders).
pub const glsl_vertex =
    \\#version 330 core
    \\layout(location = 0) in vec4 a_rect;
    \\layout(location = 1) in vec4 a_clip;
    \\layout(location = 2) in vec4 a_color;
    \\layout(location = 3) in vec4 a_border_color;
    \\layout(location = 4) in vec4 a_params;
    \\layout(location = 5) in vec4 a_uv;
    \\uniform vec2 u_viewport;
    \\out vec2 v_pos;
    \\out vec2 v_uv;
    \\flat out vec4 v_rect;
    \\flat out vec4 v_clip;
    \\flat out vec4 v_color;
    \\flat out vec4 v_border_color;
    \\flat out vec4 v_params;
    \\void main() {
    \\    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    \\    vec2 pos = a_rect.xy + corner * a_rect.zw;
    \\    v_pos = pos;
    \\    v_uv = mix(a_uv.xy, a_uv.zw, corner);
    \\    v_rect = a_rect;
    \\    v_clip = a_clip;
    \\    v_color = a_color;
    \\    v_border_color = a_border_color;
    \\    v_params = a_params;
    \\    vec2 ndc = pos / u_viewport * 2.0 - 1.0;
    \\    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    \\}
;

pub const glsl_fragment =
    \\#version 330 core
    \\in vec2 v_pos;
    \\in vec2 v_uv;
    \\flat in vec4 v_rect;
    \\flat in vec4 v_clip;
    \\flat in vec4 v_color;
    \\flat in vec4 v_border_color;
    \\flat in vec4 v_params;
    \\uniform sampler2D u_atlas;
    \\out vec4 o_color;
    \\float box(vec2 p, vec2 c, vec2 h, float r) {
    \\    vec2 q = abs(p - c) - (h - r);
    \\    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    \\}
    \\void main() {
    \\    if (v_pos.x < v_clip.x || v_pos.y < v_clip.y || v_pos.x >= v_clip.z || v_pos.y >= v_clip.w) discard;
    \\    vec4 fill = vec4(v_color.rgb * v_color.a, v_color.a);
    \\    if (v_params.z > 0.5) {
    \\        o_color = fill * texture(u_atlas, v_uv).r;
    \\        return;
    \\    }
    \\    vec2 h = v_rect.zw * 0.5;
    \\    vec2 ctr = v_rect.xy + h;
    \\    float r = v_params.x;
    \\    float bw = v_params.y;
    \\    float outer = clamp(0.5 - box(v_pos, ctr, h, r), 0.0, 1.0);
    \\    if (bw > 0.0) {
    \\        vec4 border = vec4(v_border_color.rgb * v_border_color.a, v_border_color.a);
    \\        float inner = clamp(0.5 - box(v_pos, ctr, h - bw, max(r - bw, 0.0)), 0.0, 1.0);
    \\        o_color = mix(border, fill, inner) * outer;
    \\    } else {
    \\        o_color = fill * outer;
    \\    }
    \\}
;

test "atlas places glyphs apart, and instances sample their own glyph" {
    const gpa = std.testing.allocator;
    var fonts = try text.Fonts.init(gpa);
    defer fonts.deinit();
    var s = scene.Scene{ .gpa = gpa };
    defer s.deinit();
    s.reset(800, 200, 2, @import("../geom.zig").Color.white);
    s.textRun(&fonts, 10, 30, "Streaming quotes · tick 1 qip", 18, .bold, @import("../geom.zig").Color.black);
    s.textRun(&fonts, 10, 60, "Symbol Price Change % Volume quick", 14, .regular, @import("../geom.zig").Color.black);
    var atlas = Atlas{ .gpa = gpa, .size = 256 };
    defer atlas.deinit();
    var out: std.ArrayListUnmanaged(Instance) = .empty;
    defer out.deinit(gpa);
    try build(&s, &fonts, &atlas, &out);
    // The texture the uploads make.
    const tex = try gpa.alloc(u8, atlas.size * atlas.size);
    defer gpa.free(tex);
    @memset(tex, 0);
    for (atlas.uploads.items) |u| for (0..u.h) |y| for (0..u.w) |x| {
        const i = (u.y + y) * atlas.size + u.x + x;
        try std.testing.expectEqual(@as(u8, 0), tex[i]); // nothing overlaps
        tex[i] = fonts.coverage.items[u.off + y * u.w + x] | 1;
    };
}

test "each glyph instance samples its own coverage" {
    const gpa = std.testing.allocator;
    var fonts = try text.Fonts.init(gpa);
    defer fonts.deinit();
    var s = scene.Scene{ .gpa = gpa };
    defer s.deinit();
    s.reset(800, 200, 1, @import("../geom.zig").Color.white);
    s.textRun(&fonts, 16, 30, "Streaming quotes · tick 1", 18, .bold, @import("../geom.zig").Color.black);
    var atlas = Atlas{ .gpa = gpa };
    defer atlas.deinit();
    var out: std.ArrayListUnmanaged(Instance) = .empty;
    defer out.deinit(gpa);
    try build(&s, &fonts, &atlas, &out);
    const tex = try gpa.alloc(u8, atlas.size * atlas.size);
    defer gpa.free(tex);
    @memset(tex, 0);
    for (atlas.uploads.items) |u| for (0..u.h) |y| for (0..u.w) |x| {
        tex[(u.y + y) * atlas.size + u.x + x] = fonts.coverage.items[u.off + y * u.w + x];
    };
    // Every glyph's texels, read back as the shader's sampler reads them,
    // are the coverage its glyph has.
    const Ctx = struct {
        out: []const Instance,
        tex: []const u8,
        size: u32,
        fonts: *text.Fonts,
        i: *usize,
        fn glyph(c: @This(), gx: i32, gy: i32, gl: text.Glyph) void {
            const in = c.out[c.i.*];
            c.i.* += 1;
            std.testing.expectEqual(@as(f32, @floatFromInt(gx)), in.rect[0]) catch unreachable;
            std.testing.expectEqual(@as(f32, @floatFromInt(gy)), in.rect[1]) catch unreachable;
            const sz: f32 = @floatFromInt(c.size);
            for (0..gl.h) |y| for (0..gl.w) |x| {
                const u = in.uv[0] + (@as(f32, @floatFromInt(x)) + 0.5) / sz;
                const v = in.uv[1] + (@as(f32, @floatFromInt(y)) + 0.5) / sz;
                const t = c.tex[@as(usize, @intFromFloat(v * sz)) * c.size + @as(usize, @intFromFloat(u * sz))];
                std.testing.expectEqual(c.fonts.coverage.items[gl.off + y * gl.w + x], t) catch unreachable;
            };
        }
    };
    var i: usize = 0;
    scene.glyphs(&s, &fonts, &s.ops.items[0], Ctx{ .out = out.items, .tex = tex, .size = atlas.size, .fonts = &fonts, .i = &i }, Ctx.glyph);
}
