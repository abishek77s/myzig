//! Frames times headless frames of each workload: building the view,
//! laying it out, painting the scene and drawing what changed on the CPU,
//! at 2× density as MyGo's BenchmarkFrame does.
const std = @import("std");
const myzig = @import("myzig");
const counter = @import("counter");
const quotes = @import("quotes");
const list = @import("list");

fn bench(comptime State: type, name: []const u8, fonts: *myzig.text.Fonts, state: *State, view: anytype, w: u32, h: u32, full: bool) !void {
    const gpa = std.heap.c_allocator;
    var hl = try myzig.Headless(State).init(gpa, fonts, state, view, w, h, 2);
    defer hl.deinit();
    for (0..50) |_| hl.frame();
    if (std.posix.getenv("DUMP")) |dir| {
        var buf: [256]u8 = undefined;
        try hl.writePPM(try std.fmt.bufPrint(&buf, "{s}/{s}.ppm", .{ dir, name }));
    }
    var timer = try std.time.Timer.start();
    var n: u64 = 0;
    while (timer.read() < 2 * std.time.ns_per_s) : (n += 1) {
        if (full) hl.renderer.invalidate();
        hl.frame();
    }
    const ns = timer.read();
    std.debug.print("{s:<24} {d:>9.3} ms/frame {d:>9.0} frames/s\n", .{ name, @as(f64, @floatFromInt(ns)) / @as(f64, @floatFromInt(n)) / 1e6, @as(f64, @floatFromInt(n)) * 1e9 / @as(f64, @floatFromInt(ns)) });
}

pub fn main() !void {
    var fonts = try myzig.text.Fonts.init(std.heap.c_allocator);
    var c = counter.Counter{};
    try bench(counter.Counter, "counter unchanged", &fonts, &c, counter.view, 360, 260, false);
    try bench(counter.Counter, "counter full redraw", &fonts, &c, counter.view, 360, 260, true);
    var q = quotes.Quotes.init();
    try bench(quotes.Quotes, "quotes streaming", &fonts, &q, quotes.view, 560, 1040, false);
    var l = list.Items{ .auto = true };
    try bench(list.Items, "list scrolling", &fonts, &l, list.view, 640, 720, false);
}
