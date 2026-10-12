//! Frames times headless frames of each workload: building the view,
//! laying it out, painting the scene and drawing what changed on the CPU,
//! at 2× density as MyGo's BenchmarkFrame does.
const std = @import("std");
const myzig = @import("myzig");
const counter = @import("counter");
const quotes = @import("quotes");
const list = @import("list");

fn bench(comptime State: type, name: []const u8, fonts: *myzig.text.Fonts, state: *State, view: anytype, w: u32, h: u32, full: bool, window: bool) !void {
    const gpa = std.heap.c_allocator;
    var hl = try myzig.Headless(State).init(gpa, fonts, state, view, w, h, 2);
    defer hl.deinit();
    // A window presents only what changed and lets the server move scrolled pixels.
    hl.renderer.retain_pixels = !window;
    for (0..50) |_| hl.frame();
    if (std.posix.getenv("DUMP")) |dir| {
        var buf: [256]u8 = undefined;
        try hl.writePPM(try std.fmt.bufPrint(&buf, "{s}/{s}.ppm", .{ dir, name }));
    }
    if (std.posix.getenv("DAMAGE") != null) for (0..8) |_| {
        const p0 = hl.renderer.stats.pixels;
        hl.frame();
        std.debug.print("px {d} ", .{hl.renderer.stats.pixels - p0});
        std.debug.print("{s}: {d} ops, damage:", .{ name, hl.scene.ops.items.len });
        for (hl.renderer.damage.items) |d| std.debug.print(" [{d},{d} {d}x{d}]", .{ d.x0, d.y0, d.x1 - d.x0, d.y1 - d.y0 });
        std.debug.print("\n", .{});
    };
    hl.build_ns = 0;
    hl.raster_ns = 0;
    hl.pixels_drawn = 0;
    var timer = try std.time.Timer.start();
    var n: u64 = 0;
    while (timer.read() < 2 * std.time.ns_per_s) : (n += 1) {
        if (full) hl.renderer.invalidate();
        hl.frame();
    }
    const ns = timer.read();
    const nf: f64 = @floatFromInt(n);
    std.debug.print("{s:<24} {d:>8.3} ms/frame (build+layout+paint {d:.3}, raster {d:.3}, {d:.0} px)\n", .{
        name,
        @as(f64, @floatFromInt(ns)) / nf / 1e6,
        @as(f64, @floatFromInt(hl.build_ns)) / nf / 1e6,
        @as(f64, @floatFromInt(hl.raster_ns)) / nf / 1e6,
        @as(f64, @floatFromInt(hl.pixels_drawn)) / nf,
    });
}

pub fn main() !void {
    var fonts = try myzig.text.Fonts.init(std.heap.c_allocator);
    var c = counter.Counter{};
    try bench(counter.Counter, "counter unchanged", &fonts, &c, counter.view, 360, 260, false, false);
    try bench(counter.Counter, "counter full redraw", &fonts, &c, counter.view, 360, 260, true, false);
    var q = quotes.Quotes.init();
    try bench(quotes.Quotes, "quotes streaming", &fonts, &q, quotes.view, 560, 1040, false, false);
    var l = list.Items{ .auto = true };
    try bench(list.Items, "list scrolling", &fonts, &l, list.view, 640, 720, false, false);
    var l2 = list.Items{ .auto = true };
    try bench(list.Items, "list scrolling (window)", &fonts, &l2, list.view, 640, 720, false, true);
}
