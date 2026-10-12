//! Native UI: views are Zig functions of the app's state that build a tree
//! of boxes, texts and controls every frame, MyGo's `ui` package in Zig.
//!
//!     fn view(s: *Counter, c: *ui.Context) void {
//!         c.column(.{ .fill = true, .center = true, .gap = 16 });
//!         defer c.end();
//!         c.textf("{d}", .{s.n}, .{ .size = 56, .bold = true });
//!         if (c.button("Increment", .{ .primary = true })) s.n += 1;
//!     }
//!
//! The tree lives in arrays the context reuses, so a frame that builds what
//! the last one built allocates nothing. Views only run when input, a timer
//! or an animation asks for a frame; nothing polls.
//!
//! Controls: buttons, checkboxes, radio buttons, toggles, sliders, progress
//! bars, text inputs, virtualized lists, scroll views and dividers. Every
//! control but the progress bar takes the keyboard focus: Tab and Shift+Tab
//! move it, Space and Enter press, arrows step a slider.

const std = @import("std");
const geom = @import("geom.zig");
const textm = @import("text.zig");
const scene_mod = @import("scene.zig");
pub const Rect = geom.Rect;
pub const Color = geom.Color;
pub const Scene = scene_mod.Scene;

pub const Theme = struct {
    background: Color = Color.hex("#ffffff"),
    surface: Color = Color.hex("#f4f4f5"),
    surface_hover: Color = Color.hex("#e9e9ec"),
    surface_pressed: Color = Color.hex("#dddde1"),
    border: Color = Color.hex("#d9d9de"),
    border_strong: Color = Color.hex("#a1a1aa"),
    text: Color = Color.hex("#18181b"),
    text_muted: Color = Color.hex("#71717a"),
    accent: Color = Color.hex("#2563eb"),
    accent_hover: Color = Color.hex("#1d4ed8"),
    accent_pressed: Color = Color.hex("#1e40af"),
    accent_text: Color = Color.hex("#ffffff"),
    selection: Color = Color.rgba(37, 99, 235, 0.22),
    focus_ring: Color = Color.rgba(37, 99, 235, 0.45),
    danger: Color = Color.hex("#dc2626"),
    success: Color = Color.hex("#16a34a"),
    scrollbar: Color = Color.rgba(0, 0, 0, 0.32),
    radius: f32 = 6,
    font_size: f32 = 14,

    pub fn space(t: Theme, n: f32) f32 {
        _ = t;
        return 4 * n;
    }

    /// dark is the theme's dark variant.
    pub const dark = Theme{
        .background = Color.hex("#18181b"),
        .surface = Color.hex("#27272a"),
        .surface_hover = Color.hex("#303034"),
        .surface_pressed = Color.hex("#3f3f46"),
        .border = Color.hex("#3f3f46"),
        .border_strong = Color.hex("#71717a"),
        .text = Color.hex("#fafafa"),
        .text_muted = Color.hex("#a1a1aa"),
        .accent = Color.hex("#3b82f6"),
        .accent_hover = Color.hex("#2563eb"),
        .accent_pressed = Color.hex("#1d4ed8"),
        .selection = Color.rgba(59, 130, 246, 0.35),
        .focus_ring = Color.rgba(59, 130, 246, 0.6),
        .scrollbar = Color.rgba(255, 255, 255, 0.3),
    };
};

pub const Key = enum(u8) {
    none,
    up,
    down,
    left,
    right,
    enter,
    escape,
    space,
    page_up,
    page_down,
    home,
    end,
    tab,
    backspace,
    delete,
    // Letters, for shortcuts (text arrives as typed text, see typeText).
    a,
    b,
    c,
    d,
    e,
    f,
    g,
    h,
    i,
    j,
    k,
    l,
    m,
    n,
    o,
    p,
    q,
    r,
    s,
    t,
    u,
    v,
    w,
    x,
    y,
    z,

    /// letter maps 'a'..'z' (either case) to its key.
    pub fn letter(ch: u8) Key {
        const lower = std.ascii.toLower(ch);
        if (lower < 'a' or lower > 'z') return .none;
        return @enumFromInt(@intFromEnum(Key.a) + (lower - 'a'));
    }
};

pub const Mods = packed struct(u8) {
    shift: bool = false,
    ctrl: bool = false,
    alt: bool = false,
    super: bool = false,
    _: u4 = 0,
};

pub const KeyEvent = struct { key: Key, mods: Mods = .{}, used: bool = false };

pub const Align = enum(u2) { start, center, end, stretch };
pub const Justify = enum(u2) { start, center, end, between };

/// Box styles a row or a column.
pub const Box = struct {
    gap: f32 = 0,
    padding: f32 = 0,
    padding_x: ?f32 = null,
    padding_y: ?f32 = null,
    width: ?f32 = null,
    height: ?f32 = null,
    /// fill takes the space the parent has, on both axes.
    fill: bool = false,
    grow: f32 = 0,
    /// center centers the children on both axes.
    center: bool = false,
    @"align": Align = .start,
    justify: Justify = .start,
    background: Color = .transparent,
    radius: f32 = 0,
    border: f32 = 0,
    border_color: Color = .transparent,
    clip: bool = false,
    key: ?[]const u8 = null,
};

pub const TextOpts = struct {
    size: ?f32 = null,
    bold: bool = false,
    color: ?Color = null,
    width: ?f32 = null,
    @"align": Align = .start,
    grow: f32 = 0,
};

pub const ButtonOpts = struct {
    primary: bool = false,
    disabled: bool = false,
    width: ?f32 = null,
    key: ?[]const u8 = null,
};

/// CheckOpts are a checkbox's, a radio button's or a toggle's.
pub const CheckOpts = struct {
    disabled: bool = false,
    key: ?[]const u8 = null,
};

pub const SliderOpts = struct {
    width: ?f32 = null,
    /// step rounds the value to multiples of it (from min), and is what an
    /// arrow key moves it by; 0 steps by a hundredth of the range.
    step: f32 = 0,
    disabled: bool = false,
    key: ?[]const u8 = null,
};

pub const InputOpts = struct {
    placeholder: []const u8 = "",
    width: ?f32 = null,
    grow: f32 = 0,
    /// password shows a dot for each character.
    password: bool = false,
    disabled: bool = false,
    key: ?[]const u8 = null,
};

/// ListState is what a virtualized list, or a scroll view, keeps between
/// frames.
pub const ListState = struct {
    offset: f32 = 0,
    viewport: f32 = 0,
    content: f32 = 0,

    pub fn maxOffset(s: *const ListState) f32 {
        return @max(0, s.content - s.viewport);
    }

    pub fn scrollBy(s: *ListState, dy: f32) void {
        s.offset = std.math.clamp(s.offset + dy, 0, s.maxOffset());
    }
};

pub const ScrollState = ListState;

/// TextInput is a text input's text and where its caret and selection
/// are. Its text is allocated with the context's allocator: deinit frees it.
pub const TextInput = struct {
    text: std.ArrayListUnmanaged(u8) = .empty,
    /// caret and anchor are byte offsets; the selection is between them.
    caret: u32 = 0,
    anchor: u32 = 0,
    /// scroll is how far the text is scrolled left, in logical pixels.
    scroll: f32 = 0,
    /// submitted is set by Enter, until the next frame.
    submitted: bool = false,
    blink_from: u64 = 0,

    pub fn deinit(t: *TextInput, gpa: std.mem.Allocator) void {
        t.text.deinit(gpa);
    }

    pub fn value(t: *const TextInput) []const u8 {
        return t.text.items;
    }

    pub fn set(t: *TextInput, gpa: std.mem.Allocator, s: []const u8) !void {
        t.text.clearRetainingCapacity();
        try t.text.appendSlice(gpa, s);
        t.caret = @intCast(s.len);
        t.anchor = t.caret;
    }

    pub fn selection(t: *const TextInput) [2]u32 {
        return .{ @min(t.caret, t.anchor), @max(t.caret, t.anchor) };
    }

    /// replace puts s where the selection is, the caret after it.
    pub fn replace(t: *TextInput, gpa: std.mem.Allocator, s: []const u8) !void {
        const sel = t.selection();
        try t.text.replaceRange(gpa, sel[0], sel[1] - sel[0], s);
        t.caret = sel[0] + @as(u32, @intCast(s.len));
        t.anchor = t.caret;
    }

    fn prev(t: *const TextInput, i: u32) u32 {
        var j = i;
        while (j > 0) {
            j -= 1;
            if (t.text.items[j] & 0xc0 != 0x80) break;
        }
        return j;
    }

    fn next(t: *const TextInput, i: u32) u32 {
        var j = i;
        const n: u32 = @intCast(t.text.items.len);
        if (j < n) j += 1;
        while (j < n and t.text.items[j] & 0xc0 == 0x80) j += 1;
        return j;
    }

    /// word boundaries, for Ctrl with the arrow keys and Backspace.
    fn prevWord(t: *const TextInput, i: u32) u32 {
        var j = i;
        while (j > 0 and t.text.items[j - 1] == ' ') j -= 1;
        while (j > 0 and t.text.items[j - 1] != ' ') j -= 1;
        return j;
    }

    fn nextWord(t: *const TextInput, i: u32) u32 {
        var j = i;
        const n: u32 = @intCast(t.text.items.len);
        while (j < n and t.text.items[j] == ' ') j += 1;
        while (j < n and t.text.items[j] != ' ') j += 1;
        return j;
    }
};

/// Clipboard is the platform's clipboard; without one the context keeps
/// its own.
pub const Clipboard = struct {
    ctx: *anyopaque,
    /// get returns the clipboard's text, allocated with gpa, or null.
    get: *const fn (ctx: *anyopaque, gpa: std.mem.Allocator) ?[]u8,
    set: *const fn (ctx: *anyopaque, s: []const u8) void,
};

const none: u32 = std.math.maxInt(u32);

const Kind = enum(u8) { box, text, button, list, scroll, checkbox, radio, toggle, slider, progress, input, divider };

const Flags = packed struct(u8) {
    hot: bool = false,
    pressed: bool = false,
    focused: bool = false,
    on: bool = false,
    disabled: bool = false,
    primary: bool = false,
    caret: bool = false,
    password: bool = false,
};

const Node = struct {
    kind: Kind,
    dir: enum(u1) { row, column } = .column,
    flags: Flags = .{},
    box: Box = .{},
    parent: u32 = none,
    first: u32 = none,
    last: u32 = none,
    next: u32 = none,
    children: u32 = 0,
    id: u64 = 0,
    // text, buttons and labels
    str_off: u32 = 0,
    str_len: u32 = 0,
    size: f32 = 0,
    weight: textm.Weight = .regular,
    color: Color = .transparent,
    text_align: Align = .start,
    // lists and scroll views
    list: ?*ListState = null,
    first_index: u32 = 0,
    row_h: f32 = 0,
    // sliders and progress bars (0..1); a scroll view's content height
    value: f32 = 0,
    input: ?*TextInput = null,
    // layout
    iw: f32 = 0,
    ih: f32 = 0,
    rect: Rect = .{},
};

const Hit = struct { id: u64, rect: Rect, list: ?*ListState = null, focusable: bool = false, cursor: Cursor = .arrow };

/// Cursor is the pointer's shape over what it is over.
pub const Cursor = enum(u8) { arrow, text, pointer };

pub const Input = struct {
    x: f32 = -1,
    y: f32 = -1,
    hot: u64 = 0,
    active: u64 = 0,
    clicked: u64 = 0,
    /// pressed is the control the pointer went down on since the last frame.
    pressed: u64 = 0,
    /// focus is the control that takes the keyboard.
    focus: u64 = 0,
    /// focus_visible is set when the keyboard moved the focus: controls
    /// draw a ring then, not after a click.
    focus_visible: bool = false,
    /// text_focus is set while a text input has the focus: it takes the
    /// keys that edit text before shortcuts do.
    text_focus: bool = false,
    mods: Mods = .{},
    keys: [16]KeyEvent = undefined,
    nkeys: u8 = 0,
    /// chars is the text typed since the last frame, UTF-8.
    chars: [64]u8 = undefined,
    nchars: u8 = 0,
};

const blink_ns: u64 = 530 * std.time.ns_per_ms;

pub const Context = struct {
    gpa: std.mem.Allocator,
    fonts: *textm.Fonts,
    theme: Theme = .{},
    width: f32 = 0,
    height: f32 = 0,
    nodes: std.ArrayListUnmanaged(Node) = .empty,
    stack: std.ArrayListUnmanaged(u32) = .empty,
    strings: std.ArrayListUnmanaged(u8) = .empty,
    hits: std.ArrayListUnmanaged(Hit) = .empty,
    next_hits: std.ArrayListUnmanaged(Hit) = .empty,
    texts: std.ArrayListUnmanaged(TextSpan) = .empty,
    input: Input = .{},
    animating: bool = false,
    requested: bool = true,
    /// wake_at, when not 0, is when the view wants its next frame (a
    /// caret's blink, a timer), in the frame clock's ns.
    wake_at: u64 = 0,
    /// acted is set when the view acted on input, which may have changed
    /// what it shows: the frame is built again before it paints.
    acted: bool = false,
    /// frame_time is the monotonic time of the frame being built, in ns.
    frame_time: u64 = 0,
    frames: u64 = 0,
    clipboard: ?Clipboard = null,
    own_clipboard: std.ArrayListUnmanaged(u8) = .empty,
    /// paint_clip is the clip of what is being painted, for hit rects.
    paint_clip: Rect = .{},
    /// cursor is the shape the window gives the pointer, after a frame.
    cursor: Cursor = .arrow,

    const TextSpan = struct { off: u32, len: u32, rect: Rect, id: u64 };

    pub fn init(gpa: std.mem.Allocator, fonts: *textm.Fonts) Context {
        return .{ .gpa = gpa, .fonts = fonts };
    }

    pub fn deinit(c: *Context) void {
        c.nodes.deinit(c.gpa);
        c.stack.deinit(c.gpa);
        c.strings.deinit(c.gpa);
        c.hits.deinit(c.gpa);
        c.next_hits.deinit(c.gpa);
        c.texts.deinit(c.gpa);
        c.own_clipboard.deinit(c.gpa);
    }

    // ---- building ----

    /// animationFrame asks for another frame as soon as the display can show
    /// one, for views that change every frame.
    pub fn animationFrame(c: *Context) void {
        c.animating = true;
    }

    /// wakeAt asks for a frame at t (in frame_time's clock), or sooner.
    pub fn wakeAt(c: *Context, t: u64) void {
        if (c.wake_at == 0 or t < c.wake_at) c.wake_at = t;
    }

    /// shortcut reports whether key was pressed since the last frame, with
    /// any modifiers; a key a focused text input edits with is its own.
    pub fn shortcut(c: *Context, key: Key) bool {
        return c.takeKey(key, null);
    }

    /// shortcutMods is shortcut with exactly the modifiers mods.
    pub fn shortcutMods(c: *Context, key: Key, mods: Mods) bool {
        return c.takeKey(key, mods);
    }

    fn takeKey(c: *Context, key: Key, mods: ?Mods) bool {
        if (c.input.text_focus and editsText(key, mods orelse .{})) return false;
        for (c.input.keys[0..c.input.nkeys]) |*k| {
            if (k.used or k.key != key) continue;
            if (mods) |m| if (@as(u8, @bitCast(m)) != @as(u8, @bitCast(k.mods))) continue;
            k.used = true;
            c.acted = true;
            return true;
        }
        return false;
    }

    fn editsText(key: Key, mods: Mods) bool {
        return switch (key) {
            .left, .right, .home, .end, .backspace, .delete, .space, .enter => true,
            else => @intFromEnum(key) >= @intFromEnum(Key.a) and !mods.ctrl and !mods.alt and !mods.super,
        };
    }

    pub fn column(c: *Context, b: Box) void {
        c.open(.column, b);
    }

    pub fn row(c: *Context, b: Box) void {
        c.open(.row, b);
    }

    pub fn end(c: *Context) void {
        _ = c.stack.pop();
    }

    /// spacer takes the free space of its row or column.
    pub fn spacer(c: *Context) void {
        c.row(.{ .grow = 1 });
        c.end();
    }

    /// divider is a line across its column (or down its row).
    pub fn divider(c: *Context) void {
        _ = c.add(.{ .kind = .divider, .color = c.theme.border }, null);
    }

    fn open(c: *Context, dir: @TypeOf(@as(Node, undefined).dir), b: Box) void {
        var n = Node{ .kind = .box, .dir = dir, .box = b };
        if (b.center) {
            n.box.@"align" = .center;
            n.box.justify = .center;
        }
        const i = c.add(n, b.key);
        c.stack.append(c.gpa, i) catch {};
    }

    fn add(c: *Context, n: Node, key: ?[]const u8) u32 {
        const i: u32 = @intCast(c.nodes.items.len);
        var node = n;
        const p = if (c.stack.items.len > 0) c.stack.items[c.stack.items.len - 1] else none;
        node.parent = p;
        var h = std.hash.Wyhash.init(if (p != none) c.nodes.items[p].id else 0x6d797a6967);
        if (key) |k| {
            h.update(k);
        } else {
            const idx: u32 = if (p != none) c.nodes.items[p].children else 0;
            h.update(std.mem.asBytes(&idx));
        }
        node.id = h.final();
        c.nodes.append(c.gpa, node) catch return none;
        if (p != none) {
            const pn = &c.nodes.items[p];
            if (pn.last == none) pn.first = i else c.nodes.items[pn.last].next = i;
            pn.last = i;
            pn.children += 1;
        }
        return i;
    }

    fn str(c: *Context, s: []const u8) struct { u32, u32 } {
        const off: u32 = @intCast(c.strings.items.len);
        c.strings.appendSlice(c.gpa, s) catch return .{ 0, 0 };
        return .{ off, @intCast(s.len) };
    }

    pub fn text(c: *Context, s: []const u8, o: TextOpts) void {
        const off, const len = c.str(s);
        c.textNode(off, len, o);
    }

    pub fn textf(c: *Context, comptime fmt: []const u8, args: anytype, o: TextOpts) void {
        const off: u32 = @intCast(c.strings.items.len);
        c.strings.print(c.gpa, fmt, args) catch return;
        c.textNode(off, @intCast(c.strings.items.len - off), o);
    }

    fn textNode(c: *Context, off: u32, len: u32, o: TextOpts) void {
        _ = c.add(.{
            .kind = .text,
            .str_off = off,
            .str_len = len,
            .size = o.size orelse c.theme.font_size,
            .weight = if (o.bold) .bold else .regular,
            .color = o.color orelse c.theme.text,
            .text_align = o.@"align",
            .box = .{ .width = o.width, .grow = o.grow },
        }, null);
    }

    /// Interaction is what the pointer and the keyboard did to a control.
    const Interaction = struct { hot: bool, pressed: bool, focused: bool, clicked: bool };

    fn interact(c: *Context, id: u64, disabled: bool) Interaction {
        return c.interactKeys(id, disabled, true);
    }

    /// interactKeys is interact; press_keys says whether Space and Enter
    /// press the control (a text input takes them as text).
    fn interactKeys(c: *Context, id: u64, disabled: bool, press_keys: bool) Interaction {
        if (disabled) {
            if (c.input.focus == id) c.input.focus = 0;
            return .{ .hot = false, .pressed = false, .focused = false, .clicked = false };
        }
        const hot = c.input.hot == id;
        const focused = c.input.focus == id;
        var clicked = false;
        if (c.input.clicked == id) {
            c.input.clicked = 0;
            clicked = true;
        }
        // Space or Enter press the focused control.
        if (focused and press_keys) for (c.input.keys[0..c.input.nkeys]) |*k| {
            if (!k.used and (k.key == .space or k.key == .enter) and !k.mods.ctrl) {
                k.used = true;
                clicked = true;
            }
        };
        if (clicked) c.acted = true;
        return .{ .hot = hot, .pressed = hot and c.input.active == id, .focused = focused, .clicked = clicked };
    }

    /// button shows a push button and reports whether it was clicked since
    /// the last frame.
    pub fn button(c: *Context, label: []const u8, o: ButtonOpts) bool {
        const t = c.theme;
        const off, const len = c.str(label);
        const i = c.add(.{
            .kind = .button,
            .str_off = off,
            .str_len = len,
            .size = t.font_size,
            .weight = .regular,
            .box = .{ .width = o.width, .padding_x = t.space(3.5), .padding_y = t.space(1.5), .radius = t.radius },
        }, o.key orelse label);
        if (i == none) return false;
        const n = &c.nodes.items[i];
        const in = c.interact(n.id, o.disabled);
        n.flags = .{ .hot = in.hot, .pressed = in.pressed, .focused = in.focused, .disabled = o.disabled, .primary = o.primary };
        if (o.primary) {
            n.box.background = if (in.pressed) t.accent_pressed else if (in.hot) t.accent_hover else t.accent;
            n.color = t.accent_text;
        } else {
            n.box.background = if (in.pressed) t.surface_pressed else if (in.hot) t.surface_hover else t.surface;
            n.box.border = 1;
            n.box.border_color = t.border;
            n.color = t.text;
        }
        if (o.disabled) {
            n.box.background = n.box.background.withAlpha(0.5);
            n.color = n.color.withAlpha(0.45);
            n.box.border_color = n.box.border_color.withAlpha(0.5);
        }
        return in.clicked;
    }

    fn checkLike(c: *Context, kind: Kind, label: []const u8, on: bool, o: CheckOpts) Interaction {
        const off, const len = c.str(label);
        const i = c.add(.{ .kind = kind, .str_off = off, .str_len = len, .size = c.theme.font_size, .color = c.theme.text }, o.key orelse label);
        if (i == none) return .{ .hot = false, .pressed = false, .focused = false, .clicked = false };
        const n = &c.nodes.items[i];
        const in = c.interact(n.id, o.disabled);
        n.flags = .{ .hot = in.hot, .pressed = in.pressed, .focused = in.focused, .disabled = o.disabled, .on = on != in.clicked };
        return in;
    }

    /// checkbox shows a checkbox for checked, which a click flips; it
    /// reports whether it did.
    pub fn checkbox(c: *Context, label: []const u8, checked: *bool, o: CheckOpts) bool {
        const in = c.checkLike(.checkbox, label, checked.*, o);
        if (in.clicked) checked.* = !checked.*;
        return in.clicked;
    }

    /// toggle is a checkbox drawn as a switch.
    pub fn toggle(c: *Context, label: []const u8, on: *bool, o: CheckOpts) bool {
        const in = c.checkLike(.toggle, label, on.*, o);
        if (in.clicked) on.* = !on.*;
        return in.clicked;
    }

    /// radio shows a radio button, on when selected, and reports whether
    /// it was clicked: the caller selects it then.
    ///
    ///     if (c.radio("Small", s.size == .small, .{})) s.size = .small;
    pub fn radio(c: *Context, label: []const u8, selected: bool, o: CheckOpts) bool {
        const in = c.checkLike(.radio, label, selected, o);
        const n = &c.nodes.items[c.nodes.items.len - 1];
        n.flags.on = selected or in.clicked;
        return in.clicked and !selected;
    }

    /// slider sets value between min and max with the pointer (dragging
    /// the knob, or pressing the track) or the arrow keys, and reports
    /// whether it changed it.
    pub fn slider(c: *Context, value: *f32, min: f32, max: f32, o: SliderOpts) bool {
        const i = c.add(.{ .kind = .slider, .box = .{ .width = o.width } }, o.key orelse "slider");
        if (i == none) return false;
        const id = c.nodes.items[i].id;
        const in = c.interact(id, o.disabled);
        const before = value.*;
        const range = max - min;
        const step = if (o.step > 0) o.step else range / 100;
        if (!o.disabled and range > 0) {
            if (c.input.active == id) if (c.lastRect(id)) |r| {
                const knob = 8;
                const f = std.math.clamp((c.input.x - r.x - knob) / @max(1, r.w - 2 * knob), 0, 1);
                value.* = min + f * range;
                if (o.step > 0) value.* = min + @round((value.* - min) / step) * step;
            };
            if (in.focused) for (c.input.keys[0..c.input.nkeys]) |*k| {
                if (k.used) continue;
                const d: f32 = switch (k.key) {
                    .left, .down => -step,
                    .right, .up => step,
                    .page_down => -step * 10,
                    .page_up => step * 10,
                    .home => -range,
                    .end => range,
                    else => continue,
                };
                k.used = true;
                value.* += d;
            };
            value.* = std.math.clamp(value.*, @min(min, max), @max(min, max));
        }
        const n = &c.nodes.items[i];
        n.flags = .{ .hot = in.hot, .pressed = c.input.active == id, .focused = in.focused, .disabled = o.disabled };
        n.value = if (range != 0) std.math.clamp((value.* - min) / range, 0, 1) else 0;
        if (value.* != before) c.acted = true;
        return value.* != before;
    }

    /// progress shows a bar filled to value (0..1).
    pub fn progress(c: *Context, value: f32, o: struct { width: ?f32 = null }) void {
        _ = c.add(.{ .kind = .progress, .value = std.math.clamp(value, 0, 1), .box = .{ .width = o.width } }, null);
    }

    /// textInput edits st's text: typing, Backspace and Delete, the arrows,
    /// Home and End (with Shift to select, Ctrl by words), Ctrl+A, Ctrl+C,
    /// Ctrl+X and Ctrl+V, and the pointer to place the caret and select.
    /// It reports whether the text changed; st.submitted is set by Enter.
    pub fn textInput(c: *Context, st: *TextInput, o: InputOpts) bool {
        const t = c.theme;
        const off, const len = c.str(o.placeholder);
        const i = c.add(.{
            .kind = .input,
            .str_off = off,
            .str_len = len,
            .size = t.font_size,
            .input = st,
            .box = .{ .width = o.width, .grow = o.grow, .radius = t.radius, .padding_x = t.space(2.5) },
        }, o.key orelse "input");
        if (i == none) return false;
        const id = c.nodes.items[i].id;
        const in = c.interactKeys(id, o.disabled, false);
        st.submitted = false;
        st.caret = @min(st.caret, @as(u32, @intCast(st.text.items.len)));
        st.anchor = @min(st.anchor, @as(u32, @intCast(st.text.items.len)));
        var changed = false;
        var moved = false;
        const pad = t.space(2.5);
        const display = if (o.password) c.dots(st.text.items) else st.text.items;
        // The pointer places the caret, and selects as it drags.
        if (!o.disabled) if (c.lastRect(id)) |r| {
            if (c.input.pressed == id or c.input.active == id) {
                const x = c.input.x - (r.x + pad) + st.scroll;
                var at: u32 = @intCast(c.fonts.indexAt(.regular, t.font_size, display, x));
                if (o.password) at = byteOfChar(st.text.items, at / 3);
                st.caret = at;
                if (c.input.pressed == id and !c.input.mods.shift) st.anchor = at;
                if (c.input.pressed == id) c.input.pressed = 0;
                moved = true;
            }
        };
        if (in.focused and !o.disabled) {
            // Typed text goes first, then the keys, in the order they came:
            // the platforms deliver a key's text after the key.
            for (c.input.keys[0..c.input.nkeys]) |*k| {
                if (k.used) continue;
                const shift = k.mods.shift;
                const word = k.mods.ctrl or k.mods.alt;
                switch (k.key) {
                    .left => {
                        const sel = st.selection();
                        st.caret = if (!shift and sel[0] != sel[1]) sel[0] else if (word) st.prevWord(st.caret) else st.prev(st.caret);
                        if (!shift) st.anchor = st.caret;
                    },
                    .right => {
                        const sel = st.selection();
                        st.caret = if (!shift and sel[0] != sel[1]) sel[1] else if (word) st.nextWord(st.caret) else st.next(st.caret);
                        if (!shift) st.anchor = st.caret;
                    },
                    .home, .up => {
                        st.caret = 0;
                        if (!shift) st.anchor = 0;
                    },
                    .end, .down => {
                        st.caret = @intCast(st.text.items.len);
                        if (!shift) st.anchor = st.caret;
                    },
                    .backspace => {
                        if (st.caret == st.anchor) st.anchor = if (word) st.prevWord(st.caret) else st.prev(st.caret);
                        if (st.caret != st.anchor) {
                            st.replace(c.gpa, "") catch {};
                            changed = true;
                        }
                    },
                    .delete => {
                        if (st.caret == st.anchor) st.anchor = if (word) st.nextWord(st.caret) else st.next(st.caret);
                        if (st.caret != st.anchor) {
                            st.replace(c.gpa, "") catch {};
                            changed = true;
                        }
                    },
                    .enter => st.submitted = true,
                    .a => if (k.mods.ctrl) {
                        st.anchor = 0;
                        st.caret = @intCast(st.text.items.len);
                    } else continue,
                    .c, .x => if (k.mods.ctrl) {
                        const sel = st.selection();
                        if (sel[0] != sel[1] and !o.password) c.setClipboard(st.text.items[sel[0]..sel[1]]);
                        if (k.key == .x and sel[0] != sel[1]) {
                            st.replace(c.gpa, "") catch {};
                            changed = true;
                        }
                    } else continue,
                    .v => if (k.mods.ctrl) {
                        if (c.getClipboard()) |s| {
                            defer c.gpa.free(s);
                            // One line: newlines become spaces.
                            for (s) |*ch| if (ch.* == '\n' or ch.* == '\r' or ch.* == '\t') {
                                ch.* = ' ';
                            };
                            st.replace(c.gpa, s) catch {};
                            changed = true;
                        }
                    } else continue,
                    .space => {}, // its text comes as typed text
                    else => continue,
                }
                k.used = true;
                moved = true;
            }
            if (c.input.nchars > 0) {
                st.replace(c.gpa, c.input.chars[0..c.input.nchars]) catch {};
                c.input.nchars = 0;
                changed = true;
            }
        }
        if (changed or moved) {
            st.blink_from = c.frame_time;
            c.acted = true;
        }
        // Keep the caret in view.
        if (c.lastRect(id)) |r| {
            const shown = if (o.password) c.dots(st.text.items) else st.text.items;
            const caret_at = if (o.password) charCount(st.text.items[0..st.caret]) * 3 else st.caret;
            const cx = c.fonts.prefixWidth(.regular, t.font_size, shown, caret_at);
            const inner = @max(0, r.w - 2 * pad - 2);
            if (cx - st.scroll > inner) st.scroll = cx - inner;
            if (cx - st.scroll < 0) st.scroll = cx;
            const total = c.fonts.measure(.regular, t.font_size, shown);
            st.scroll = std.math.clamp(st.scroll, 0, @max(0, total - inner));
        }
        // The caret blinks while the input has the focus.
        var caret = false;
        if (in.focused) {
            const since = c.frame_time -| st.blink_from;
            caret = (since / blink_ns) % 2 == 0;
            c.wakeAt(st.blink_from + (since / blink_ns + 1) * blink_ns);
        }
        const n = &c.nodes.items[i];
        n.flags = .{ .hot = in.hot, .focused = in.focused, .disabled = o.disabled, .caret = caret, .password = o.password };
        return changed;
    }

    /// dots is a password's text shown: a dot (U+2022, 3 bytes) a
    /// character, in the frame's strings.
    fn dots(c: *Context, s: []const u8) []const u8 {
        const off = c.strings.items.len;
        var it = textm.Utf8Iter{ .s = s };
        while (it.next()) |_| c.strings.appendSlice(c.gpa, "\u{2022}") catch break;
        return c.strings.items[off..];
    }

    fn charCount(s: []const u8) usize {
        var n: usize = 0;
        for (s) |b| {
            if (b & 0xc0 != 0x80) n += 1;
        }
        return n;
    }

    fn byteOfChar(s: []const u8, ch: usize) u32 {
        var n: usize = 0;
        for (s, 0..) |b, i| if (b & 0xc0 != 0x80) {
            if (n == ch) return @intCast(i);
            n += 1;
        };
        return @intCast(s.len);
    }

    fn setClipboard(c: *Context, s: []const u8) void {
        if (c.clipboard) |cb| return cb.set(cb.ctx, s);
        c.own_clipboard.clearRetainingCapacity();
        c.own_clipboard.appendSlice(c.gpa, s) catch {};
    }

    fn getClipboard(c: *Context) ?[]u8 {
        if (c.clipboard) |cb| return cb.get(cb.ctx, c.gpa);
        if (c.own_clipboard.items.len == 0) return null;
        return c.gpa.dupe(u8, c.own_clipboard.items) catch null;
    }

    /// lastRect is where the control id was in the last frame.
    fn lastRect(c: *const Context, id: u64) ?Rect {
        for (c.hits.items) |h| if (h.id == id) return h.rect;
        return null;
    }

    /// List is a virtualized list of count rows of row_h each: next gives
    /// the index of each row in view, whose content the caller builds.
    pub const List = struct {
        c: *Context,
        i: u32,
        end: u32,
        node: u32,
        open: bool = false,

        pub fn next(l: *List) ?u32 {
            if (l.open) l.c.end();
            l.open = false;
            if (l.i >= l.end) {
                l.c.end(); // the list itself
                return null;
            }
            const i = l.i;
            l.i += 1;
            var h: u32 = i;
            _ = &h;
            l.c.row(.{ .@"align" = .center, .padding_x = 12, .gap = 12 });
            // Rows are identified by index, not position in view.
            const rn = &l.c.nodes.items[l.c.nodes.items.len - 1];
            rn.id = std.hash.Wyhash.hash(l.c.nodes.items[l.node].id, std.mem.asBytes(&h));
            l.open = true;
            return i;
        }
    };

    pub fn list(c: *Context, state: *ListState, count: u32, row_h: f32, b: Box) List {
        var bb = b;
        bb.clip = true;
        const i = c.add(.{ .kind = .list, .box = bb, .list = state, .row_h = row_h }, b.key);
        c.stack.append(c.gpa, i) catch {};
        state.content = @as(f32, @floatFromInt(count)) * row_h;
        state.offset = std.math.clamp(state.offset, 0, state.maxOffset());
        const vp = if (state.viewport > 0) state.viewport else c.height;
        const first: u32 = @intFromFloat(@floor(state.offset / row_h));
        const last: u32 = @min(count, @as(u32, @intFromFloat(@ceil((state.offset + vp) / row_h))) + 1);
        c.nodes.items[i].first_index = first;
        return .{ .c = c, .i = @min(first, count), .end = last, .node = i };
    }

    /// scrollView is a column whose content scrolls with the wheel when
    /// it is taller than the view: end it with end(). Unlike a list, all
    /// of its content is built.
    pub fn scrollView(c: *Context, state: *ScrollState, b: Box) void {
        var bb = b;
        bb.clip = true;
        const i = c.add(.{ .kind = .scroll, .dir = .column, .box = bb, .list = state }, b.key);
        c.stack.append(c.gpa, i) catch {};
    }

    // ---- frames ----

    /// build runs view for a frame of width×height, twice when it acted on
    /// input, so that a click's effect shows in the frame that reports it.
    pub fn build(c: *Context, state: anytype, view: anytype, width: f32, height: f32, now: u64) void {
        c.acted = false;
        c.moveFocus();
        c.beginFrame(width, height, now);
        view(state, c);
        if (c.acted) {
            c.input.nkeys = 0;
            c.input.nchars = 0;
            c.input.clicked = 0;
            c.input.pressed = 0;
            c.acted = false;
            c.beginFrame(width, height, now);
            c.frames -= 1;
            view(state, c);
        }
    }

    /// moveFocus moves the focus for Tab (Shift+Tab: back), through the
    /// controls of the last frame in order.
    fn moveFocus(c: *Context) void {
        for (c.input.keys[0..c.input.nkeys]) |*k| {
            if (k.used or k.key != .tab) continue;
            k.used = true;
            const items = c.hits.items;
            var at: ?usize = null;
            for (items, 0..) |h, idx| if (h.focusable and h.id == c.input.focus) {
                at = idx;
            };
            const n = items.len;
            if (n == 0) return;
            var idx: usize = if (at) |a| a else if (k.mods.shift) 0 else n - 1;
            var steps: usize = 0;
            while (steps < n) : (steps += 1) {
                idx = if (k.mods.shift) (idx + n - 1) % n else (idx + 1) % n;
                if (items[idx].focusable) {
                    c.input.focus = items[idx].id;
                    c.input.focus_visible = true;
                    break;
                }
            }
        }
    }

    pub fn beginFrame(c: *Context, width: f32, height: f32, now: u64) void {
        c.width = width;
        c.height = height;
        c.nodes.clearRetainingCapacity();
        c.stack.clearRetainingCapacity();
        c.strings.clearRetainingCapacity();
        c.animating = false;
        c.requested = false;
        c.wake_at = 0;
        c.frame_time = now;
        c.frames += 1;
        // The root fills the window.
        _ = c.add(.{ .kind = .box, .box = .{ .width = width, .height = height } }, null);
        c.stack.append(c.gpa, 0) catch {};
    }

    /// endFrame lays the tree out and paints it into s.
    pub fn endFrame(c: *Context, s: *Scene) void {
        c.input.nkeys = 0;
        c.input.nchars = 0;
        c.input.clicked = 0;
        c.input.pressed = 0;
        if (c.nodes.items.len == 0) return;
        _ = c.measure(0);
        c.arrange(0, .{ .x = 0, .y = 0, .w = c.width, .h = c.height });
        c.next_hits.clearRetainingCapacity();
        c.texts.clearRetainingCapacity();
        s.background = c.theme.background;
        c.paint_clip = .{ .x = 0, .y = 0, .w = c.width, .h = c.height };
        c.paint(0, s);
        std.mem.swap(std.ArrayListUnmanaged(Hit), &c.hits, &c.next_hits);
        // The focus is lost with its control.
        if (c.input.focus != 0 and c.lastRect(c.input.focus) == null) c.input.focus = 0;
        c.input.text_focus = false;
        if (c.input.focus != 0) for (c.nodes.items) |n| if (n.id == c.input.focus and n.kind == .input) {
            c.input.text_focus = true;
        };
        // The pointer may be over another control now.
        const h = c.hitTest(c.input.x, c.input.y);
        if (h != c.input.hot) {
            c.input.hot = h;
            c.requested = true;
        }
        const over = if (c.input.active != 0) c.input.active else h;
        c.cursor = .arrow;
        for (c.hits.items) |hit| if (hit.id == over) {
            c.cursor = hit.cursor;
        };
    }

    fn labelWidth(c: *Context, n: *const Node) f32 {
        if (n.str_len == 0) return 0;
        return c.fonts.measure(n.weight, n.size, c.strings.items[n.str_off..][0..n.str_len]);
    }

    const check_size: f32 = 16;
    const toggle_w: f32 = 36;
    const toggle_h: f32 = 20;
    const label_gap: f32 = 8;

    fn measure(c: *Context, i: u32) [2]f32 {
        var n = &c.nodes.items[i];
        var w: f32 = 0;
        var h: f32 = 0;
        switch (n.kind) {
            .text => {
                w = c.labelWidth(n);
                h = c.fonts.metrics(n.weight, n.size).line_height;
            },
            .button => {
                w = c.labelWidth(n) + 2 * (n.box.padding_x orelse 0);
                h = c.fonts.metrics(n.weight, n.size).line_height + 2 * (n.box.padding_y orelse 0);
            },
            .checkbox, .radio, .toggle => {
                const lw = c.labelWidth(n);
                const cw: f32 = if (n.kind == .toggle) toggle_w else check_size;
                w = cw + if (lw > 0) label_gap + lw else 0;
                h = @max(if (n.kind == .toggle) toggle_h else check_size, c.fonts.metrics(.regular, n.size).line_height);
            },
            .slider => {
                w = 200;
                h = 20;
            },
            .progress => {
                w = 200;
                h = 8;
            },
            .input => {
                w = 240;
                h = c.fonts.metrics(.regular, n.size).line_height + 2 * c.theme.space(2);
            },
            .divider => {
                const p = n.parent;
                if (p != none and c.nodes.items[p].dir == .row) w = 1 else h = 1;
            },
            .list => {
                var ch = n.first;
                while (ch != none) : (ch = c.nodes.items[ch].next) _ = c.measure(ch);
            },
            .box, .scroll => {
                const px = n.box.padding_x orelse n.box.padding;
                const py = n.box.padding_y orelse n.box.padding;
                var ch = n.first;
                var k: u32 = 0;
                while (ch != none) : (ch = c.nodes.items[ch].next) {
                    const m = c.measure(ch);
                    if (n.dir == .row) {
                        w += m[0];
                        h = @max(h, m[1]);
                    } else {
                        h += m[1];
                        w = @max(w, m[0]);
                    }
                    k += 1;
                    n = &c.nodes.items[i];
                }
                const gaps = if (k > 1) n.box.gap * @as(f32, @floatFromInt(k - 1)) else 0;
                if (n.dir == .row) w += gaps else h += gaps;
                w += 2 * px;
                h += 2 * py;
                if (n.kind == .scroll) {
                    // Its content's height; its own is what it is given.
                    n.value = h;
                    h = 0;
                }
            },
        }
        n = &c.nodes.items[i];
        n.iw = n.box.width orelse w;
        n.ih = n.box.height orelse h;
        return .{ n.iw, n.ih };
    }

    fn arrange(c: *Context, i: u32, r: Rect) void {
        const n = &c.nodes.items[i];
        n.rect = r;
        switch (n.kind) {
            .box => c.arrangeChildren(i, r),
            .scroll => {
                const st = n.list.?;
                st.viewport = r.h;
                st.content = n.value;
                st.offset = std.math.clamp(st.offset, 0, st.maxOffset());
                // The content, at its own height, scrolled.
                c.arrangeChildren(i, .{ .x = r.x, .y = r.y - st.offset, .w = r.w, .h = @max(r.h, n.value) });
            },
            .list => {
                const st = n.list.?;
                st.viewport = r.h;
                st.offset = std.math.clamp(st.offset, 0, st.maxOffset());
                var ch = n.first;
                var k: f32 = @floatFromInt(n.first_index);
                while (ch != none) : (ch = c.nodes.items[ch].next) {
                    c.arrange(ch, .{ .x = r.x, .y = r.y + k * n.row_h - st.offset, .w = r.w, .h = n.row_h });
                    k += 1;
                }
            },
            else => {},
        }
    }

    fn arrangeChildren(c: *Context, i: u32, r: Rect) void {
        const n = &c.nodes.items[i];
        const box = n.box;
        const px = box.padding_x orelse box.padding;
        const py = box.padding_y orelse box.padding;
        const inner = Rect{ .x = r.x + px, .y = r.y + py, .w = @max(0, r.w - 2 * px), .h = @max(0, r.h - 2 * py) };
        const row_dir = n.dir == .row;
        const main_size = if (row_dir) inner.w else inner.h;
        const cross_size = if (row_dir) inner.h else inner.w;
        // Sizes along the main axis, and what grows.
        var used: f32 = 0;
        var grow: f32 = 0;
        var count: u32 = 0;
        var ch = n.first;
        while (ch != none) : (ch = c.nodes.items[ch].next) {
            const cn = &c.nodes.items[ch];
            used += if (row_dir) cn.iw else cn.ih;
            grow += childGrow(cn);
            count += 1;
        }
        if (count == 0) return;
        used += box.gap * @as(f32, @floatFromInt(count - 1));
        const free = main_size - used;
        var pos: f32 = 0;
        var gap = box.gap;
        if (grow == 0 and free > 0) switch (box.justify) {
            .start => {},
            .center => pos = free / 2,
            .end => pos = free,
            .between => if (count > 1) {
                gap += free / @as(f32, @floatFromInt(count - 1));
            },
        };
        ch = n.first;
        while (ch != none) : (ch = c.nodes.items[ch].next) {
            const cn = &c.nodes.items[ch];
            var m = if (row_dir) cn.iw else cn.ih;
            const g = childGrow(cn);
            if (g > 0 and free > 0) m += free * g / grow;
            var cs = if (row_dir) cn.ih else cn.iw;
            const stretch = box.@"align" == .stretch or cn.box.fill or cn.kind == .list or cn.kind == .scroll or cn.kind == .divider;
            const fixed_cross = if (row_dir) cn.box.height else cn.box.width;
            if (stretch and fixed_cross == null) cs = cross_size;
            const co: f32 = switch (box.@"align") {
                .start, .stretch => 0,
                .center => (cross_size - cs) / 2,
                .end => cross_size - cs,
            };
            const cr = if (row_dir)
                Rect{ .x = inner.x + pos, .y = inner.y + co, .w = m, .h = cs }
            else
                Rect{ .x = inner.x + co, .y = inner.y + pos, .w = cs, .h = m };
            c.arrange(ch, cr);
            pos += m + gap;
        }
    }

    fn childGrow(cn: *const Node) f32 {
        if (cn.box.grow > 0) return cn.box.grow;
        if (cn.box.fill or cn.kind == .list) return 1;
        if (cn.kind == .scroll and cn.box.height == null) return 1;
        return 0;
    }

    fn addHit(c: *Context, n: *const Node, focusable: bool) void {
        const r = intersect(n.rect, c.paint_clip);
        if (r.w <= 0 or r.h <= 0) return;
        const cursor: Cursor = if (n.flags.disabled) .arrow else if (n.kind == .input) .text else .arrow;
        c.next_hits.append(c.gpa, .{ .id = n.id, .rect = r, .focusable = focusable and !n.flags.disabled, .cursor = cursor }) catch {};
    }

    fn intersect(a: Rect, b: Rect) Rect {
        const x0 = @max(a.x, b.x);
        const y0 = @max(a.y, b.y);
        const x1 = @min(a.x + a.w, b.x + b.w);
        const y1 = @min(a.y + a.h, b.y + b.h);
        return .{ .x = x0, .y = y0, .w = @max(0, x1 - x0), .h = @max(0, y1 - y0) };
    }

    /// focusRing draws the ring of a control the keyboard focused.
    fn focusRing(c: *Context, s: *Scene, n: *const Node, r: Rect, radius: f32) void {
        if (!n.flags.focused or !c.input.focus_visible) return;
        s.rect(.{ .x = r.x - 3, .y = r.y - 3, .w = r.w + 6, .h = r.h + 6 }, .transparent, radius + 3, 2, c.theme.focus_ring);
    }

    fn paintLabel(c: *Context, s: *Scene, n: *const Node, x: f32, r: Rect) void {
        if (n.str_len == 0) return;
        const str_ = c.strings.items[n.str_off..][0..n.str_len];
        const m = c.fonts.metrics(.regular, n.size);
        const color = if (n.flags.disabled) n.color.withAlpha(0.45) else n.color;
        s.textRun(c.fonts, x, r.y + (r.h - m.line_height) / 2 + m.ascent, str_, n.size, .regular, color);
        c.texts.append(c.gpa, .{ .off = n.str_off, .len = n.str_len, .rect = r, .id = n.id }) catch {};
    }

    fn paint(c: *Context, i: u32, s: *Scene) void {
        const n = &c.nodes.items[i];
        const r = n.rect;
        const t = c.theme;
        switch (n.kind) {
            .text => {
                const str_ = c.strings.items[n.str_off..][0..n.str_len];
                const m = c.fonts.metrics(n.weight, n.size);
                const w = c.fonts.measure(n.weight, n.size, str_);
                const x = switch (n.text_align) {
                    .center => r.x + (r.w - w) / 2,
                    .end => r.x + r.w - w,
                    else => r.x,
                };
                const y = r.y + (r.h - m.line_height) / 2 + m.ascent;
                s.textRun(c.fonts, x, y, str_, n.size, n.weight, n.color);
                c.texts.append(c.gpa, .{ .off = n.str_off, .len = n.str_len, .rect = r, .id = n.id }) catch {};
            },
            .button => {
                c.focusRing(s, n, r, n.box.radius);
                s.rect(r, n.box.background, n.box.radius, n.box.border, n.box.border_color);
                const str_ = c.strings.items[n.str_off..][0..n.str_len];
                const m = c.fonts.metrics(n.weight, n.size);
                const w = c.fonts.measure(n.weight, n.size, str_);
                s.textRun(c.fonts, r.x + (r.w - w) / 2, r.y + (r.h - m.line_height) / 2 + m.ascent, str_, n.size, n.weight, n.color);
                c.addHit(n, true);
                c.texts.append(c.gpa, .{ .off = n.str_off, .len = n.str_len, .rect = r, .id = n.id }) catch {};
            },
            .checkbox, .radio => {
                const b = Rect{ .x = r.x, .y = r.y + (r.h - check_size) / 2, .w = check_size, .h = check_size };
                const radius: f32 = if (n.kind == .radio) check_size / 2 else 4;
                c.focusRing(s, n, b, radius);
                const fade: f32 = if (n.flags.disabled) 0.45 else 1;
                if (n.kind == .checkbox and n.flags.on) {
                    const fill = if (n.flags.pressed) t.accent_pressed else if (n.flags.hot) t.accent_hover else t.accent;
                    s.rect(b, fill.withAlpha(fade), radius, 0, .transparent);
                    s.stroke(.{ .x = b.x + 3.5, .y = b.y + 4, .w = 9, .h = 8 }, .{ .{ 0, 0.5, 0.36, 0.9 }, .{ 0.36, 0.9, 1, 0.05 } }, 2, t.accent_text.withAlpha(fade));
                } else {
                    const ring = if (n.flags.on) t.accent else if (n.flags.hot) t.border_strong else t.border;
                    const bg = if (n.flags.pressed) t.surface_pressed else t.background;
                    s.rect(b, bg.withAlpha(fade), radius, if (n.flags.on) 1.5 else 1, ring.withAlpha(fade));
                    if (n.flags.on) s.rect(.{ .x = b.x + 4, .y = b.y + 4, .w = 8, .h = 8 }, t.accent.withAlpha(fade), 4, 0, .transparent);
                }
                c.paintLabel(s, n, r.x + check_size + label_gap, r);
                c.addHit(n, true);
            },
            .toggle => {
                const b = Rect{ .x = r.x, .y = r.y + (r.h - toggle_h) / 2, .w = toggle_w, .h = toggle_h };
                c.focusRing(s, n, b, toggle_h / 2);
                const fade: f32 = if (n.flags.disabled) 0.45 else 1;
                const track = if (n.flags.on)
                    (if (n.flags.hot) t.accent_hover else t.accent)
                else if (n.flags.hot) t.border_strong else t.surface_pressed;
                s.rect(b, track.withAlpha(fade), toggle_h / 2, 0, .transparent);
                const kx = if (n.flags.on) b.x + toggle_w - toggle_h + 2 else b.x + 2;
                s.rect(.{ .x = kx, .y = b.y + 2, .w = toggle_h - 4, .h = toggle_h - 4 }, Color.white.withAlpha(fade), (toggle_h - 4) / 2, 0, .transparent);
                c.paintLabel(s, n, r.x + toggle_w + label_gap, r);
                c.addHit(n, true);
            },
            .slider => {
                const knob: f32 = 16;
                const fade: f32 = if (n.flags.disabled) 0.45 else 1;
                const tx = r.x + knob / 2;
                const tw = @max(0, r.w - knob);
                const ty = r.y + r.h / 2 - 2;
                const kx = tx + tw * n.value;
                s.rect(.{ .x = tx, .y = ty, .w = tw, .h = 4 }, t.surface_pressed.withAlpha(fade), 2, 0, .transparent);
                s.rect(.{ .x = tx, .y = ty, .w = kx - tx, .h = 4 }, t.accent.withAlpha(fade), 2, 0, .transparent);
                const kb = Rect{ .x = kx - knob / 2, .y = r.y + r.h / 2 - knob / 2, .w = knob, .h = knob };
                c.focusRing(s, n, kb, knob / 2);
                const ring = if (n.flags.pressed) t.accent_pressed else if (n.flags.hot) t.accent_hover else t.accent;
                s.rect(kb, Color.white.withAlpha(fade), knob / 2, 2, ring.withAlpha(fade));
                c.addHit(n, true);
            },
            .progress => {
                s.rect(r, t.surface_pressed, r.h / 2, 0, .transparent);
                if (n.value > 0) s.rect(.{ .x = r.x, .y = r.y, .w = @max(r.h, r.w * n.value), .h = r.h }, t.accent, r.h / 2, 0, .transparent);
            },
            .divider => s.rect(r, n.color, 0, 0, .transparent),
            .input => c.paintInput(n, s),
            .box, .list, .scroll => {
                s.rect(r, n.box.background, n.box.radius, n.box.border, n.box.border_color);
                const scrolls = n.kind == .list or n.kind == .scroll;
                const saved = c.paint_clip;
                if (scrolls) {
                    s.pushLayer(n.id, r, n.list.?.offset);
                } else if (n.box.clip) s.pushClip(r);
                if (scrolls or n.box.clip) c.paint_clip = intersect(saved, r);
                var ch = n.first;
                while (ch != none) : (ch = c.nodes.items[ch].next) c.paint(ch, s);
                c.paint_clip = saved;
                if (scrolls) {
                    s.popLayer();
                    s.pushClip(r);
                    const st = n.list.?;
                    c.next_hits.append(c.gpa, .{ .id = n.id, .rect = intersect(r, saved), .list = st }) catch {};
                    if (st.content > st.viewport and st.viewport > 0) {
                        const th = @max(24, r.h * st.viewport / st.content);
                        const ty = r.y + (r.h - th) * (st.offset / st.maxOffset());
                        s.rect(.{ .x = r.x + r.w - 8, .y = ty + 2, .w = 5, .h = th - 4 }, t.scrollbar, 2.5, 0, .transparent);
                    }
                }
                if (n.box.clip) s.popClip();
            },
        }
    }

    fn paintInput(c: *Context, n: *const Node, s: *Scene) void {
        const t = c.theme;
        const r = n.rect;
        const st = n.input.?;
        const fade: f32 = if (n.flags.disabled) 0.5 else 1;
        c.focusRing(s, n, r, n.box.radius);
        const ring = if (n.flags.focused) t.accent else if (n.flags.hot) t.border_strong else t.border;
        s.rect(r, t.background.withAlpha(fade), n.box.radius, if (n.flags.focused) 1.5 else 1, ring.withAlpha(fade));
        c.addHit(n, true);
        const pad = n.box.padding_x orelse 0;
        const inner = Rect{ .x = r.x + pad, .y = r.y + 1, .w = @max(0, r.w - 2 * pad), .h = @max(0, r.h - 2) };
        s.pushClip(inner);
        defer s.popClip();
        const m = c.fonts.metrics(.regular, n.size);
        const baseline = r.y + (r.h - m.line_height) / 2 + m.ascent;
        const x0 = inner.x - st.scroll;
        const shown = if (n.flags.password) c.dots(st.text.items) else st.text.items;
        const at = struct {
            fn f(nn: *const Node, ss: *const TextInput, byte: u32) usize {
                return if (nn.flags.password) charCount(ss.text.items[0..byte]) * 3 else byte;
            }
        }.f;
        if (shown.len == 0) {
            const ph = c.strings.items[n.str_off..][0..n.str_len];
            s.textRun(c.fonts, inner.x, baseline, ph, n.size, .regular, t.text_muted.withAlpha(fade));
        } else {
            const sel = st.selection();
            if (n.flags.focused and sel[0] != sel[1]) {
                const sx0 = c.fonts.prefixWidth(.regular, n.size, shown, at(n, st, sel[0]));
                const sx1 = c.fonts.prefixWidth(.regular, n.size, shown, at(n, st, sel[1]));
                s.rect(.{ .x = x0 + sx0, .y = baseline - m.ascent, .w = sx1 - sx0, .h = m.line_height }, t.selection, 0, 0, .transparent);
            }
            s.textRun(c.fonts, x0, baseline, shown, n.size, .regular, t.text.withAlpha(fade));
        }
        if (n.flags.caret) {
            const cx = c.fonts.prefixWidth(.regular, n.size, shown, at(n, st, st.caret));
            s.rect(.{ .x = x0 + cx - 0.5, .y = baseline - m.ascent, .w = 1.5, .h = m.ascent + m.descent }, t.text, 0, 0, .transparent);
        }
        c.texts.append(c.gpa, .{ .off = n.str_off, .len = n.str_len, .rect = r, .id = n.id }) catch {};
    }

    // ---- input ----

    fn hitTest(c: *const Context, x: f32, y: f32) u64 {
        var i = c.hits.items.len;
        while (i > 0) {
            i -= 1;
            const h = c.hits.items[i];
            if (h.list == null and h.rect.contains(x, y)) return h.id;
        }
        return 0;
    }

    fn hitFocusable(c: *const Context, id: u64) bool {
        for (c.hits.items) |h| if (h.id == id) return h.focusable;
        return false;
    }

    /// pointerMove reports whether the frame must be rebuilt: the pointer
    /// went over another control, or drags one.
    pub fn pointerMove(c: *Context, x: f32, y: f32) bool {
        c.input.x = x;
        c.input.y = y;
        const h = c.hitTest(x, y);
        if (h == c.input.hot) return c.input.active != 0;
        c.input.hot = h;
        return true;
    }

    pub fn pointerDown(c: *Context, x: f32, y: f32) bool {
        _ = c.pointerMove(x, y);
        c.input.active = c.input.hot;
        c.input.pressed = c.input.hot;
        const was = c.input.focus;
        c.input.focus = if (c.hitFocusable(c.input.hot)) c.input.hot else 0;
        c.input.focus_visible = false;
        return c.input.active != 0 or was != c.input.focus;
    }

    pub fn pointerUp(c: *Context, x: f32, y: f32) bool {
        _ = c.pointerMove(x, y);
        const was = c.input.active;
        c.input.active = 0;
        if (was != 0 and was == c.input.hot) c.input.clicked = was;
        return was != 0;
    }

    pub fn scroll(c: *Context, x: f32, y: f32, dy: f32) bool {
        var i = c.hits.items.len;
        while (i > 0) {
            i -= 1;
            const h = c.hits.items[i];
            if (h.list) |st| if (h.rect.contains(x, y)) {
                const before = st.offset;
                st.scrollBy(dy);
                if (st.offset != before) return true;
            };
        }
        return false;
    }

    pub fn keyDown(c: *Context, k: Key) bool {
        return c.keyDownMods(k, c.input.mods);
    }

    /// keyDownMods records a key press with its modifiers; the platforms
    /// call it, then typeText with the key's text.
    pub fn keyDownMods(c: *Context, k: Key, mods: Mods) bool {
        c.input.mods = mods;
        if (k == .none or c.input.nkeys == c.input.keys.len) return false;
        c.input.keys[c.input.nkeys] = .{ .key = k, .mods = mods };
        c.input.nkeys += 1;
        return true;
    }

    /// setMods records the modifiers held, for the pointer (Shift-click).
    pub fn setMods(c: *Context, mods: Mods) void {
        c.input.mods = mods;
    }

    /// typeText records text typed, UTF-8; control characters are not
    /// text and are dropped.
    pub fn typeText(c: *Context, s: []const u8) bool {
        var any = false;
        for (s) |b| {
            if (b < 0x20 or b == 0x7f) continue;
            if (c.input.nchars == c.input.chars.len) break;
            c.input.chars[c.input.nchars] = b;
            c.input.nchars += 1;
            any = true;
        }
        return any;
    }

    // ---- queries, for tests ----

    pub fn find(c: *const Context, label_: []const u8) ?Rect {
        for (c.texts.items) |t| {
            if (std.mem.eql(u8, c.strings.items[t.off..][0..t.len], label_)) return t.rect;
        }
        return null;
    }

    pub fn hasText(c: *const Context, s: []const u8) bool {
        return c.find(s) != null;
    }
};
