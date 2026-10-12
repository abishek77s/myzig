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
//! the last one built allocates nothing. Views only run when input or an
//! animation asks for a frame; nothing polls.

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
    text: Color = Color.hex("#18181b"),
    text_muted: Color = Color.hex("#71717a"),
    accent: Color = Color.hex("#2563eb"),
    accent_hover: Color = Color.hex("#1d4ed8"),
    accent_pressed: Color = Color.hex("#1e40af"),
    accent_text: Color = Color.hex("#ffffff"),
    danger: Color = Color.hex("#dc2626"),
    success: Color = Color.hex("#16a34a"),
    scrollbar: Color = Color.rgba(0, 0, 0, 0.32),
    radius: f32 = 6,
    font_size: f32 = 14,

    pub fn space(t: Theme, n: f32) f32 {
        _ = t;
        return 4 * n;
    }
};

pub const Key = enum(u8) { none, up, down, left, right, enter, escape, space, page_up, page_down, home, end, tab, backspace };

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

/// ListState is what a virtualized list keeps between frames.
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

const none: u32 = std.math.maxInt(u32);

const Kind = enum(u8) { box, text, button, list };

const Node = struct {
    kind: Kind,
    dir: enum(u1) { row, column } = .column,
    box: Box = .{},
    parent: u32 = none,
    first: u32 = none,
    last: u32 = none,
    next: u32 = none,
    children: u32 = 0,
    id: u64 = 0,
    // text and buttons
    str_off: u32 = 0,
    str_len: u32 = 0,
    size: f32 = 0,
    weight: textm.Weight = .regular,
    color: Color = .transparent,
    text_align: Align = .start,
    // lists
    list: ?*ListState = null,
    first_index: u32 = 0,
    row_h: f32 = 0,
    // layout
    iw: f32 = 0,
    ih: f32 = 0,
    rect: Rect = .{},
};

const Hit = struct { id: u64, rect: Rect, list: ?*ListState = null };

pub const Input = struct {
    x: f32 = -1,
    y: f32 = -1,
    hot: u64 = 0,
    active: u64 = 0,
    clicked: u64 = 0,
    keys: [8]Key = .{.none} ** 8,
    nkeys: u8 = 0,
};

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
    /// acted is set when the view acted on input, which may have changed
    /// what it shows: the frame is built again before it paints.
    acted: bool = false,
    /// frame_time is the monotonic time of the frame being built, in ns.
    frame_time: u64 = 0,
    frames: u64 = 0,

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
    }

    // ---- building ----

    /// animationFrame asks for another frame as soon as the display can show
    /// one, for views that change every frame.
    pub fn animationFrame(c: *Context) void {
        c.animating = true;
    }

    /// shortcut reports whether key was pressed since the last frame.
    pub fn shortcut(c: *Context, key: Key) bool {
        for (c.input.keys[0..c.input.nkeys]) |k| if (k == key) {
            c.acted = true;
            return true;
        };
        return false;
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

    pub fn textf(c: *Context, comptime fmt: []const u8, args: anytype, o: TextOpts) void {
        const off: u32 = @intCast(c.strings.items.len);
        c.strings.print(c.gpa, fmt, args) catch return;
        const len: u32 = @intCast(c.strings.items.len - off);
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
        const hot = c.input.hot == n.id and !o.disabled;
        const pressed = hot and c.input.active == n.id;
        if (o.primary) {
            n.box.background = if (pressed) t.accent_pressed else if (hot) t.accent_hover else t.accent;
            n.color = t.accent_text;
        } else {
            n.box.background = if (pressed) t.surface_pressed else if (hot) t.surface_hover else t.surface;
            n.box.border = 1;
            n.box.border_color = t.border;
            n.color = t.text;
        }
        if (o.disabled) {
            n.box.background = n.box.background.withAlpha(0.5);
            n.color = n.color.withAlpha(0.45);
            n.box.border_color = n.box.border_color.withAlpha(0.5);
            return false;
        }
        if (c.input.clicked == n.id) {
            c.input.clicked = 0;
            c.acted = true;
            return true;
        }
        return false;
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

    // ---- frames ----

    /// build runs view for a frame of width×height, twice when it acted on
    /// input, so that a click's effect shows in the frame that reports it.
    pub fn build(c: *Context, state: anytype, view: anytype, width: f32, height: f32, now: u64) void {
        c.acted = false;
        c.beginFrame(width, height, now);
        view(state, c);
        if (c.acted) {
            c.input.nkeys = 0;
            c.input.clicked = 0;
            c.acted = false;
            c.beginFrame(width, height, now);
            c.frames -= 1;
            view(state, c);
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
        c.frame_time = now;
        c.frames += 1;
        // The root fills the window.
        _ = c.add(.{ .kind = .box, .box = .{ .width = width, .height = height } }, null);
        c.stack.append(c.gpa, 0) catch {};
    }

    /// endFrame lays the tree out and paints it into s.
    pub fn endFrame(c: *Context, s: *Scene) void {
        c.input.nkeys = 0;
        c.input.clicked = 0;
        if (c.nodes.items.len == 0) return;
        _ = c.measure(0);
        c.arrange(0, .{ .x = 0, .y = 0, .w = c.width, .h = c.height });
        c.next_hits.clearRetainingCapacity();
        c.texts.clearRetainingCapacity();
        s.background = c.theme.background;
        c.paint(0, s);
        std.mem.swap(std.ArrayListUnmanaged(Hit), &c.hits, &c.next_hits);
        // The pointer may be over another control now.
        const h = c.hitTest(c.input.x, c.input.y);
        if (h != c.input.hot) {
            c.input.hot = h;
            c.requested = true;
        }
    }

    fn measure(c: *Context, i: u32) [2]f32 {
        var n = &c.nodes.items[i];
        var w: f32 = 0;
        var h: f32 = 0;
        switch (n.kind) {
            .text => {
                const s = c.strings.items[n.str_off..][0..n.str_len];
                w = c.fonts.measure(n.weight, n.size, s);
                h = c.fonts.metrics(n.weight, n.size).line_height;
            },
            .button => {
                const s = c.strings.items[n.str_off..][0..n.str_len];
                w = c.fonts.measure(n.weight, n.size, s) + 2 * (n.box.padding_x orelse 0);
                h = c.fonts.metrics(n.weight, n.size).line_height + 2 * (n.box.padding_y orelse 0);
            },
            .list => {
                var ch = n.first;
                while (ch != none) : (ch = c.nodes.items[ch].next) _ = c.measure(ch);
            },
            .box => {
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
            .text, .button => return,
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
                return;
            },
            .box => {},
        }
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
            grow += childGrow(cn, row_dir);
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
            const g = childGrow(cn, row_dir);
            if (g > 0 and free > 0) m += free * g / grow;
            var cs = if (row_dir) cn.ih else cn.iw;
            const stretch = box.@"align" == .stretch or cn.box.fill or (cn.kind == .list);
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

    fn childGrow(cn: *const Node, row_dir: bool) f32 {
        _ = row_dir;
        if (cn.box.grow > 0) return cn.box.grow;
        if (cn.box.fill or cn.kind == .list) return 1;
        return 0;
    }

    fn paint(c: *Context, i: u32, s: *Scene) void {
        const n = &c.nodes.items[i];
        const r = n.rect;
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
                s.rect(r, n.box.background, n.box.radius, n.box.border, n.box.border_color);
                const str_ = c.strings.items[n.str_off..][0..n.str_len];
                const m = c.fonts.metrics(n.weight, n.size);
                const w = c.fonts.measure(n.weight, n.size, str_);
                s.textRun(c.fonts, r.x + (r.w - w) / 2, r.y + (r.h - m.line_height) / 2 + m.ascent, str_, n.size, n.weight, n.color);
                c.next_hits.append(c.gpa, .{ .id = n.id, .rect = r }) catch {};
                c.texts.append(c.gpa, .{ .off = n.str_off, .len = n.str_len, .rect = r, .id = n.id }) catch {};
            },
            .box, .list => {
                s.rect(r, n.box.background, n.box.radius, n.box.border, n.box.border_color);
                if (n.kind == .list) {
                    s.pushLayer(n.id, r, n.list.?.offset);
                } else if (n.box.clip) s.pushClip(r);
                var ch = n.first;
                while (ch != none) : (ch = c.nodes.items[ch].next) c.paint(ch, s);
                if (n.kind == .list) {
                    s.popLayer();
                    s.pushClip(r);
                    const st = n.list.?;
                    c.next_hits.append(c.gpa, .{ .id = n.id, .rect = r, .list = st }) catch {};
                    if (st.content > st.viewport and st.viewport > 0) {
                        const th = @max(24, r.h * st.viewport / st.content);
                        const ty = r.y + (r.h - th) * (st.offset / st.maxOffset());
                        s.rect(.{ .x = r.x + r.w - 8, .y = ty + 2, .w = 5, .h = th - 4 }, c.theme.scrollbar, 2.5, 0, .transparent);
                    }
                }
                if (n.box.clip) s.popClip();
            },
        }
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

    /// pointerMove reports whether the frame must be rebuilt (the pointer
    /// went over another control).
    pub fn pointerMove(c: *Context, x: f32, y: f32) bool {
        c.input.x = x;
        c.input.y = y;
        const h = c.hitTest(x, y);
        if (h == c.input.hot) return false;
        c.input.hot = h;
        return true;
    }

    pub fn pointerDown(c: *Context, x: f32, y: f32) bool {
        _ = c.pointerMove(x, y);
        c.input.active = c.input.hot;
        return c.input.active != 0;
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
                return st.offset != before;
            };
        }
        return false;
    }

    pub fn keyDown(c: *Context, k: Key) bool {
        if (k == .none or c.input.nkeys == c.input.keys.len) return false;
        c.input.keys[c.input.nkeys] = k;
        c.input.nkeys += 1;
        return true;
    }

    // ---- queries, for tests ----

    pub fn find(c: *const Context, label: []const u8) ?Rect {
        for (c.texts.items) |t| {
            if (std.mem.eql(u8, c.strings.items[t.off..][0..t.len], label)) return t.rect;
        }
        return null;
    }

    pub fn hasText(c: *const Context, s: []const u8) bool {
        return c.find(s) != null;
    }
};
