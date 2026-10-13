//! Headless runs a view without a window: frames are built, laid out and
//! drawn into memory, and input is sent as a user would. Tests and the
//! benchmarks use it.

const std = @import("std");
const ui = @import("ui.zig");
const text = @import("text.zig");
const scene = @import("scene.zig");
const raster = @import("raster.zig");

pub fn Headless(comptime State: type) type {
    return struct {
        const Self = @This();
        pub const View = *const fn (*State, *ui.Context) void;

        gpa: std.mem.Allocator,
        state: *State,
        view: View,
        fonts: *text.Fonts,
        ctx: ui.Context,
        scene: scene.Scene,
        renderer: scene.Renderer,
        pixels: []u32,
        width: i32,
        height: i32,
        scale: f32,
        build_ns: u64 = 0,
        raster_ns: u64 = 0,
        pixels_drawn: u64 = 0,

        pub fn init(gpa: std.mem.Allocator, fonts: *text.Fonts, state: *State, view: View, width: u32, height: u32, scale: f32) !Self {
            const pw: i32 = @intFromFloat(@round(@as(f32, @floatFromInt(width)) * scale));
            const ph: i32 = @intFromFloat(@round(@as(f32, @floatFromInt(height)) * scale));
            var self = Self{
                .gpa = gpa,
                .state = state,
                .view = view,
                .fonts = fonts,
                .ctx = ui.Context.init(gpa, fonts),
                .scene = .{ .gpa = gpa },
                .renderer = .{ .gpa = gpa },
                .pixels = try gpa.alloc(u32, @intCast(pw * ph)),
                .width = pw,
                .height = ph,
                .scale = scale,
            };
            self.frame();
            return self;
        }

        pub fn deinit(h: *Self) void {
            h.ctx.deinit();
            h.scene.deinit();
            h.renderer.deinit();
            h.gpa.free(h.pixels);
        }

        /// frame builds, lays out and draws one frame, at the time of the
        /// last.
        pub fn frame(h: *Self) void {
            h.frameAt(h.ctx.frame_time);
        }

        /// frameAt is frame at the time now (ns), for timers.
        pub fn frameAt(h: *Self, now: u64) void {
            var timer = std.time.Timer.start() catch unreachable;
            h.ctx.build(h.state, h.view, @as(f32, @floatFromInt(h.width)) / h.scale, @as(f32, @floatFromInt(h.height)) / h.scale, now);
            h.scene.reset(h.width, h.height, h.scale, h.ctx.theme.background);
            h.ctx.endFrame(&h.scene);
            const t1 = timer.read();
            const cv = raster.Canvas{ .pixels = h.pixels, .width = h.width, .height = h.height, .stride = @intCast(h.width) };
            const px = h.renderer.stats.pixels;
            _ = h.renderer.render(&h.scene, h.fonts, &cv);
            h.build_ns += t1;
            h.raster_ns += timer.read() - t1;
            h.pixels_drawn += h.renderer.stats.pixels - px;
        }

        pub fn click(h: *Self, label: []const u8) !void {
            return h.clickN(label, 1);
        }

        /// clickN is the clicks'th click of a quick series on label: 2 for
        /// a double click.
        pub fn clickN(h: *Self, label: []const u8, clicks: u8) !void {
            const r = h.ctx.find(label) orelse return error.NotFound;
            h.press(r.x + r.w / 2, r.y + r.h / 2, .left, clicks);
        }

        /// rightClick presses and releases the right button on label.
        pub fn rightClick(h: *Self, label: []const u8) !void {
            const r = h.ctx.find(label) orelse return error.NotFound;
            h.press(r.x + r.w / 2, r.y + r.h / 2, .right, 1);
        }

        fn press(h: *Self, x: f32, y: f32, button: ui.MouseButton, clicks: u8) void {
            if (h.ctx.pointerMove(x, y)) h.frame();
            _ = h.ctx.pointerPress(x, y, button, clicks);
            h.frame();
            _ = h.ctx.pointerRelease(x, y, button);
            h.frame();
        }

        /// compose shows s as an input method's text being composed, its
        /// caret at its end; "" ends it.
        pub fn compose(h: *Self, s: []const u8) void {
            _ = h.ctx.setPreedit(s, s.len);
            h.frame();
        }

        pub fn key(h: *Self, k: ui.Key) void {
            _ = h.ctx.keyDownMods(k, .{});
            h.frame();
        }

        /// keyMods presses k with mods held.
        pub fn keyMods(h: *Self, k: ui.Key, mods: ui.Mods) void {
            _ = h.ctx.keyDownMods(k, mods);
            h.frame();
            h.ctx.setMods(.{});
        }

        /// typeText types s, as the platforms deliver typed text.
        pub fn typeText(h: *Self, s: []const u8) void {
            _ = h.ctx.typeText(s);
            h.frame();
        }

        /// clickAt clicks at x, y (logical pixels).
        pub fn clickAt(h: *Self, x: f32, y: f32) void {
            h.press(x, y, .left, 1);
        }

        /// drag presses at (x0, y0), moves to (x1, y1) in steps, and lets go.
        pub fn drag(h: *Self, x0: f32, y0: f32, x1: f32, y1: f32) void {
            if (h.ctx.pointerMove(x0, y0)) h.frame();
            _ = h.ctx.pointerPress(x0, y0, .left, 1);
            h.frame();
            const steps = 4;
            for (1..steps + 1) |i| {
                const f = @as(f32, @floatFromInt(i)) / steps;
                if (h.ctx.pointerMove(x0 + (x1 - x0) * f, y0 + (y1 - y0) * f)) h.frame();
            }
            _ = h.ctx.pointerUp(x1, y1);
            h.frame();
        }

        /// rectOf is where the text (or the control labelled) label is.
        pub fn rectOf(h: *const Self, label: []const u8) ?ui.Rect {
            return h.ctx.find(label);
        }

        pub fn scroll(h: *Self, x: f32, y: f32, dy: f32) void {
            if (h.ctx.scroll(x, y, dy)) h.frame();
        }

        /// navigate presses the mouse's back or forward button.
        pub fn navigate(h: *Self, nav: ui.Nav) void {
            _ = h.ctx.pointerPress(0, 0, if (nav == .back) .back else .forward, 1);
            h.frame();
        }

        pub fn hasText(h: *const Self, s: []const u8) bool {
            return h.ctx.hasText(s);
        }

        /// writePPM saves the last frame, to look at it.
        pub fn writePPM(h: *const Self, path: []const u8) !void {
            var f = try std.fs.cwd().createFile(path, .{});
            defer f.close();
            var wbuf: [64 << 10]u8 = undefined;
            var fw = f.writer(&wbuf);
            const w = &fw.interface;
            try w.print("P6\n{d} {d}\n255\n", .{ h.width, h.height });
            for (h.pixels) |p| try w.writeAll(&.{ @truncate(p >> 16), @truncate(p >> 8), @truncate(p) });
            try w.flush();
        }
    };
}
