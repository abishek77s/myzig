//! Text input through an X input method (IBus, Fcitx, the compose tables):
//! where the method can, it sends what it is composing to the window,
//! which shows it in the text input (on-the-spot, callbacks); else the
//! method draws it at the caret (over-the-spot); else nothing is shown
//! until it commits. Either way its candidate window goes by the caret.

const std = @import("std");
const x = @import("../x11.zig");
const ui = @import("../ui.zig");

const XIMPreeditCallbacks = 0x0002;
const XIMPreeditPosition = 0x0004;
const XIMPreeditNothing = 0x0008;
const XIMStatusNothing = 0x0400;

const XIMStyles = extern struct { count: c_ushort, styles: [*]c_ulong };
const XIMCallback = extern struct { client_data: ?*anyopaque, callback: ?*const anyopaque };
const XIMText = extern struct {
    length: c_ushort,
    feedback: ?[*]c_ulong,
    encoding_is_wchar: c_int,
    string: extern union { multi_byte: ?[*]u8, wide_char: ?[*]u32 },
};
const DrawStruct = extern struct { caret: c_int, chg_first: c_int, chg_length: c_int, text: ?*XIMText };
const CaretStruct = extern struct { position: c_int, direction: c_int, style: c_int };
const XPoint = extern struct { x: c_short, y: c_short };

extern "X11" fn XGetIMValues(im: *anyopaque, ...) ?[*:0]u8;
extern "X11" fn XSetICValues(ic: *anyopaque, ...) ?[*:0]u8;
extern "X11" fn XVaCreateNestedList(unused: c_int, ...) ?*anyopaque;

pub const Ime = struct {
    im: ?*anyopaque = null,
    ic: ?*anyopaque = null,
    /// on_the_spot is set when the window shows what is composed.
    on_the_spot: bool = false,
    ctx: *ui.Context,
    /// changed is set when what is composed changed: a frame is due.
    changed: bool = false,
    text: [128]u21 = undefined,
    len: usize = 0,
    caret: usize = 0,
    spot: XPoint = .{ .x = -1, .y = -1 },
    callbacks: [4]XIMCallback = undefined,

    /// open opens the input method and a context of it for win. ime must
    /// not move after: the method calls back with its address.
    pub fn open(ime: *Ime, dpy: *x.Display, win: x.Window) void {
        _ = x.setlocale(0, ""); // LC_CTYPE
        _ = x.XSetLocaleModifiers("");
        const im = x.XOpenIM(dpy, null, null, null) orelse return;
        ime.im = im;
        var styles: ?*XIMStyles = null;
        _ = XGetIMValues(im, "queryInputStyle", &styles, @as(?*anyopaque, null));
        var callbacks = false;
        var position = false;
        if (styles) |st| {
            defer _ = x.XFree(st);
            for (st.styles[0..st.count]) |s| {
                if (s == XIMPreeditCallbacks | XIMStatusNothing) callbacks = true;
                if (s == XIMPreeditPosition | XIMStatusNothing) position = true;
            }
        }
        if (callbacks and std.posix.getenv("MYZIG_XIM_OVER_THE_SPOT") == null) {
            ime.callbacks = .{
                .{ .client_data = ime, .callback = @ptrCast(&onStart) },
                .{ .client_data = ime, .callback = @ptrCast(&onDone) },
                .{ .client_data = ime, .callback = @ptrCast(&onDraw) },
                .{ .client_data = ime, .callback = @ptrCast(&onCaret) },
            };
            const attrs = XVaCreateNestedList(0, "preeditStartCallback", &ime.callbacks[0], "preeditDoneCallback", &ime.callbacks[1], "preeditDrawCallback", &ime.callbacks[2], "preeditCaretCallback", &ime.callbacks[3], "spotLocation", &ime.spot, @as(?*anyopaque, null));
            defer _ = x.XFree(attrs);
            ime.ic = x.XCreateIC(im, "inputStyle", @as(c_ulong, XIMPreeditCallbacks | XIMStatusNothing), "clientWindow", win, "focusWindow", win, "preeditAttributes", attrs, @as(?*anyopaque, null));
            ime.on_the_spot = ime.ic != null;
        }
        if (ime.ic == null and position) {
            const attrs = XVaCreateNestedList(0, "spotLocation", &ime.spot, @as(?*anyopaque, null));
            defer _ = x.XFree(attrs);
            ime.ic = x.XCreateIC(im, "inputStyle", @as(c_ulong, XIMPreeditPosition | XIMStatusNothing), "clientWindow", win, "focusWindow", win, "preeditAttributes", attrs, @as(?*anyopaque, null));
        }
        if (ime.ic == null) ime.ic = x.XCreateIC(im, "inputStyle", @as(c_ulong, XIMPreeditNothing | XIMStatusNothing), "clientWindow", win, "focusWindow", win, @as(?*anyopaque, null));
        if (ime.ic) |i| x.XSetICFocus(i);
    }

    pub fn close(ime: *Ime) void {
        if (ime.ic) |i| x.XDestroyIC(i);
        if (ime.im) |m| _ = x.XCloseIM(m);
        ime.* = .{ .ctx = ime.ctx };
    }

    /// follow puts the method's candidate window (or, over-the-spot, what
    /// it composes) by the caret, at r in device pixels.
    pub fn follow(ime: *Ime, r: ui.Rect, scale: f32) void {
        const ic = ime.ic orelse return;
        const spot = XPoint{ .x = @intFromFloat(std.math.clamp(r.x * scale, 0, 32767)), .y = @intFromFloat(std.math.clamp((r.y + r.h) * scale, 0, 32767)) };
        if (spot.x == ime.spot.x and spot.y == ime.spot.y) return;
        ime.spot = spot;
        const attrs = XVaCreateNestedList(0, "spotLocation", &ime.spot, @as(?*anyopaque, null));
        defer _ = x.XFree(attrs);
        _ = XSetICValues(ic, "preeditAttributes", attrs, @as(?*anyopaque, null));
    }

    fn publish(ime: *Ime) void {
        var buf: [512]u8 = undefined;
        var n: usize = 0;
        var caret_byte: usize = 0;
        for (ime.text[0..ime.len], 0..) |cp, i| {
            if (i == ime.caret) caret_byte = n;
            n += std.unicode.utf8Encode(cp, buf[n..]) catch break;
        }
        if (ime.caret >= ime.len) caret_byte = n;
        if (ime.ctx.setPreedit(buf[0..n], caret_byte)) ime.changed = true;
    }

    fn of(client: ?*anyopaque) *Ime {
        return @ptrCast(@alignCast(client.?));
    }

    fn onStart(_: ?*anyopaque, client: ?*anyopaque, _: ?*anyopaque) callconv(.c) c_int {
        const ime = of(client);
        ime.len = 0;
        ime.caret = 0;
        return ime.text.len;
    }

    fn onDone(_: ?*anyopaque, client: ?*anyopaque, _: ?*anyopaque) callconv(.c) void {
        const ime = of(client);
        ime.len = 0;
        ime.caret = 0;
        ime.publish();
    }

    fn onDraw(_: ?*anyopaque, client: ?*anyopaque, call: ?*anyopaque) callconv(.c) void {
        const ime = of(client);
        const d: *const DrawStruct = @ptrCast(@alignCast(call orelse return));
        // Replace chg_length characters at chg_first with the text sent.
        var new: [128]u21 = undefined;
        var nn: usize = 0;
        if (d.text) |t| {
            if (t.encoding_is_wchar != 0) {
                if (t.string.wide_char) |w| for (w[0..t.length]) |cp| {
                    if (nn < new.len) new[nn] = std.math.cast(u21, cp) orelse 0xfffd;
                    nn += 1;
                };
            } else if (t.string.multi_byte) |mb| {
                const s = std.mem.span(@as([*:0]u8, @ptrCast(mb)));
                var it = std.unicode.Utf8View.initUnchecked(s).iterator();
                while (it.nextCodepoint()) |cp| {
                    if (nn < new.len) new[nn] = cp;
                    nn += 1;
                }
            }
        }
        nn = @min(nn, new.len);
        const first: usize = @min(@as(usize, @intCast(@max(0, d.chg_first))), ime.len);
        const gone: usize = @min(@as(usize, @intCast(@max(0, d.chg_length))), ime.len - first);
        var out: [128]u21 = undefined;
        var n: usize = 0;
        for (ime.text[0..first]) |cp| {
            out[n] = cp;
            n += 1;
        }
        for (new[0..nn]) |cp| if (n < out.len) {
            out[n] = cp;
            n += 1;
        };
        for (ime.text[first + gone .. ime.len]) |cp| if (n < out.len) {
            out[n] = cp;
            n += 1;
        };
        ime.text = out;
        ime.len = n;
        ime.caret = @min(@as(usize, @intCast(@max(0, d.caret))), ime.len);
        ime.publish();
    }

    fn onCaret(_: ?*anyopaque, client: ?*anyopaque, call: ?*anyopaque) callconv(.c) void {
        const ime = of(client);
        const cs: *CaretStruct = @ptrCast(@alignCast(call orelse return));
        switch (cs.direction) {
            0 => ime.caret = @min(ime.caret + 1, ime.len), // XIMForwardChar
            1 => ime.caret -|= 1, // XIMBackwardChar
            4 => ime.caret = 0, // XIMLineStart
            5 => ime.caret = ime.len, // XIMLineEnd
            10 => ime.caret = @min(@as(usize, @intCast(@max(0, cs.position))), ime.len), // XIMAbsolutePosition
            else => {},
        }
        cs.position = @intCast(ime.caret);
        ime.publish();
    }
};

test "a composed text is edited the way the method's draw calls say" {
    var fonts = try @import("../text.zig").Fonts.init(std.testing.allocator);
    defer fonts.deinit();
    var c = ui.Context.init(std.testing.allocator, &fonts);
    defer c.deinit();
    var ime = Ime{ .ctx = &c };
    c.input.text_focus = true;
    _ = Ime.onStart(null, &ime, null);
    var mb = "かな".*;
    var t = XIMText{ .length = 2, .feedback = null, .encoding_is_wchar = 0, .string = .{ .multi_byte = &mb } };
    var d = DrawStruct{ .caret = 2, .chg_first = 0, .chg_length = 0, .text = &t };
    Ime.onDraw(null, &ime, &d);
    try std.testing.expectEqualStrings("かな", c.input.preedit[0..c.input.preedit_len]);
    try std.testing.expect(ime.changed);
    // Replace the second character.
    var mb2 = "に".*;
    t = .{ .length = 1, .feedback = null, .encoding_is_wchar = 0, .string = .{ .multi_byte = &mb2 } };
    d = .{ .caret = 1, .chg_first = 1, .chg_length = 1, .text = &t };
    Ime.onDraw(null, &ime, &d);
    try std.testing.expectEqualStrings("かに", c.input.preedit[0..c.input.preedit_len]);
    try std.testing.expectEqual(@as(u16, 3), c.input.preedit_caret);
    // A deletion.
    d = .{ .caret = 0, .chg_first = 0, .chg_length = 1, .text = null };
    Ime.onDraw(null, &ime, &d);
    try std.testing.expectEqualStrings("に", c.input.preedit[0..c.input.preedit_len]);
    Ime.onDone(null, &ime, null);
    try std.testing.expectEqual(@as(u16, 0), c.input.preedit_len);
}
