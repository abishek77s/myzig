//! Counter is a counter in native UI, MyGo's counter-native in Zig. The
//! arrow keys count too.
//!
//!     zig build counter
const std = @import("std");
const myzig = @import("myzig");
const ui = myzig.ui;

// Small release binaries: MyZig prints panics and log messages.
pub const panic = myzig.panic;
pub const std_options = myzig.std_options;

pub const Counter = struct { n: i64 = 0 };

pub fn view(s: *Counter, c: *ui.Context) void {
    if (c.shortcut(.up)) s.n += 1;
    if (c.shortcut(.down)) s.n -= 1;
    const t = c.theme;
    c.column(.{ .fill = true, .center = true, .gap = 16 });
    defer c.end();
    c.textf("{d}", .{s.n}, .{ .size = 56, .bold = true });
    {
        c.row(.{ .gap = 8 });
        defer c.end();
        if (c.button("−", .{ .key = "decrement", .width = 44 })) s.n -= 1;
        if (c.button("Reset", .{ .disabled = s.n == 0 })) s.n = 0;
        if (c.button("+", .{ .key = "increment", .primary = true, .width = 44 })) s.n += 1;
    }
    c.text("↑ and ↓ count too", .{ .size = 12, .color = t.text_muted });
}

pub fn main() !void {
    var s = Counter{};
    try myzig.run(&s, view, .{ .title = "Counter", .width = 360, .height = 260 });
}

test "counter" {
    var fonts = try myzig.text.Fonts.init(std.testing.allocator);
    defer fonts.deinit();
    var s = Counter{};
    var h = try myzig.Headless(Counter).init(std.testing.allocator, &fonts, &s, view, 360, 260, 1);
    defer h.deinit();
    try h.click("+");
    try h.click("+");
    try h.click("−");
    try std.testing.expectEqual(@as(i64, 1), s.n);
    try std.testing.expect(h.hasText("1"));
    h.key(.up);
    h.key(.up);
    h.key(.down);
    try std.testing.expectEqual(@as(i64, 2), s.n);
    try h.click("Reset");
    try std.testing.expectEqual(@as(i64, 0), s.n);
}
