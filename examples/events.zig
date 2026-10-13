//! Events shows the input the window gets: every mouse button, clicks and
//! double clicks, the wheel both ways (a trackpad's exact deltas), the
//! side buttons, typed and composed text, and the clipboard. Each event is
//! also printed, which is how CI drives a real window and checks it.
//!
//!     zig build events
const std = @import("std");
const myzig = @import("myzig");
const ui = myzig.ui;

// Small release binaries: MyZig prints panics and log messages.
pub const panic = myzig.panic;
pub const std_options = myzig.std_options;

pub const Events = struct {
    gpa: std.mem.Allocator,
    log: std.ArrayListUnmanaged([]u8) = .empty,
    log_scroll: ui.ScrollState = .{},
    pad_scroll: ui.ScrollState = .{},
    last_offset: f32 = 0,
    sideways: f32 = 0,
    field: ui.TextInput = .{},
    menu: ui.Menu = .{},
    quiet: bool = false,

    pub fn deinit(s: *Events) void {
        for (s.log.items) |l| s.gpa.free(l);
        s.log.deinit(s.gpa);
        s.field.deinit(s.gpa);
    }

    fn note(s: *Events, comptime fmt: []const u8, args: anytype) void {
        const line = std.fmt.allocPrint(s.gpa, fmt, args) catch return;
        if (!s.quiet) {
            myzig.sys.out(line);
            myzig.sys.out("\n");
        }
        if (s.log.items.len == 200) s.gpa.free(s.log.orderedRemove(0));
        s.log.append(s.gpa, line) catch s.gpa.free(line);
        s.log_scroll.offset = std.math.floatMax(f32);
    }
};

pub fn view(s: *Events, c: *ui.Context) void {
    const t = c.theme;
    c.row(.{ .fill = true, .padding = 20, .gap = 20, .@"align" = .stretch });
    defer c.end();
    {
        c.column(.{ .gap = 12, .grow = 1 });
        defer c.end();
        c.text("Events", .{ .size = 22, .bold = true });
        if (c.button("Click", .{ .primary = true })) s.note("click {d}", .{c.input.clicks});
        if (c.textInput(&s.field, .{ .placeholder = "Type, compose or paste", .width = 400, .key = "field" })) {
            const v = s.field.value();
            s.note("text {d} bytes: {s}", .{ v.len, v[0..@min(v.len, 40)] });
        }
        // A pad that takes the other buttons and the wheel.
        c.scrollView(&s.pad_scroll, .{ .grow = 1, .padding = 12, .gap = 8, .background = t.surface, .radius = t.radius, .key = "pad" });
        if (c.pressedWith(.right)) |at| {
            s.note("right {d:.0},{d:.0}", .{ at.x, at.y });
            s.menu.openAt(at);
        }
        if (c.pressedWith(.middle)) |at| s.note("middle {d:.0},{d:.0}", .{ at.x, at.y });
        c.text("Right or middle click here; scroll either way.", .{ .color = t.text_muted });
        for (0..40) |i| c.textf("Row {d}", .{i}, .{});
        c.end();
    }
    if (s.pad_scroll.offset != s.last_offset) {
        s.note("scroll y {d:.1}", .{s.pad_scroll.offset - s.last_offset});
        s.last_offset = s.pad_scroll.offset;
    }
    if (c.input.wheel_x != 0) {
        s.sideways += c.input.wheel_x;
        s.note("scroll x {d:.1}", .{c.input.wheel_x});
        c.input.wheel_x = 0;
    }
    if (c.navigation()) |n| s.note("{s}", .{@tagName(n)});
    if (c.beginMenu(&s.menu)) {
        defer c.endMenu();
        if (c.menuItem("Copy all", .{})) {
            s.note("menu copy", .{});
        }
        if (c.menuItem("Clear log", .{})) {
            for (s.log.items) |l| s.gpa.free(l);
            s.log.clearRetainingCapacity();
        }
    }
    {
        c.column(.{ .gap = 8, .width = 280 });
        defer c.end();
        c.text("Log", .{ .size = 16, .bold = true });
        c.scrollView(&s.log_scroll, .{ .gap = 4, .padding = 10, .background = t.surface, .radius = t.radius, .key = "log" });
        defer c.end();
        for (s.log.items) |l| c.text(l, .{ .size = 12 });
    }
}

pub fn main() !void {
    var s = Events{ .gpa = std.heap.c_allocator };
    defer s.deinit();
    try myzig.run(&s, view, .{ .title = "Events", .width = 760, .height = 520 });
}

const testing = std.testing;

test "the pad takes the right and middle buttons, and the wheel" {
    var fonts = try myzig.text.Fonts.init(testing.allocator);
    defer fonts.deinit();
    var s = Events{ .gpa = testing.allocator, .quiet = true };
    defer s.deinit();
    var h = try myzig.Headless(Events).init(testing.allocator, &fonts, &s, view, 760, 520, 1);
    defer h.deinit();
    h.frame();
    try h.rightClick("Row 3");
    try testing.expect(std.mem.startsWith(u8, s.log.items[0], "right "));
    try testing.expect(s.menu.open);
    h.key(.escape);
    const r = h.rectOf("Row 3").?;
    if (h.ctx.pointerMove(r.x, r.y)) h.frame();
    _ = h.ctx.pointerPress(r.x, r.y, .middle, 1);
    h.frame();
    try testing.expect(std.mem.startsWith(u8, s.log.items[1], "middle "));
    if (h.ctx.scrollXY(r.x, r.y, 12.5, 30)) h.frame();
    try testing.expectEqual(@as(f32, 12.5), s.sideways);
    try testing.expectEqual(@as(f32, 30), s.pad_scroll.offset);
}
