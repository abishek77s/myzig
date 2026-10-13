//! Gallery shows every control MyZig has: text inputs, checkboxes, radio
//! buttons, toggles, a slider, a progress bar, buttons and a scroll view,
//! a select, a tooltip, a dialog, a context menu and an image, with a dark
//! theme a toggle switches to. Text wraps. Tab moves the focus.
//!
//!     zig build gallery
const std = @import("std");
const myzig = @import("myzig");
const ui = myzig.ui;

pub const Size = enum { small, medium, large };

pub const regions = [_][]const u8{ "Europe", "North America", "South America", "Asia Pacific", "Africa" };

pub const Gallery = struct {
    gpa: std.mem.Allocator,
    name: ui.TextInput = .{},
    password: ui.TextInput = .{},
    notifications: bool = true,
    digest: bool = false,
    size: Size = .medium,
    dark: bool = false,
    volume: f32 = 40,
    log: std.ArrayListUnmanaged([]u8) = .empty,
    log_scroll: ui.ScrollState = .{},
    saves: u32 = 0,
    region: usize = 0,
    confirm_clear: bool = false,
    log_menu: ui.Menu = .{},
    /// logo is decoded once, from the PNG built into the executable.
    logo: ?*myzig.Image = null,

    pub fn deinit(s: *Gallery) void {
        if (s.logo) |l| l.deinit();
        s.name.deinit(s.gpa);
        s.password.deinit(s.gpa);
        for (s.log.items) |l| s.gpa.free(l);
        s.log.deinit(s.gpa);
    }

    fn note(s: *Gallery, comptime fmt: []const u8, args: anytype) void {
        const line = std.fmt.allocPrint(s.gpa, fmt, args) catch return;
        s.log.append(s.gpa, line) catch s.gpa.free(line);
        // Follow the end of the log.
        s.log_scroll.offset = std.math.floatMax(f32);
    }
};

fn section(c: *ui.Context, title: []const u8) void {
    c.text(title, .{ .size = 12, .bold = true, .color = c.theme.text_muted });
}

pub fn view(s: *Gallery, c: *ui.Context) void {
    c.theme = if (s.dark) ui.Theme.dark else .{};
    const t = c.theme;
    c.row(.{ .fill = true, .padding = 24, .gap = 24, .@"align" = .stretch });
    defer c.end();

    // The controls.
    {
        c.column(.{ .gap = 12, .grow = 1 });
        defer c.end();
        c.text("Components", .{ .size = 22, .bold = true });

        section(c, "TEXT INPUT");
        _ = c.textInput(&s.name, .{ .placeholder = "Your name", .width = 280, .key = "name" });
        _ = c.textInput(&s.password, .{ .placeholder = "Password", .width = 280, .password = true, .key = "password" });
        if (s.name.submitted) s.note("Submitted {s}", .{s.name.value()});
        if (s.name.value().len > 0)
            c.textf("Hello, {s}!", .{s.name.value()}, .{ .color = t.text_muted })
        else
            c.text("Type a name, then Enter.", .{ .color = t.text_muted });

        section(c, "CHOICES");
        if (c.checkbox("Notifications", &s.notifications, .{})) s.note("Notifications {s}", .{if (s.notifications) "on" else "off"});
        if (c.checkbox("Weekly digest", &s.digest, .{ .disabled = !s.notifications })) s.note("Digest {s}", .{if (s.digest) "on" else "off"});
        {
            c.row(.{ .gap = 16 });
            defer c.end();
            inline for (.{ .{ "Small", Size.small }, .{ "Medium", Size.medium }, .{ "Large", Size.large } }) |o| {
                if (c.radio(o[0], s.size == o[1], .{})) {
                    s.size = o[1];
                    s.note("Size {s}", .{o[0]});
                }
            }
        }
        {
            c.row(.{ .gap = 16, .@"align" = .center });
            defer c.end();
            if (c.toggle("Dark mode", &s.dark, .{})) s.note("Dark mode {s}", .{if (s.dark) "on" else "off"});
            if (c.select(&regions, &s.region, .{ .width = 170, .key = "region" })) s.note("Region {s}", .{regions[s.region]});
        }

        section(c, "VOLUME");
        {
            c.row(.{ .gap = 12, .@"align" = .center });
            defer c.end();
            _ = c.slider(&s.volume, 0, 100, .{ .width = 220, .step = 1, .key = "volume" });
            c.textf("{d:.0}", .{s.volume}, .{ .width = 32, .@"align" = .end });
        }
        c.progress(s.volume / 100, .{ .width = 264 });

        c.spacer();
        {
            c.row(.{ .gap = 8 });
            defer c.end();
            if (c.button("Save", .{ .primary = true })) {
                s.saves += 1;
                s.note("Saved ({d})", .{s.saves});
            }
            c.tooltip("Adds a line to the activity log");
            if (c.button("Clear log", .{ .disabled = s.log.items.len == 0 })) s.confirm_clear = true;
        }
    }

    if (c.beginModal(&s.confirm_clear, .{ .title = "Clear the activity log?", .width = 360 })) {
        defer c.endModal();
        const n = s.log.items.len;
        c.textf("Its {d} line{s} will be gone for good.", .{ n, if (n == 1) "" else "s" }, .{ .wrap = true, .color = t.text_muted });
        c.row(.{ .gap = 8, .justify = .end, .@"align" = .center, .fill = true });
        defer c.end();
        if (c.button("Cancel", .{})) s.confirm_clear = false;
        if (c.button("Clear", .{ .primary = true, .key = "confirm" })) {
            for (s.log.items) |l| s.gpa.free(l);
            s.log.clearRetainingCapacity();
            s.confirm_clear = false;
        }
    }

    c.divider();

    // What happened, in a scroll view.
    {
        c.column(.{ .gap = 8, .width = 260 });
        defer c.end();
        {
            c.row(.{ .gap = 10, .@"align" = .center });
            defer c.end();
            if (s.logo == null) s.logo = myzig.Image.decode(s.gpa, @embedFile("assets/logo.png")) catch null;
            if (s.logo) |logo| c.image(logo, .{ .width = 32, .radius = 8 });
            c.text("Activity", .{ .size = 16, .bold = true });
        }
        c.scrollView(&s.log_scroll, .{ .gap = 6, .padding = 10, .background = t.surface, .radius = t.radius, .key = "log" });
        defer c.end();
        // A right click opens the log's menu.
        if (c.pressedWith(.right)) |at| s.log_menu.openAt(at);
        if (s.log.items.len == 0) c.text("Nothing yet", .{ .color = t.text_muted });
        for (s.log.items) |l| c.text(l, .{});
    }
    if (c.beginMenu(&s.log_menu)) {
        defer c.endMenu();
        if (c.menuItem("Save", .{ .shortcut = "Ctrl+S" })) {
            s.saves += 1;
            s.note("Saved ({d})", .{s.saves});
        }
        c.menuSeparator();
        if (c.menuItem("Clear log…", .{ .disabled = s.log.items.len == 0 })) s.confirm_clear = true;
    }
    if (c.shortcutMods(.s, .{ .ctrl = true })) {
        s.saves += 1;
        s.note("Saved ({d})", .{s.saves});
    }
}

pub fn main() !void {
    var s = Gallery{ .gpa = std.heap.c_allocator };
    defer s.deinit();
    try myzig.run(&s, view, .{ .title = "Gallery", .width = 720, .height = 560 });
}

const testing = std.testing;

fn harness(s: *Gallery, fonts: *myzig.text.Fonts) !myzig.Headless(Gallery) {
    var h = try myzig.Headless(Gallery).init(testing.allocator, fonts, s, view, 720, 560, 1);
    h.frame();
    return h;
}

test "text input: typing, editing keys, selection and the clipboard" {
    var fonts = try myzig.text.Fonts.init(testing.allocator);
    defer fonts.deinit();
    var s = Gallery{ .gpa = testing.allocator };
    defer s.deinit();
    var h = try harness(&s, &fonts);
    defer h.deinit();

    try h.click("Your name");
    h.typeText("Ada Lovelace");
    try testing.expectEqualStrings("Ada Lovelace", s.name.value());
    try testing.expect(h.hasText("Hello, Ada Lovelace!"));
    h.key(.backspace);
    h.keyMods(.backspace, .{ .ctrl = true }); // a word
    try testing.expectEqualStrings("Ada ", s.name.value());
    h.key(.home);
    h.typeText("¡");
    try testing.expectEqualStrings("¡Ada ", s.name.value());
    h.key(.right); // past "A", a code point after a two-byte one
    h.key(.delete);
    try testing.expectEqualStrings("¡Aa ", s.name.value());
    // Select all, copy, replace, paste twice.
    h.keyMods(.a, .{ .ctrl = true });
    h.keyMods(.c, .{ .ctrl = true });
    h.typeText("x");
    try testing.expectEqualStrings("x", s.name.value());
    h.keyMods(.v, .{ .ctrl = true });
    h.keyMods(.v, .{ .ctrl = true });
    try testing.expectEqualStrings("x¡Aa ¡Aa ", s.name.value());
    // Shift+Left selects; typing replaces the selection.
    h.keyMods(.left, .{ .shift = true });
    h.keyMods(.left, .{ .shift = true });
    h.typeText("!");
    try testing.expectEqualStrings("x¡Aa ¡A!", s.name.value());
    // Enter submits.
    h.key(.enter);
    try testing.expectEqual(@as(usize, 1), s.log.items.len);
    try testing.expectEqualStrings("Submitted x¡Aa ¡A!", s.log.items[0]);
}

test "text input: the pointer places the caret; passwords show dots" {
    var fonts = try myzig.text.Fonts.init(testing.allocator);
    defer fonts.deinit();
    var s = Gallery{ .gpa = testing.allocator };
    defer s.deinit();
    var h = try harness(&s, &fonts);
    defer h.deinit();
    const r = h.rectOf("Your name").?;
    try h.click("Your name");
    h.typeText("abcdef");
    // A click at the start of the text puts the caret there.
    h.clickAt(r.x + 10, r.y + r.h / 2);
    h.typeText("_");
    try testing.expectEqualStrings("_abcdef", s.name.value());
    // A drag selects.
    h.drag(r.x + 10, r.y + r.h / 2, r.x + r.w - 4, r.y + r.h / 2);
    h.typeText("z");
    try testing.expectEqualStrings("z", s.name.value());

    try h.click("Password");
    h.typeText("hunter2");
    try testing.expectEqualStrings("hunter2", s.password.value());
    try testing.expect(!h.hasText("hunter2"));
    // A password is not copied.
    h.keyMods(.a, .{ .ctrl = true });
    h.keyMods(.c, .{ .ctrl = true });
    try h.click("Your name");
    h.keyMods(.v, .{ .ctrl = true });
    try testing.expectEqualStrings("z", s.name.value());
}

test "checkboxes, radio buttons, the toggle and keyboard focus" {
    var fonts = try myzig.text.Fonts.init(testing.allocator);
    defer fonts.deinit();
    var s = Gallery{ .gpa = testing.allocator };
    defer s.deinit();
    var h = try harness(&s, &fonts);
    defer h.deinit();

    try h.click("Weekly digest");
    try testing.expect(s.digest);
    try h.click("Notifications");
    try testing.expect(!s.notifications);
    // Disabled now: a click does nothing.
    try h.click("Weekly digest");
    try testing.expect(s.digest);
    try h.click("Large");
    try testing.expectEqual(Size.large, s.size);
    try h.click("Large"); // already selected
    try testing.expectEqual(Size.large, s.size);
    try h.click("Dark mode");
    try testing.expect(s.dark);
    try testing.expectEqual(ui.Theme.dark.background, h.ctx.theme.background);

    // Tab from the name input reaches the password, then Notifications;
    // Space flips it.
    try h.click("Your name");
    h.key(.tab);
    h.key(.tab);
    h.key(.space);
    try testing.expect(s.notifications);
    // Shift+Tab goes back, to the password input, which takes Space as text.
    h.keyMods(.tab, .{ .shift = true });
    h.key(.space);
    h.typeText(" ");
    try testing.expectEqualStrings(" ", s.password.value());
    try testing.expect(s.notifications);
}

test "slider: dragging, pressing the track and the arrow keys" {
    var fonts = try myzig.text.Fonts.init(testing.allocator);
    defer fonts.deinit();
    var s = Gallery{ .gpa = testing.allocator };
    defer s.deinit();
    var h = try harness(&s, &fonts);
    defer h.deinit();
    var r: ui.Rect = undefined;
    for (h.ctx.hits.items) |hit| {
        if (hit.rect.w == 220 and hit.rect.h == 20) r = hit.rect;
    }
    h.drag(r.x + 8 + 0.4 * (r.w - 16), r.y + 10, r.x + r.w, r.y + 10);
    try testing.expectEqual(@as(f32, 100), s.volume);
    h.clickAt(r.x + 8 + 0.25 * (r.w - 16), r.y + 10);
    try testing.expectEqual(@as(f32, 25), s.volume);
    h.key(.right);
    h.key(.right);
    try testing.expectEqual(@as(f32, 27), s.volume);
    h.key(.page_down);
    try testing.expectEqual(@as(f32, 17), s.volume);
    h.key(.home);
    try testing.expectEqual(@as(f32, 0), s.volume);
    try testing.expect(h.hasText("0"));
}

test "the activity log scrolls" {
    var fonts = try myzig.text.Fonts.init(testing.allocator);
    defer fonts.deinit();
    var s = Gallery{ .gpa = testing.allocator };
    defer s.deinit();
    var h = try harness(&s, &fonts);
    defer h.deinit();
    for (0..40) |_| try h.click("Save");
    try testing.expectEqual(@as(u32, 40), s.saves);
    h.frame();
    // It followed the end; the wheel goes back up.
    try testing.expect(s.log_scroll.offset > 0);
    try testing.expectEqual(s.log_scroll.maxOffset(), s.log_scroll.offset);
    try testing.expect(h.hasText("Saved (40)"));
    h.scroll(600, 300, -1e6);
    try testing.expectEqual(@as(f32, 0), s.log_scroll.offset);
    try testing.expect(h.hasText("Saved (1)"));
    try h.click("Clear log");
    try testing.expect(h.hasText("Clear the activity log?"));
    // A click outside closes the dialog, and does not reach what is there.
    try h.click("Save");
    try testing.expectEqual(@as(u32, 40), s.saves);
    try testing.expect(!h.hasText("Clear the activity log?"));
    try h.click("Clear log");
    try h.click("Cancel");
    try testing.expect(!h.hasText("Clear the activity log?"));
    try h.click("Clear log");
    h.key(.escape);
    try testing.expect(!s.confirm_clear);
    try h.click("Clear log");
    try h.click("Clear");
    try testing.expect(h.hasText("Nothing yet"));
    try testing.expect(!h.hasText("Clear the activity log?"));
}

test "select: pointer, keyboard, and a click outside" {
    var fonts = try myzig.text.Fonts.init(testing.allocator);
    defer fonts.deinit();
    var s = Gallery{ .gpa = testing.allocator };
    defer s.deinit();
    var h = try harness(&s, &fonts);
    defer h.deinit();
    try h.click("Europe");
    try testing.expect(h.hasText("Asia Pacific"));
    try h.click("Asia Pacific");
    try testing.expectEqual(@as(usize, 3), s.region);
    try testing.expect(!h.hasText("Africa"));
    // The keyboard: open, down, Enter.
    try h.click("Asia Pacific");
    h.key(.down);
    h.key(.enter);
    try testing.expectEqual(@as(usize, 4), s.region);
    // Closed, the arrows step.
    h.key(.up);
    try testing.expectEqual(@as(usize, 3), s.region);
    // A click outside closes it, and does not reach what is there.
    try h.click("Asia Pacific");
    try h.click("Save");
    try testing.expect(!h.hasText("Africa"));
    try testing.expectEqual(@as(u32, 0), s.saves);
    try testing.expectEqualStrings("Region Asia Pacific", s.log.items[s.log.items.len - 1]);
}

test "a tooltip shows when the pointer rests" {
    var fonts = try myzig.text.Fonts.init(testing.allocator);
    defer fonts.deinit();
    var s = Gallery{ .gpa = testing.allocator };
    defer s.deinit();
    var h = try harness(&s, &fonts);
    defer h.deinit();
    const r = h.rectOf("Save").?;
    _ = h.ctx.pointerMove(r.x + 4, r.y + 4);
    h.frameAt(0);
    try testing.expect(!h.hasText("Adds a line to the activity log"));
    try testing.expect(h.ctx.wake_at > 0);
    h.frameAt(h.ctx.wake_at);
    try testing.expect(h.hasText("Adds a line to the activity log"));
    _ = h.ctx.pointerMove(1, 1);
    h.frameAt(h.ctx.frame_time + 1);
    try testing.expect(!h.hasText("Adds a line to the activity log"));
}

test "one option of an open select is highlighted, where the pointer is" {
    var fonts = try myzig.text.Fonts.init(testing.allocator);
    defer fonts.deinit();
    var s = Gallery{ .gpa = testing.allocator };
    defer s.deinit();
    var h = try harness(&s, &fonts);
    defer h.deinit();
    try h.click("Europe");
    const r = h.rectOf("South America").?;
    if (h.ctx.pointerMove(r.x + 4, r.y + r.h / 2)) h.frame();
    h.frame();
    var hot: usize = 0;
    var which: []const u8 = "";
    for (h.ctx.nodes.items) |n| if (n.kind == .option and n.flags.hot) {
        hot += 1;
        which = h.ctx.strings.items[n.str_off..][0..n.str_len];
    };
    try testing.expectEqual(@as(usize, 1), hot);
    try testing.expectEqualStrings("South America", which);
}

test "a right click on the log opens its menu; keys and clicks choose" {
    var fonts = try myzig.text.Fonts.init(testing.allocator);
    defer fonts.deinit();
    var s = Gallery{ .gpa = testing.allocator };
    defer s.deinit();
    var h = try harness(&s, &fonts);
    defer h.deinit();
    // Not on the controls: the log's menu is the log's.
    try h.rightClick("Save");
    try testing.expect(!s.log_menu.open);
    try h.rightClick("Nothing yet");
    try testing.expect(s.log_menu.open);
    try testing.expect(h.hasText("Ctrl+S"));
    // The menu's Save, over the button's.
    var item: ?ui.Rect = null;
    for (h.ctx.texts.items) |tx| if (std.mem.eql(u8, h.ctx.strings.items[tx.off..][0..tx.len], "Save")) {
        item = tx.rect;
    };
    h.clickAt(item.?.x + 4, item.?.y + item.?.h / 2);
    try testing.expectEqual(@as(u32, 1), s.saves);
    try testing.expect(!h.hasText("Ctrl+S"));
    // The keyboard: down twice skips nothing (the disabled item is still
    // an item), Enter chooses.
    try h.rightClick("Saved (1)");
    h.key(.down);
    h.key(.down);
    h.key(.enter);
    try testing.expect(s.confirm_clear);
    try testing.expect(!s.log_menu.open);
    h.key(.escape);
    // Escape and a click outside close it.
    try h.rightClick("Saved (1)");
    h.key(.escape);
    try testing.expect(!s.log_menu.open);
    try h.rightClick("Saved (1)");
    try h.click("Components");
    try testing.expect(!s.log_menu.open);
    try testing.expectEqual(@as(u32, 1), s.saves);
}

test "text input: a double click selects a word, a triple all" {
    var fonts = try myzig.text.Fonts.init(testing.allocator);
    defer fonts.deinit();
    var s = Gallery{ .gpa = testing.allocator };
    defer s.deinit();
    var h = try harness(&s, &fonts);
    defer h.deinit();
    try h.click("Your name");
    h.typeText("Ada Lovelace");
    const r = h.rectOf("Your name").?;
    // On "Ada".
    const x = r.x + 16;
    const y = r.y + r.h / 2;
    for (1..3) |n| {
        if (h.ctx.pointerMove(x, y)) h.frame();
        _ = h.ctx.pointerPress(x, y, .left, @intCast(n));
        h.frame();
        _ = h.ctx.pointerRelease(x, y, .left);
        h.frame();
    }
    try testing.expectEqual([2]u32{ 0, 3 }, s.name.selection());
    _ = h.ctx.pointerPress(x, y, .left, 3);
    h.frame();
    _ = h.ctx.pointerRelease(x, y, .left);
    h.frame();
    try testing.expectEqual([2]u32{ 0, 12 }, s.name.selection());
}

test "text input: an input method's text shows at the caret until committed" {
    var fonts = try myzig.text.Fonts.init(testing.allocator);
    defer fonts.deinit();
    var s = Gallery{ .gpa = testing.allocator };
    defer s.deinit();
    var h = try harness(&s, &fonts);
    defer h.deinit();
    try h.click("Your name");
    h.typeText("A");
    h.compose("にほ");
    try testing.expectEqualStrings("A", s.name.value());
    var shown = false;
    for (h.scene.ops.items) |op| if (op.kind == .text and std.mem.eql(u8, h.scene.string(&op), "Aにほ")) {
        shown = true;
    };
    try testing.expect(shown);
    // The candidate window goes by the caret, after the composed text.
    const ime = h.ctx.ime_rect.?;
    try testing.expect(ime.x > h.rectOf("Your name").?.x + 20);
    h.compose("");
    h.typeText("日本");
    try testing.expectEqualStrings("A日本", s.name.value());
}

test "the mouse's side buttons reach the view" {
    var fonts = try myzig.text.Fonts.init(testing.allocator);
    defer fonts.deinit();
    const Nav = struct {
        back: u32 = 0,
        fn view(st: *@This(), c: *ui.Context) void {
            if (c.navigation()) |n| if (n == .back) {
                st.back += 1;
            };
            c.textf("{d}", .{st.back}, .{});
        }
    };
    var st = Nav{};
    var h = try myzig.Headless(Nav).init(testing.allocator, &fonts, &st, Nav.view, 200, 100, 1);
    defer h.deinit();
    h.navigate(.back);
    try testing.expectEqual(@as(u32, 1), st.back);
    try testing.expect(h.hasText("1"));
}

test "the logo decodes and is drawn where its box is" {
    var fonts = try myzig.text.Fonts.init(testing.allocator);
    defer fonts.deinit();
    var s = Gallery{ .gpa = testing.allocator };
    defer s.deinit();
    var h = try harness(&s, &fonts);
    defer h.deinit();
    const logo = s.logo.?;
    try testing.expectEqual(@as(u32, 128), logo.width);
    var found = false;
    for (h.scene.ops.items) |op| if (op.kind == .image) {
        found = true;
        try testing.expectEqual(@as(f32, 32), op.rect.w);
        try testing.expectEqual(@as(f32, 32), op.rect.h);
    };
    try testing.expect(found);
}
