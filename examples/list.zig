//! List scrolls a virtualized list of 10,000 rows, the "sidebar scrolling"
//! workload of gpui-fast's benchmarks. With AUTOSCROLL set it scrolls
//! itself by 4 px a frame, back and forth.
//!
//!     zig build list
const std = @import("std");
const myzig = @import("myzig");
const ui = myzig.ui;

pub const count = 10_000;

pub const Items = struct {
    list: ui.ListState = .{},
    auto: bool = false,
    dir: f32 = 4,
};

pub fn view(s: *Items, c: *ui.Context) void {
    if (s.auto) {
        c.animationFrame();
        s.list.scrollBy(s.dir);
        if (s.list.offset <= 0 or s.list.offset >= s.list.maxOffset()) s.dir = -s.dir;
    }
    if (c.shortcut(.page_down)) s.list.scrollBy(s.list.viewport);
    if (c.shortcut(.page_up)) s.list.scrollBy(-s.list.viewport);
    const t = c.theme;
    c.column(.{ .fill = true, .@"align" = .stretch });
    defer c.end();
    {
        c.row(.{ .padding = 12, .gap = 8, .@"align" = .center, .background = t.surface });
        defer c.end();
        c.text("Inbox", .{ .size = 16, .bold = true });
        c.textf("{d} messages", .{count}, .{ .color = t.text_muted, .size = 12 });
    }
    var it = c.list(&s.list, count, 36, .{});
    while (it.next()) |i| {
        c.text(dot(i), .{ .size = 10, .color = if (i % 3 == 0) t.accent else t.border });
        c.textf("Message {d}", .{i}, .{ .width = 140, .bold = i % 7 == 0 });
        c.textf("Subject line for item {d}, with a preview", .{i * 7919 % 10007}, .{ .color = t.text_muted, .grow = 1 });
        c.textf("{d:0>2}:{d:0>2}", .{ (i / 60) % 24, i % 60 }, .{ .size = 12, .color = t.text_muted });
    }
}

fn dot(i: u32) []const u8 {
    return if (i % 3 == 0) "●" else "○";
}

pub fn main() !void {
    var s = Items{ .auto = myzig.env.get("AUTOSCROLL") != null };
    try myzig.run(&s, view, .{ .title = "List", .width = 640, .height = 720 });
}

test "scrolling draws what a full redraw draws" {
    const gpa = std.testing.allocator;
    var fonts = try myzig.text.Fonts.init(gpa);
    defer fonts.deinit();
    var s = Items{ .auto = true };
    var h = try myzig.Headless(Items).init(gpa, &fonts, &s, view, 640, 720, 2);
    defer h.deinit();
    var t = Items{ .auto = false };
    var full = try myzig.Headless(Items).init(gpa, &fonts, &t, view, 640, 720, 2);
    defer full.deinit();
    for (0..120) |i| {
        h.frame();
        if (i % 7 == 0) s.dir = -s.dir * 3; // speeds up, and turns back
        if (i % 13 == 0) h.scroll(100, 300, 48); // a wheel too
        t.list = s.list;
        full.renderer.invalidate();
        full.frame();
        try std.testing.expectEqualSlices(u32, full.pixels, h.pixels);
    }
}
