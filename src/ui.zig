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
    /// wrap breaks the text into lines at spaces (and at newlines) to fit
    /// the width it is given.
    wrap: bool = false,
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
    /// unit is what a drag selects by, from the click that began it:
    /// characters, words (a double click) or all (a triple); word is the
    /// word the double click selected.
    unit: u8 = 1,
    word: [2]u32 = .{ 0, 0 },

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

    /// wordAt is the word (or run of spaces, or of punctuation) at i, what
    /// a double click selects.
    fn wordAt(t: *const TextInput, i: u32) [2]u32 {
        const s = t.text.items;
        const n: u32 = @intCast(s.len);
        if (n == 0) return .{ 0, 0 };
        const at = if (i >= n) n - 1 else i;
        const k = class(s[at]);
        var lo = at;
        while (lo > 0 and class(s[lo - 1]) == k) lo -= 1;
        var hi = at;
        while (hi < n and class(s[hi]) == k) hi += 1;
        return .{ lo, hi };
    }

    fn class(b: u8) u2 {
        if (b == ' ' or b == '\t') return 0;
        if (std.ascii.isAlphanumeric(b) or b == '_' or b >= 0x80) return 1;
        return 2;
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

const Kind = enum(u8) { box, text, button, list, scroll, checkbox, radio, toggle, slider, progress, input, divider, overlay, select, option, menu_item };

/// Place is where an overlay goes: below its anchor (a popup, flipped
/// above when it does not fit), at the pointer (a tooltip), or centered (a
/// modal dialog).
const Place = enum(u2) { below, pointer, center, at };
const OverlayMode = enum(u2) { popup, modal, tip };

// Ids derived from a control's, for what it opens.
const popup_salt: u64 = 0x706f707570;
const blocker_salt: u64 = 0x626c6f636b;
const tip_salt: u64 = 0x746970;
const tip_delay: u64 = 600 * std.time.ns_per_ms;

const Flags = packed struct(u16) {
    hot: bool = false,
    pressed: bool = false,
    focused: bool = false,
    on: bool = false,
    disabled: bool = false,
    primary: bool = false,
    caret: bool = false,
    password: bool = false,
    wrap: bool = false,
    /// listens is set on a box that asked for presses of other buttons.
    listens: bool = false,
    _: u6 = 0,
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
    // a menu item's shortcut
    sub_off: u32 = 0,
    sub_len: u32 = 0,
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
    // overlays
    anchor: Rect = .{},
    place: Place = .below,
    mode: OverlayMode = .popup,
    // layout
    iw: f32 = 0,
    ih: f32 = 0,
    rect: Rect = .{},
};

const Hit = struct {
    id: u64,
    rect: Rect,
    list: ?*ListState = null,
    focusable: bool = false,
    cursor: Cursor = .arrow,
    /// blocks is set on what an overlay puts under itself: it takes the
    /// pointer (and the wheel) from what is under it.
    blocks: bool = false,
    /// modal is set on a modal dialog's: the focus stays above it.
    modal: bool = false,
    /// passive is set on a box's that listens for other buttons: it does
    /// not take the pointer from what is in it.
    passive: bool = false,
};

/// Cursor is the pointer's shape over what it is over.
pub const Cursor = enum(u8) { arrow, text, pointer };

/// MouseButton is a pointer button: the platforms report every one, and
/// back and forward (the side buttons) as navigation.
pub const MouseButton = enum(u3) { left, right, middle, back, forward };

pub const Point = struct { x: f32 = 0, y: f32 = 0 };

/// Nav is what a mouse's side buttons asked for.
pub const Nav = enum(u2) { back, forward };

/// Menu is a context menu's state: open it where the pointer was pressed.
///
///     if (c.pressedWith(.right)) |at| s.menu.openAt(at);
///     if (c.beginMenu(&s.menu)) {
///         defer c.endMenu();
///         if (c.menuItem("Copy", .{ .shortcut = "Ctrl+C" })) copy(s);
///     }
pub const Menu = struct {
    open: bool = false,
    at: Point = .{},

    pub fn openAt(m: *Menu, at: Point) void {
        m.open = true;
        m.at = at;
    }
};

pub const MenuItemOpts = struct {
    shortcut: []const u8 = "",
    disabled: bool = false,
    key: ?[]const u8 = null,
};

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
    /// clicks counts the presses of the left button in quick succession
    /// at one place, for the press that set pressed: 2 for a double click.
    clicks: u8 = 1,
    last_press_ns: i128 = 0,
    last_press: Point = .{ .x = -100, .y = -100 },
    /// aux is the box a button other than the left one went down on, since
    /// the last frame, and aux_at where.
    aux: u64 = 0,
    aux_button: MouseButton = .right,
    aux_at: Point = .{},
    nav: ?Nav = null,
    /// wheel_x is the horizontal scrolling since the last frame, in
    /// logical pixels, for views that scroll sideways.
    wheel_x: f32 = 0,
    /// preedit is the text an input method is composing, UTF-8, and
    /// preedit_caret where its caret is in it: the focused text input shows
    /// it at its caret, underlined, until the method commits it as typed text.
    preedit: [256]u8 = undefined,
    preedit_len: u16 = 0,
    preedit_caret: u16 = 0,
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
    overlays: std.ArrayListUnmanaged(u32) = .empty,
    /// popup is the select whose options are open, highlight the option
    /// the keyboard or pointer is on.
    popup: u64 = 0,
    highlight: usize = 0,
    /// tip_id is the control the pointer rests on, since tip_since.
    tip_id: u64 = 0,
    tip_since: u64 = 0,
    /// ime_rect is where the focused text input's caret is, after a frame,
    /// in logical pixels: the platforms put an input method's candidate
    /// window by it.
    ime_rect: ?Rect = null,
    /// menu is the open menu's id, menu_items how many items it had in the
    /// last frame, menu_index the next item's.
    menu: u64 = 0,
    menu_items: usize = 0,
    menu_index: usize = 0,
    menu_state: ?*Menu = null,

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
        c.overlays.deinit(c.gpa);
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
            .flags = .{ .wrap = o.wrap },
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
                const n_: u32 = @intCast(st.text.items.len);
                if (c.input.pressed == id) {
                    // A double click selects a word, a triple all of it;
                    // a drag from there goes by words, or keeps all.
                    st.unit = if (o.password and c.input.clicks == 2) 3 else c.input.clicks;
                    if (st.unit == 2) st.word = st.wordAt(at);
                }
                switch (st.unit) {
                    2 => {
                        const w = st.wordAt(at);
                        if (at < st.word[0]) {
                            st.anchor = st.word[1];
                            st.caret = w[0];
                        } else {
                            st.anchor = st.word[0];
                            st.caret = @max(w[1], st.word[1]);
                        }
                    },
                    3 => {
                        st.anchor = 0;
                        st.caret = n_;
                    },
                    else => {
                        st.caret = at;
                        if (c.input.pressed == id and !c.input.mods.shift) st.anchor = at;
                    },
                }
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
            const composing = in.focused and !o.password and c.input.preedit_len > 0;
            const shown = if (o.password) c.dots(st.text.items) else if (composing) c.composed(st) else st.text.items;
            const caret_at = if (o.password) charCount(st.text.items[0..st.caret]) * 3 else if (composing) st.caret + c.input.preedit_caret else st.caret;
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

    /// composed is st's text with the input method's text at its caret, in
    /// the frame's strings.
    fn composed(c: *Context, st: *const TextInput) []const u8 {
        const off = c.strings.items.len;
        const s = st.text.items;
        const at = @min(st.caret, s.len);
        c.strings.appendSlice(c.gpa, s[0..at]) catch return s;
        c.strings.appendSlice(c.gpa, c.input.preedit[0..c.input.preedit_len]) catch return s;
        c.strings.appendSlice(c.gpa, s[at..]) catch return s;
        return c.strings.items[off..];
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

    // ---- overlays ----

    fn beginOverlay(c: *Context, id: u64, anchor: Rect, place: Place, mode: OverlayMode, b: Box) void {
        const i: u32 = @intCast(c.nodes.items.len);
        c.nodes.append(c.gpa, .{ .kind = .overlay, .dir = .column, .box = b, .id = id, .anchor = anchor, .place = place, .mode = mode }) catch {
            c.stack.append(c.gpa, 0) catch {};
            return;
        };
        c.overlays.append(c.gpa, i) catch {};
        c.stack.append(c.gpa, i) catch {};
    }

    pub const SelectOpts = struct {
        width: ?f32 = null,
        placeholder: []const u8 = "Select…",
        disabled: bool = false,
        key: ?[]const u8 = null,
    };

    /// select chooses one of options into selected (an index; out of range
    /// for none) from a popup list: a click opens it, the arrows move in
    /// it, Enter or a click chooses, Escape or a click outside closes it.
    /// Closed, the arrows step through the options. It reports whether
    /// selected changed.
    pub fn select(c: *Context, options: []const []const u8, selected: *usize, o: SelectOpts) bool {
        const t = c.theme;
        const has = selected.* < options.len;
        const off, const len = c.str(if (has) options[selected.*] else o.placeholder);
        var widest = c.fonts.measure(.regular, t.font_size, o.placeholder);
        for (options) |opt| widest = @max(widest, c.fonts.measure(.regular, t.font_size, opt));
        const i = c.add(.{
            .kind = .select,
            .str_off = off,
            .str_len = len,
            .size = t.font_size,
            .color = if (has) t.text else t.text_muted,
            .value = widest,
            .box = .{ .width = o.width, .radius = t.radius, .padding_x = t.space(2.5) },
        }, o.key orelse "select");
        if (i == none) return false;
        const id = c.nodes.items[i].id;
        const in = c.interactKeys(id, o.disabled, false);
        const before = selected.*;
        var shown = c.popup == id and !o.disabled;
        if (c.popup == id and !shown) c.popup = 0;
        if (in.clicked) {
            shown = !shown;
            if (shown) c.highlight = if (has) selected.* else 0;
        }
        if (in.focused and options.len > 0) for (c.input.keys[0..c.input.nkeys]) |*k| {
            if (k.used) continue;
            switch (k.key) {
                .space, .enter => if (shown) {
                    selected.* = @min(c.highlight, options.len - 1);
                    shown = false;
                } else {
                    shown = true;
                    c.highlight = if (has) selected.* else 0;
                },
                .escape => if (shown) {
                    shown = false;
                } else continue,
                .up => if (shown) {
                    c.highlight -|= 1;
                } else {
                    selected.* = if (has) selected.* -| 1 else 0;
                },
                .down => if (shown) {
                    c.highlight = @min(c.highlight + 1, options.len - 1);
                } else {
                    selected.* = if (has) @min(selected.* + 1, options.len - 1) else 0;
                },
                else => continue,
            }
            k.used = true;
            c.acted = true;
        };
        // A click outside closes the popup.
        if (shown and c.input.clicked == id ^ popup_salt ^ blocker_salt) {
            c.input.clicked = 0;
            shown = false;
            c.acted = true;
        }
        if (shown) if (c.lastRect(id)) |anchor| {
            c.beginOverlay(id ^ popup_salt, anchor, .below, .popup, .{ .padding = 4, .@"align" = .stretch });
            const first: u32 = @intCast(c.nodes.items.len);
            for (options, 0..) |opt, k| {
                const oo, const ol = c.str(opt);
                const oi = c.add(.{ .kind = .option, .str_off = oo, .str_len = ol, .size = t.font_size, .color = t.text, .first_index = @intCast(k) }, null);
                if (oi == none) continue;
                const on = &c.nodes.items[oi];
                const oin = c.interactKeys(on.id, false, false);
                if (oin.hot and c.input.hot != 0) c.highlight = k;
                if (oin.clicked) {
                    selected.* = k;
                    shown = false;
                }
            }
            // The highlight, where the pointer or the keys left it.
            for (c.nodes.items[first..]) |*on| if (on.kind == .option and on.parent == first - 1) {
                on.flags = .{ .on = on.first_index == selected.*, .hot = on.first_index == c.highlight };
            };
            c.end();
        } else {
            c.requested = true; // its place is known after this frame
        };
        c.popup = if (shown) id else if (c.popup == id) 0 else c.popup;
        const n = &c.nodes.items[i];
        n.flags = .{ .hot = in.hot, .focused = in.focused, .on = shown, .disabled = o.disabled };
        if (selected.* != before) c.acted = true;
        return selected.* != before;
    }

    /// tooltip shows s by the pointer when it rests on the control built
    /// last, for a moment.
    pub fn tooltip(c: *Context, s: []const u8) void {
        if (c.nodes.items.len == 0) return;
        const id = c.nodes.items[c.nodes.items.len - 1].id;
        if (c.input.hot != id or c.input.active != 0) {
            if (c.tip_id == id) c.tip_id = 0;
            return;
        }
        if (c.tip_id != id) {
            c.tip_id = id;
            c.tip_since = c.frame_time;
        }
        const due = c.tip_since + tip_delay;
        if (c.frame_time < due) return c.wakeAt(due);
        c.beginOverlay(id ^ tip_salt, .{ .x = c.input.x, .y = c.input.y }, .pointer, .tip, .{ .padding_x = 8, .padding_y = 5, .width = null });
        c.text(s, .{ .size = 12, .color = c.theme.background });
        c.end();
    }

    pub const ModalOpts = struct {
        title: ?[]const u8 = null,
        width: f32 = 400,
        /// dismissable lets Escape and a click outside close the dialog.
        dismissable: bool = true,
        key: ?[]const u8 = null,
    };

    /// beginModal shows a dialog over the window while open is set: it
    /// reports whether it does, and then the caller builds its content and
    /// ends it with endModal. Escape or a click outside clear open.
    ///
    ///     if (c.beginModal(&s.confirm, .{ .title = "Delete?" })) {
    ///         defer c.endModal();
    ///         ...
    ///     }
    pub fn beginModal(c: *Context, is_open: *bool, o: ModalOpts) bool {
        if (!is_open.*) return false;
        const id = std.hash.Wyhash.hash(0x6d6f64616c, o.key orelse "modal");
        if (o.dismissable) {
            if (c.input.clicked == id ^ blocker_salt) {
                c.input.clicked = 0;
                is_open.* = false;
                c.acted = true;
                return false;
            }
            for (c.input.keys[0..c.input.nkeys]) |*k| if (!k.used and k.key == .escape) {
                k.used = true;
                is_open.* = false;
                c.acted = true;
                return false;
            };
        }
        // Just opened: the focus leaves what is under it.
        if (c.lastRect(id ^ blocker_salt) == null) c.input.focus = 0;
        c.beginOverlay(id, .{}, .center, .modal, .{ .width = o.width, .padding = 20, .gap = 14 });
        if (o.title) |title| c.text(title, .{ .size = 16, .bold = true });
        return true;
    }

    pub fn endModal(c: *Context) void {
        c.end();
    }

    // ---- other buttons ----

    /// pressedWith reports where button (right or middle) went down on the
    /// row or column being built, since the last frame, unless a box in it
    /// that also asks took it: a right click opens a context menu so.
    pub fn pressedWith(c: *Context, btn: MouseButton) ?Point {
        if (c.stack.items.len == 0) return null;
        const n = &c.nodes.items[c.stack.items[c.stack.items.len - 1]];
        n.flags.listens = true;
        if (c.input.aux == 0 or c.input.aux != n.id or c.input.aux_button != btn) return null;
        c.input.aux = 0;
        c.acted = true;
        return c.input.aux_at;
    }

    /// navigation reports a press of the mouse's back or forward button
    /// since the last frame.
    pub fn navigation(c: *Context) ?Nav {
        const nav = c.input.nav orelse return null;
        c.input.nav = null;
        c.acted = true;
        return nav;
    }

    /// beginMenu shows m's menu at its point while it is open: it reports
    /// whether it does, and then the caller adds menuItems and ends it with
    /// endMenu. The arrows move in it, Enter or a click chooses, and
    /// Escape or a click outside close it.
    pub fn beginMenu(c: *Context, m: *Menu) bool {
        const id = std.hash.Wyhash.hash(0x6d656e75, std.mem.asBytes(&@intFromPtr(m)));
        if (!m.open) {
            if (c.menu == id) c.menu = 0;
            return false;
        }
        if (c.menu != id) {
            // Just opened: nothing is highlighted, and the keys are its.
            c.menu = id;
            c.menu_items = 0;
            c.highlight = std.math.maxInt(usize);
            c.input.focus = 0;
        }
        if (c.input.clicked == id ^ blocker_salt) {
            c.input.clicked = 0;
            return c.closeMenu(m);
        }
        for (c.input.keys[0..c.input.nkeys]) |*k| {
            if (k.used) continue;
            const none_lit = std.math.maxInt(usize);
            switch (k.key) {
                .escape => {
                    k.used = true;
                    return c.closeMenu(m);
                },
                .down => c.highlight = if (c.highlight == none_lit or c.highlight + 1 >= c.menu_items) 0 else c.highlight + 1,
                .up => c.highlight = if (c.highlight == none_lit or c.highlight == 0) c.menu_items -| 1 else c.highlight - 1,
                else => continue,
            }
            k.used = true;
            c.acted = true;
        }
        c.menu_index = 0;
        c.menu_state = m;
        c.beginOverlay(id, .{ .x = m.at.x, .y = m.at.y }, .at, .popup, .{ .padding = 4, .@"align" = .stretch });
        return true;
    }

    fn closeMenu(c: *Context, m: *Menu) bool {
        m.open = false;
        c.menu = 0;
        c.acted = true;
        return false;
    }

    pub fn endMenu(c: *Context) void {
        c.menu_items = c.menu_index;
        c.menu_state = null;
        c.end();
    }

    /// menuItem adds an item to the menu being built, with the shortcut
    /// that does the same shown by it; it reports whether it was chosen,
    /// which closes the menu.
    pub fn menuItem(c: *Context, label: []const u8, o: MenuItemOpts) bool {
        const t = c.theme;
        const off, const len = c.str(label);
        const so, const sl = c.str(o.shortcut);
        const i = c.add(.{ .kind = .menu_item, .str_off = off, .str_len = len, .sub_off = so, .sub_len = sl, .size = t.font_size, .color = t.text }, o.key orelse label);
        if (i == none) return false;
        const index = c.menu_index;
        c.menu_index += 1;
        const id = c.nodes.items[i].id;
        var in = c.interactKeys(id, o.disabled, false);
        if (in.hot and c.input.hot != 0) c.highlight = index;
        if (!o.disabled and index == c.highlight) for (c.input.keys[0..c.input.nkeys]) |*k| {
            if (!k.used and (k.key == .enter or k.key == .space)) {
                k.used = true;
                in.clicked = true;
            }
        };
        const n = &c.nodes.items[i];
        n.flags = .{ .hot = index == c.highlight, .disabled = o.disabled };
        if (n.flags.disabled) n.color = t.text_muted;
        if (in.clicked) if (c.menu_state) |m| {
            _ = c.closeMenu(m);
        };
        return in.clicked;
    }

    /// menuSeparator is a line between groups of a menu's items.
    pub fn menuSeparator(c: *Context) void {
        c.column(.{ .padding_y = 4 });
        c.divider();
        c.end();
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
            c.input.aux = 0;
            c.input.nav = null;
            c.input.wheel_x = 0;
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
            var items = c.hits.items;
            // A modal dialog keeps the focus.
            var m = items.len;
            while (m > 0) {
                m -= 1;
                if (items[m].modal) {
                    items = items[m + 1 ..];
                    break;
                }
            }
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
        c.overlays.clearRetainingCapacity();
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
        c.input.aux = 0;
        c.input.nav = null;
        c.input.wheel_x = 0;
        c.ime_rect = null;
        if (c.nodes.items.len == 0) return;
        _ = c.measure(0, c.width);
        c.arrange(0, .{ .x = 0, .y = 0, .w = c.width, .h = c.height });
        c.next_hits.clearRetainingCapacity();
        c.texts.clearRetainingCapacity();
        s.background = c.theme.background;
        c.paint_clip = .{ .x = 0, .y = 0, .w = c.width, .h = c.height };
        c.paint(0, s);
        // Overlays, over everything, in the order they were opened.
        for (c.overlays.items) |oi| c.placeOverlay(oi, s);
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

    fn placeOverlay(c: *Context, oi: u32, s: *Scene) void {
        const m = c.measure(oi, c.width - 32);
        const on = &c.nodes.items[oi];
        const margin: f32 = 8;
        var w = @max(m[0], if (on.place == .below) on.anchor.w else 0);
        w = @min(w, c.width - 2 * margin);
        const h = @min(m[1], c.height - 2 * margin);
        var x: f32 = 0;
        var y: f32 = 0;
        switch (on.place) {
            .below => {
                x = on.anchor.x;
                y = on.anchor.y + on.anchor.h + 4;
                if (y + h > c.height - margin and on.anchor.y - h - 4 >= margin) y = on.anchor.y - h - 4;
            },
            .pointer => {
                x = on.anchor.x;
                y = on.anchor.y + 22;
                if (y + h > c.height - margin) y = on.anchor.y - h - 8;
            },
            .center => {
                x = (c.width - w) / 2;
                y = (c.height - h) / 2;
            },
            .at => {
                // At the point, flipped left or up where it does not fit.
                x = on.anchor.x;
                y = on.anchor.y;
                if (x + w > c.width - margin and on.anchor.x - w >= margin) x = on.anchor.x - w;
                if (y + h > c.height - margin and on.anchor.y - h >= margin) y = on.anchor.y - h;
            },
        }
        x = std.math.clamp(x, margin, @max(margin, c.width - w - margin));
        y = std.math.clamp(y, margin, @max(margin, c.height - h - margin));
        c.arrange(oi, .{ .x = x, .y = y, .w = w, .h = h });
        c.paint_clip = .{ .x = 0, .y = 0, .w = c.width, .h = c.height };
        c.paint(oi, s);
    }

    fn labelWidth(c: *Context, n: *const Node) f32 {
        if (n.str_len == 0) return 0;
        return c.fonts.measure(n.weight, n.size, c.strings.items[n.str_off..][0..n.str_len]);
    }

    const check_size: f32 = 16;
    const menu_pad: f32 = 10;
    const toggle_w: f32 = 36;
    const toggle_h: f32 = 20;
    const label_gap: f32 = 8;

    /// measure sizes node i and what is in it, given avail of width (what
    /// wrapped text fits in).
    fn measure(c: *Context, i: u32, avail: f32) [2]f32 {
        var n = &c.nodes.items[i];
        var w: f32 = 0;
        var h: f32 = 0;
        switch (n.kind) {
            .text => {
                w = c.labelWidth(n);
                h = c.fonts.metrics(n.weight, n.size).line_height;
                const limit = n.box.width orelse avail;
                if (n.flags.wrap and (w > limit or std.mem.indexOfScalar(u8, c.strings.items[n.str_off..][0..n.str_len], '\n') != null)) {
                    var it = c.lines(n, limit);
                    var lines_: f32 = 0;
                    var widest: f32 = 0;
                    while (it.next()) |ln| {
                        lines_ += 1;
                        widest = @max(widest, ln.width);
                    }
                    w = widest;
                    h *= @max(1, lines_);
                }
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
            .select => {
                w = n.value + 2 * (n.box.padding_x orelse 0) + 18;
                h = c.fonts.metrics(.regular, n.size).line_height + 2 * c.theme.space(2);
            },
            .option => {
                w = c.labelWidth(n) + 28 + 12;
                h = c.fonts.metrics(.regular, n.size).line_height + 12;
            },
            .menu_item => {
                w = c.labelWidth(n) + 2 * menu_pad;
                if (n.sub_len > 0) w += 32 + c.fonts.measure(.regular, n.size, c.strings.items[n.sub_off..][0..n.sub_len]);
                h = c.fonts.metrics(.regular, n.size).line_height + 10;
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
                const inner = n.box.width orelse avail;
                while (ch != none) : (ch = c.nodes.items[ch].next) _ = c.measure(ch, inner);
            },
            .box, .scroll, .overlay => {
                const px = n.box.padding_x orelse n.box.padding;
                const py = n.box.padding_y orelse n.box.padding;
                const inner = @max(0, (n.box.width orelse avail) - 2 * px);
                const row_dir = n.dir == .row;
                var k: u32 = 0;
                var flexible: u32 = 0;
                // A row measures its fixed children first: the ones that
                // flex (grow, or wrap) share the width they leave.
                var ch = n.first;
                while (ch != none) : (ch = c.nodes.items[ch].next) {
                    k += 1;
                    if (row_dir and flexes(&c.nodes.items[ch])) {
                        flexible += 1;
                        continue;
                    }
                    const m = c.measure(ch, inner);
                    if (row_dir) {
                        w += m[0];
                        h = @max(h, m[1]);
                    } else {
                        h += m[1];
                        w = @max(w, m[0]);
                    }
                }
                n = &c.nodes.items[i];
                const gaps = if (k > 1) n.box.gap * @as(f32, @floatFromInt(k - 1)) else 0;
                if (flexible > 0) {
                    const share = @max(0, inner - w - gaps) / @as(f32, @floatFromInt(flexible));
                    ch = n.first;
                    while (ch != none) : (ch = c.nodes.items[ch].next) {
                        if (!flexes(&c.nodes.items[ch])) continue;
                        const m = c.measure(ch, share);
                        w += m[0];
                        h = @max(h, m[1]);
                    }
                    n = &c.nodes.items[i];
                }
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

    /// Lines breaks a wrapped text into lines that fit width: at spaces,
    /// greedily, and at newlines. A word wider than the width is a line.
    const Lines = struct {
        c: *Context,
        s: []const u8,
        width: f32,
        weight: textm.Weight,
        size: f32,
        space: f32,
        i: usize = 0,
        done: bool = false,

        const Line = struct { text: []const u8, width: f32 };

        fn next(it: *Lines) ?Line {
            if (it.done) return null;
            const s = it.s;
            const start = it.i;
            var e = start; // the end of the last word that fits
            var w: f32 = 0;
            var j = start;
            while (true) {
                var k = j;
                while (k < s.len and s[k] != ' ' and s[k] != '\n') k += 1;
                const with = w + it.space * @as(f32, @floatFromInt(j - e)) + it.c.fonts.measure(it.weight, it.size, s[j..k]);
                if (with > it.width and e > start) {
                    // Break before this word; the spaces go with the break.
                    var after = e;
                    while (after < s.len and s[after] == ' ') after += 1;
                    it.i = after;
                    if (after >= s.len) it.done = true;
                    return .{ .text = s[start..e], .width = w };
                }
                w = with;
                e = k;
                if (k >= s.len) {
                    it.done = true;
                    return .{ .text = s[start..e], .width = w };
                }
                if (s[k] == '\n') {
                    it.i = k + 1;
                    return .{ .text = s[start..e], .width = w };
                }
                j = k + 1;
            }
        }
    };

    fn lines(c: *Context, n: *const Node, width: f32) Lines {
        return .{
            .c = c,
            .s = c.strings.items[n.str_off..][0..n.str_len],
            .width = width,
            .weight = n.weight,
            .size = n.size,
            .space = c.fonts.measure(n.weight, n.size, " "),
        };
    }

    fn arrange(c: *Context, i: u32, r: Rect) void {
        const n = &c.nodes.items[i];
        n.rect = r;
        switch (n.kind) {
            .box, .overlay => c.arrangeChildren(i, r),
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

    /// flexes reports whether a row's child takes a share of its free width.
    fn flexes(cn: *const Node) bool {
        return cn.box.grow > 0 or cn.box.fill or (cn.kind == .text and cn.flags.wrap) or (cn.kind == .input and cn.box.grow > 0);
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
                if (n.flags.wrap and (w > r.w + 0.5 or std.mem.indexOfScalar(u8, str_, '\n') != null)) {
                    var it = c.lines(n, r.w + 0.5);
                    var y = r.y + m.ascent;
                    while (it.next()) |ln| : (y += m.line_height) {
                        const x = switch (n.text_align) {
                            .center => r.x + (r.w - ln.width) / 2,
                            .end => r.x + r.w - ln.width,
                            else => r.x,
                        };
                        s.textRun(c.fonts, x, y, ln.text, n.size, n.weight, n.color);
                    }
                    c.texts.append(c.gpa, .{ .off = n.str_off, .len = n.str_len, .rect = r, .id = n.id }) catch {};
                    return;
                }
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
            .select => {
                c.focusRing(s, n, r, n.box.radius);
                const fade: f32 = if (n.flags.disabled) 0.5 else 1;
                const ring = if (n.flags.focused or n.flags.on) t.accent else if (n.flags.hot) t.border_strong else t.border;
                const bg = if (n.flags.hot and !n.flags.disabled) t.surface else t.background;
                s.rect(r, bg.withAlpha(fade), n.box.radius, if (n.flags.focused or n.flags.on) 1.5 else 1, ring.withAlpha(fade));
                const pad = n.box.padding_x orelse 0;
                s.pushClip(.{ .x = r.x + pad, .y = r.y, .w = @max(0, r.w - 2 * pad - 16), .h = r.h });
                c.paintLabel(s, n, r.x + pad, r);
                s.popClip();
                const chev = Rect{ .x = r.x + r.w - pad - 10, .y = r.y + r.h / 2 - 2.5, .w = 10, .h = 5 };
                const flip: [2][4]f32 = if (n.flags.on) .{ .{ 0, 1, 0.5, 0 }, .{ 0.5, 0, 1, 1 } } else .{ .{ 0, 0, 0.5, 1 }, .{ 0.5, 1, 1, 0 } };
                s.stroke(chev, flip, 1.5, t.text_muted.withAlpha(fade));
                c.addHit(n, true);
            },
            .menu_item => {
                if (n.flags.hot and !n.flags.disabled) s.rect(r, t.surface_hover, 4, 0, .transparent);
                c.paintLabel(s, n, r.x + menu_pad, r);
                if (n.sub_len > 0) {
                    const sub = c.strings.items[n.sub_off..][0..n.sub_len];
                    const m = c.fonts.metrics(.regular, n.size);
                    const sw = c.fonts.measure(.regular, n.size, sub);
                    s.textRun(c.fonts, r.x + r.w - menu_pad - sw, r.y + (r.h - m.line_height) / 2 + m.ascent, sub, n.size, .regular, t.text_muted);
                    c.texts.append(c.gpa, .{ .off = n.sub_off, .len = n.sub_len, .rect = r, .id = n.id }) catch {};
                }
                c.addHit(n, false);
            },
            .option => {
                if (n.flags.hot) s.rect(r, t.surface_hover, 4, 0, .transparent);
                if (n.flags.on) s.stroke(.{ .x = r.x + 9, .y = r.y + r.h / 2 - 4, .w = 10, .h = 8 }, .{ .{ 0, 0.5, 0.36, 0.95 }, .{ 0.36, 0.95, 1, 0.05 } }, 1.75, t.accent);
                c.paintLabel(s, n, r.x + 28, r);
                c.addHit(n, false);
            },
            .overlay => {
                const full = Rect{ .x = 0, .y = 0, .w = c.width, .h = c.height };
                const tip = n.mode == .tip;
                if (n.mode == .modal) s.rect(full, Color.rgba(0, 0, 0, 0.4), 0, 0, .transparent);
                // What is under it does not take the pointer.
                if (!tip) c.next_hits.append(c.gpa, .{ .id = n.id ^ blocker_salt, .rect = full, .blocks = true, .modal = n.mode == .modal }) catch {};
                const radius: f32 = if (tip) 4 else if (n.mode == .modal) 10 else t.radius + 2;
                s.shadow(.{ .x = r.x, .y = r.y + 4, .w = r.w, .h = r.h }, radius, if (tip) 6 else 16, Color.rgba(0, 0, 0, if (tip) 0.18 else 0.24));
                if (tip) {
                    s.rect(r, t.text, radius, 0, .transparent);
                } else {
                    s.rect(r, t.background, radius, 1, t.border);
                }
                s.pushClip(r);
                c.paint_clip = r;
                var ch = n.first;
                while (ch != none) : (ch = c.nodes.items[ch].next) c.paint(ch, s);
                s.popClip();
            },
            .box, .list, .scroll => {
                s.rect(r, n.box.background, n.box.radius, n.box.border, n.box.border_color);
                if (n.flags.listens) {
                    const hr = intersect(r, c.paint_clip);
                    if (hr.w > 0 and hr.h > 0) c.next_hits.append(c.gpa, .{ .id = n.id, .rect = hr, .passive = true }) catch {};
                }
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
        const composing = n.flags.focused and !n.flags.password and c.input.preedit_len > 0;
        const shown = if (n.flags.password) c.dots(st.text.items) else if (composing) c.composed(st) else st.text.items;
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
            if (composing) {
                // What the input method composes, underlined.
                const ux0 = c.fonts.prefixWidth(.regular, n.size, shown, st.caret);
                const ux1 = c.fonts.prefixWidth(.regular, n.size, shown, st.caret + c.input.preedit_len);
                s.rect(.{ .x = x0 + ux0, .y = baseline + 2, .w = ux1 - ux0, .h = 1 }, t.text, 0, 0, .transparent);
            } else if (n.flags.focused and sel[0] != sel[1]) {
                const sx0 = c.fonts.prefixWidth(.regular, n.size, shown, at(n, st, sel[0]));
                const sx1 = c.fonts.prefixWidth(.regular, n.size, shown, at(n, st, sel[1]));
                s.rect(.{ .x = x0 + sx0, .y = baseline - m.ascent, .w = sx1 - sx0, .h = m.line_height }, t.selection, 0, 0, .transparent);
            }
            s.textRun(c.fonts, x0, baseline, shown, n.size, .regular, t.text.withAlpha(fade));
        }
        const caret_byte = if (composing) st.caret + c.input.preedit_caret else at(n, st, st.caret);
        const cx = c.fonts.prefixWidth(.regular, n.size, shown, caret_byte);
        if (n.flags.caret or (composing and n.flags.focused)) {
            s.rect(.{ .x = x0 + cx - 0.5, .y = baseline - m.ascent, .w = 1.5, .h = m.ascent + m.descent }, t.text, 0, 0, .transparent);
        }
        if (n.flags.focused) c.ime_rect = .{ .x = @min(x0 + cx, inner.x + inner.w), .y = baseline - m.ascent, .w = 1, .h = m.line_height };
        c.texts.append(c.gpa, .{ .off = n.str_off, .len = n.str_len, .rect = r, .id = n.id }) catch {};
    }

    // ---- input ----

    fn hitTest(c: *const Context, x: f32, y: f32) u64 {
        var i = c.hits.items.len;
        while (i > 0) {
            i -= 1;
            const h = c.hits.items[i];
            if (h.list == null and !h.passive and h.rect.contains(x, y)) return h.id;
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

    /// pointerDown is the left button's press; pointerPress takes any
    /// button and the platform's click count (0 counts here).
    pub fn pointerDown(c: *Context, x: f32, y: f32) bool {
        return c.pointerPress(x, y, .left, 0);
    }

    pub fn pointerPress(c: *Context, x: f32, y: f32, btn: MouseButton, clicks: u8) bool {
        switch (btn) {
            .left => {},
            .back, .forward => {
                c.input.nav = if (btn == .back) .back else .forward;
                return true;
            },
            .right, .middle => {
                _ = c.pointerMove(x, y);
                // The innermost box that listens takes it.
                var i = c.hits.items.len;
                while (i > 0) {
                    i -= 1;
                    const h = c.hits.items[i];
                    if (!h.rect.contains(x, y)) continue;
                    if (h.blocks) break;
                    if (!h.passive) continue;
                    c.input.aux = h.id;
                    c.input.aux_button = btn;
                    c.input.aux_at = .{ .x = x, .y = y };
                    return true;
                }
                return false;
            },
        }
        // Presses close in time and place count as one double (triple) click.
        const now = std.time.nanoTimestamp();
        const near = @abs(x - c.input.last_press.x) <= 4 and @abs(y - c.input.last_press.y) <= 4;
        c.input.clicks = if (clicks > 0) clicks else if (near and now - c.input.last_press_ns < 500 * std.time.ns_per_ms) c.input.clicks % 3 + 1 else 1;
        c.input.last_press_ns = now;
        c.input.last_press = .{ .x = x, .y = y };
        _ = c.pointerMove(x, y);
        c.input.active = c.input.hot;
        c.input.pressed = c.input.hot;
        const was = c.input.focus;
        c.input.focus = if (c.hitFocusable(c.input.hot)) c.input.hot else 0;
        c.input.focus_visible = false;
        return c.input.active != 0 or was != c.input.focus;
    }

    pub fn pointerUp(c: *Context, x: f32, y: f32) bool {
        return c.pointerRelease(x, y, .left);
    }

    pub fn pointerRelease(c: *Context, x: f32, y: f32, btn: MouseButton) bool {
        if (btn != .left) return c.pointerMove(x, y);
        _ = c.pointerMove(x, y);
        const was = c.input.active;
        c.input.active = 0;
        if (was != 0 and was == c.input.hot) c.input.clicked = was;
        return was != 0;
    }

    pub fn scroll(c: *Context, x: f32, y: f32, dy: f32) bool {
        return c.scrollXY(x, y, 0, dy);
    }

    /// scrollXY scrolls what is under the pointer by dy, and keeps dx for
    /// views that scroll sideways (wheel_x); both in logical pixels, a
    /// trackpad's exact deltas or a wheel's notches.
    pub fn scrollXY(c: *Context, x: f32, y: f32, dx: f32, dy: f32) bool {
        var moved = false;
        if (dx != 0) {
            c.input.wheel_x += dx;
            moved = true;
        }
        if (dy == 0) return moved;
        var i = c.hits.items.len;
        while (i > 0) {
            i -= 1;
            const h = c.hits.items[i];
            if (h.blocks and h.rect.contains(x, y)) return false;
            if (h.list) |st| if (h.rect.contains(x, y)) {
                const before = st.offset;
                st.scrollBy(dy);
                if (st.offset != before) return true;
            };
        }
        return moved;
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

    /// setPreedit sets the text an input method is composing, and its caret
    /// (a byte offset in it); "" when it is done. It reports whether that
    /// changed what the window shows.
    pub fn setPreedit(c: *Context, s: []const u8, caret: usize) bool {
        var n = @min(s.len, c.input.preedit.len);
        while (n < s.len and n > 0 and s[n] & 0xc0 == 0x80) n -= 1;
        const same = n == c.input.preedit_len and std.mem.eql(u8, s[0..n], c.input.preedit[0..n]) and @min(caret, n) == c.input.preedit_caret;
        @memcpy(c.input.preedit[0..n], s[0..n]);
        c.input.preedit_len = @intCast(n);
        c.input.preedit_caret = @intCast(@min(caret, n));
        return !same and c.input.text_focus;
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

test "wrapped text breaks at spaces to fit, and a row shares what is left" {
    const gpa = std.testing.allocator;
    var fonts = try textm.Fonts.init(gpa);
    defer fonts.deinit();
    var c = Context.init(gpa, &fonts);
    defer c.deinit();
    var sc = Scene{ .gpa = gpa };
    defer sc.deinit();
    const para = "MyZig lays text out in lines that fit the width they are given, breaking at spaces.\nA newline breaks too.";
    const View = struct {
        fn view(_: *const u8, cx: *Context) void {
            cx.column(.{ .width = 200, .key = "col" });
            cx.text(para, .{ .wrap = true });
            cx.end();
            cx.row(.{ .width = 300, .gap = 10 });
            cx.text("Label", .{});
            cx.text(para, .{ .wrap = true });
            cx.end();
        }
    };
    const dummy: u8 = 0;
    c.build(&dummy, View.view, 800, 600, 0);
    sc.reset(800, 600, 1, c.theme.background);
    c.endFrame(&sc);
    const lh = fonts.metrics(.regular, 14).line_height;
    var lines_in_col: usize = 0;
    var lines_in_row: usize = 0;
    for (sc.ops.items) |op| if (op.kind == .text) {
        const str_ = sc.string(&op);
        if (std.mem.eql(u8, str_, "Label")) continue;
        try std.testing.expect(str_[0] != ' ' and str_[str_.len - 1] != ' ');
        if (op.rect.x == 0) {
            lines_in_col += 1;
            try std.testing.expect(op.rect.w <= 200.5);
        } else {
            lines_in_row += 1;
            // What the label and the gap leave of 300.
            try std.testing.expect(op.rect.x > 10 and op.rect.x + op.rect.w <= 300.5);
        }
    };
    try std.testing.expect(lines_in_col >= 4);
    try std.testing.expect(lines_in_row >= 3 and lines_in_row <= lines_in_col);
    // The column is as tall as its lines.
    for (c.nodes.items) |n| if (n.kind == .text and n.flags.wrap and n.rect.x == 0) {
        try std.testing.expectEqual(lh * @as(f32, @floatFromInt(lines_in_col)), n.rect.h);
    };
}
