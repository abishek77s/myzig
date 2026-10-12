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

        /// frame builds, lays out and draws one frame.
        pub fn frame(h: *Self) void {
            h.ctx.build(h.state, h.view, @as(f32, @floatFromInt(h.width)) / h.scale, @as(f32, @floatFromInt(h.height)) / h.scale, 0);
            h.scene.reset(h.width, h.height, h.scale, h.ctx.theme.background);
            h.ctx.endFrame(&h.scene);
            const cv = raster.Canvas{ .pixels = h.pixels, .width = h.width, .height = h.height, .stride = @intCast(h.width) };
            _ = h.renderer.render(&h.scene, h.fonts, &cv);
        }

        pub fn click(h: *Self, label: []const u8) !void {
            const r = h.ctx.find(label) orelse return error.NotFound;
            const cx = r.x + r.w / 2;
            const cy = r.y + r.h / 2;
            if (h.ctx.pointerMove(cx, cy)) h.frame();
            _ = h.ctx.pointerDown(cx, cy);
            h.frame();
            _ = h.ctx.pointerUp(cx, cy);
            h.frame();
        }

        pub fn key(h: *Self, k: ui.Key) void {
            _ = h.ctx.keyDown(k);
            h.frame();
        }

        pub fn scroll(h: *Self, x: f32, y: f32, dy: f32) void {
            if (h.ctx.scroll(x, y, dy)) h.frame();
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
