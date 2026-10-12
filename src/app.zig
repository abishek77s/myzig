//! The app's event loop and its window on X11.
//!
//! The loop sleeps in poll(2) on the X connection: an idle window wakes for
//! nothing. A frame is built when input changed what the view shows, when
//! the window was exposed or resized, or every display refresh while the
//! view animates. Each frame presents only the rectangles the renderer
//! redrew, from shared memory, and the next frame waits for the server to
//! have read them.

const std = @import("std");
const env = @import("env.zig");
const x = @import("x11.zig");
const ui = @import("ui.zig");
const text = @import("text.zig");
const scene = @import("scene.zig");
const raster = @import("raster.zig");
const geom = @import("geom.zig");
const choose = @import("gpu/choose.zig");
const backend = @import("gpu/backend.zig");
const glr = backend.glr;
const vulkan = backend.vulkan;
const Glx = @import("gpu/glx.zig").Glx;

pub const WindowOptions = struct {
    title: [:0]const u8 = "MyZig",
    width: u32 = 800,
    height: u32 = 600,
};

/// Stats are what MYZIG_STATS prints each second, and what benchmarks read.
pub const Stats = struct {
    frames: u64 = 0,
    build_ns: u64 = 0,
    raster_ns: u64 = 0,
    present_ns: u64 = 0,
    pixels: u64 = 0,
};

const Buffer = struct {
    img: ?*x.XImage = null,
    shm: x.ShmSegmentInfo = .{},
    owned: []u32 = &.{},
    width: i32 = 0,
    height: i32 = 0,
    stride: usize = 0,

    fn pixels(b: *Buffer) []u32 {
        const img = b.img orelse return &.{};
        const p: [*]u32 = @ptrCast(@alignCast(img.data.?));
        return p[0 .. b.stride * @as(usize, @intCast(b.height))];
    }
};

pub fn run(state: anytype, comptime view: fn (@TypeOf(state), *ui.Context) void, opts: WindowOptions) !void {
    const gpa = std.heap.c_allocator;
    const dpy = x.XOpenDisplay(null) orelse return error.NoDisplay;
    defer _ = x.XCloseDisplay(dpy);
    const screen = x.XDefaultScreen(dpy);
    const root = x.XRootWindow(dpy, screen);
    var visual = x.XDefaultVisual(dpy, screen);
    var depth = x.XDefaultDepth(dpy, screen);
    if (depth != 24 and depth != 32) return error.UnsupportedDepth;

    // OpenGL needs its visual before the window exists; Vulkan draws to
    // a window of any, so the window takes OpenGL's when it could be
    // the one that draws.
    const choice = choose.choose();
    var glx: ?Glx = null;
    var vi: ?*x.XVisualInfo = null;
    if (choice.kind == .gl or (choice.kind == .vulkan and !choice.forced)) {
        if (Glx.open()) |g| {
            glx = g;
            vi = glx.?.visual(dpy, screen) catch null;
            if (vi) |v| {
                visual = v.visual;
                depth = v.depth;
            }
        } else |_| {}
    }

    const scale: f32 = if (env.get("MYZIG_SCALE")) |s| std.fmt.parseFloat(f32, s) catch 1 else 1;
    const uncapped = env.get("MYZIG_UNCAPPED") != null;
    const print_stats = env.get("MYZIG_STATS") != null;
    const exit_after_ns: ?u64 = if (env.get("MYZIG_EXIT_AFTER")) |s| @intFromFloat((std.fmt.parseFloat(f64, s) catch 0) * 1e9) else null;

    var w: i32 = @intFromFloat(@round(@as(f32, @floatFromInt(opts.width)) * scale));
    var h: i32 = @intFromFloat(@round(@as(f32, @floatFromInt(opts.height)) * scale));
    var attrs = x.XSetWindowAttributes{ .background_pixel = 0xffffff, .colormap = x.XCreateColormap(dpy, root, visual, x.AllocNone) };
    const win = x.XCreateWindow(dpy, root, 0, 0, @intCast(w), @intCast(h), 0, depth, x.InputOutput, visual, x.CWBackPixel | x.CWBorderPixel | x.CWColormap, &attrs);
    _ = x.XStoreName(dpy, win, opts.title.ptr);
    var wm_delete = x.XInternAtom(dpy, "WM_DELETE_WINDOW", 0);
    _ = x.XSetWMProtocols(dpy, win, @ptrCast(&wm_delete), 1);
    _ = x.XSelectInput(dpy, win, x.KeyPressMask | x.ButtonPressMask | x.ButtonReleaseMask | x.PointerMotionMask | x.LeaveWindowMask | x.ExposureMask | x.StructureNotifyMask | x.FocusChangeMask);
    // Text comes through an input method, in UTF-8, where there is one;
    // it is opened when a text input first takes the focus (it loads the
    // compose tables, a megabyte an app without text input does without).
    var im: ?*anyopaque = null;
    var ic: ?*anyopaque = null;
    var im_tried = false;
    defer if (ic) |i| x.XDestroyIC(i);
    defer if (im) |m| {
        _ = x.XCloseIM(m);
    };
    // Cursors are made when first shown (making one loads the cursor
    // theme); the arrow is the window's default, none of its own.
    var cursors = [_]c_ulong{ 0, 0, 0 };
    const cursor_shapes = [_]c_uint{ x.XC_left_ptr, x.XC_xterm, x.XC_hand2 };
    var cursor: ui.Cursor = .arrow;
    _ = x.XMapWindow(dpy, win);
    const gc = x.XCreateGC(dpy, win, 0, null);

    const use_shm = x.XShmQueryExtension(dpy) != 0 and env.get("MYZIG_NO_SHM") == null;
    const shm_completion = if (use_shm) x.XShmGetEventBase(dpy) else -1;

    var fonts = try text.Fonts.init(gpa);
    defer fonts.deinit();
    var ctx = ui.Context.init(gpa, &fonts);
    defer ctx.deinit();
    var clip = Clipboard.init(gpa, dpy, win);
    defer clip.deinit();
    ctx.clipboard = clip.interface();
    var sc = scene.Scene{ .gpa = gpa };
    defer sc.deinit();
    var rend = scene.Renderer{ .gpa = gpa, .retain_pixels = false };
    defer rend.deinit();

    // The GPU renderer, where one draws: Vulkan's, else OpenGL's. One
    // that draws on the CPU is not used unless it was asked for.
    var gpu: ?backend.Gpu = null;
    defer if (gpu) |*g| g.deinit();
    if (choice.kind == .vulkan) vk: {
        var r = vulkan.Renderer.init(gpa, .{ .xlib = .{ .dpy = dpy, .window = win } }, w, h, .{
            .vsync = !uncapped,
            .validation = env.has("MYZIG_VK_VALIDATION"),
        }) catch |e| {
            if (choice.forced) std.log.warn("myzig: no Vulkan ({s}); drawing on the CPU", .{@errorName(e)});
            break :vk;
        };
        if (!choice.forced and r.software()) {
            r.deinit();
            break :vk;
        }
        gpu = .{ .vulkan = r };
    }
    if (gpu == null and glx != null and vi != null) gpu: {
        glx.?.context(dpy, win) catch |e| {
            std.log.warn("myzig: no OpenGL context ({s}); drawing on the CPU", .{@errorName(e)});
            break :gpu;
        };
        const gl = glr.Gl.load(&glx.?, Glx.proc) catch break :gpu;
        var r = glr.Renderer.init(gpa, gl) catch break :gpu;
        if (!choice.forced and glr.software(r.name())) {
            r.deinit();
            glx.?.release(dpy);
            break :gpu;
        }
        gpu = .{ .gl = r };
    }
    if (gpu) |*g| if (print_stats) std.debug.print("myzig: drawing with {s} on {s}\n", .{ g.api(), g.name() });
    defer if (glx) |*g| g.release(dpy);
    if (gpu != null) rend.raster = false;
    const dump = env.get("MYZIG_DUMP");
    if (dump != null) if (gpu) |*g| g.capture();

    var buf = Buffer{};
    if (gpu == null) try resize(dpy, visual, depth, use_shm, &buf, w, h);
    buf.width = w;
    buf.height = h;
    defer freeBuffer(dpy, &buf);

    var stats = Stats{};
    var window_stats = Stats{};
    var timer = try std.time.Timer.start();
    var last_print: u64 = 0;
    const period: u64 = 16_666_667;
    var next_frame: u64 = 0;
    var need_frame = true;
    var presenting = false;
    var mapped = false;
    var ev: x.XEvent = undefined;
    const fd = x.XConnectionNumber(dpy);

    while (true) {
        // Drain the events the server sent.
        while (x.XPending(dpy) > 0) {
            _ = x.XNextEvent(dpy, &ev);
            if (x.XFilterEvent(&ev, 0) != 0) continue; // the input method's
            const k = scale;
            switch (ev.type) {
                x.Expose, x.GraphicsExpose => {
                    mapped = true;
                    rend.invalidate();
                    need_frame = true;
                },
                x.ConfigureNotify => {
                    const c = ev.configure;
                    if (c.width != w or c.height != h) {
                        w = c.width;
                        h = c.height;
                        if (gpu == null) try resize(dpy, visual, depth, use_shm, &buf, w, h);
                        buf.width = w;
                        buf.height = h;
                        rend.invalidate();
                        need_frame = true;
                    }
                },
                x.MotionNotify => {
                    if (ctx.pointerMove(@as(f32, @floatFromInt(ev.motion.x)) / k, @as(f32, @floatFromInt(ev.motion.y)) / k)) need_frame = true;
                },
                x.LeaveNotify => {
                    if (ctx.pointerMove(-1, -1)) need_frame = true;
                },
                x.ButtonPress => {
                    ctx.setMods(modsOf(ev.button.state));
                    const px = @as(f32, @floatFromInt(ev.button.x)) / k;
                    const py = @as(f32, @floatFromInt(ev.button.y)) / k;
                    switch (ev.button.detail) {
                        1 => if (ctx.pointerDown(px, py)) {
                            need_frame = true;
                        },
                        4 => if (ctx.scroll(px, py, -48)) {
                            need_frame = true;
                        },
                        5 => if (ctx.scroll(px, py, 48)) {
                            need_frame = true;
                        },
                        else => {},
                    }
                },
                x.ButtonRelease => if (ev.button.detail == 1) {
                    // A click is reported by the next frame the view builds.
                    if (ctx.pointerUp(@as(f32, @floatFromInt(ev.button.x)) / k, @as(f32, @floatFromInt(ev.button.y)) / k)) need_frame = true;
                },
                x.KeyPress => {
                    const mods = modsOf(ev.key.state);
                    if (ctx.keyDownMods(mapKey(x.XLookupKeysym(&ev.key, 0)), mods)) need_frame = true;
                    if (!mods.ctrl and !mods.alt and !mods.super) {
                        var tb: [64]u8 = undefined;
                        const n = keyText(ic, &ev.key, &tb);
                        if (n > 0 and ctx.typeText(tb[0..n])) need_frame = true;
                    }
                },
                x.FocusIn => if (ic) |i| x.XSetICFocus(i),
                x.FocusOut => if (ic) |i| x.XUnsetICFocus(i),
                x.SelectionRequest => clip.answer(&ev.selection_request),
                x.SelectionClear => clip.owned.clearRetainingCapacity(),
                x.ClientMessage => {
                    if (@as(x.Atom, @bitCast(ev.client.data[0])) == wm_delete) return;
                },
                else => if (ev.type == shm_completion) {
                    presenting = false;
                },
            }
        }

        const now = timer.read();
        if (exit_after_ns) |limit| if (now >= limit) {
            if (print_stats) printStats("final", stats, now);
            return;
        };
        if (print_stats and now - last_print >= std.time.ns_per_s) {
            const d = Stats{
                .frames = stats.frames - window_stats.frames,
                .build_ns = stats.build_ns - window_stats.build_ns,
                .raster_ns = stats.raster_ns - window_stats.raster_ns,
                .present_ns = stats.present_ns - window_stats.present_ns,
                .pixels = stats.pixels - window_stats.pixels,
            };
            printStats("1s", d, now - last_print);
            window_stats = stats;
            last_print = now;
        }

        if (need_frame and mapped and !presenting and (uncapped or now >= next_frame)) {
            need_frame = false;
            buildAndDraw(state, view, &ctx, &sc, &rend, &fonts, &buf, scale, &timer, &stats);
            if (gpu) |*g| {
                // The whole frame, when anything in it changed.
                if (rend.damage.items.len > 0) {
                    const t0 = timer.read();
                    g.draw(&sc, &fonts, w, h) catch |e| std.log.err("myzig: {s} frame: {s}", .{ g.api(), @errorName(e) });
                    if (dump) |path| {
                        backend.writePPM(gpa, g, w, h, path) catch {};
                        return;
                    }
                    if (g.* == .gl) glx.?.swapBuffers(dpy, win);
                    stats.raster_ns += timer.read() - t0;
                }
            } else {
                presentDamage(dpy, win, gc, &buf, rend.scrolls.items, rend.damage.items, use_shm, &stats, &timer);
                if (dump) |path| {
                    writePixels(path, buf.pixels(), w, h, buf.stride) catch {};
                    return;
                }
                presenting = use_shm and rend.damage.items.len > 0;
            }
            if (ctx.animating or ctx.requested) need_frame = true;
            if (ctx.input.text_focus and !im_tried) {
                im_tried = true;
                _ = x.setlocale(0, ""); // LC_CTYPE
                _ = x.XSetLocaleModifiers("");
                im = x.XOpenIM(dpy, null, null, null);
                if (im) |m| ic = x.XCreateIC(m, "inputStyle", @as(c_long, x.XIMPreeditNothing | x.XIMStatusNothing), "clientWindow", win, "focusWindow", win, @as(?*anyopaque, null));
                if (ic) |i| x.XSetICFocus(i);
            }
            if (ctx.cursor != cursor) {
                cursor = ctx.cursor;
                const ci = @intFromEnum(cursor);
                if (cursor != .arrow and cursors[ci] == 0) cursors[ci] = x.XCreateFontCursor(dpy, cursor_shapes[ci]);
                _ = x.XDefineCursor(dpy, win, cursors[ci]);
            }
            next_frame = if (now - next_frame < period) next_frame + period else now + period;
            continue;
        }

        // A frame the view asked for at a time (a caret's blink).
        if (ctx.wake_at != 0 and now >= ctx.wake_at) {
            ctx.wake_at = 0;
            need_frame = true;
            continue;
        }
        // Sleep until the server writes, or the next frame is due.
        var timeout: i32 = -1;
        if (ctx.wake_at != 0) timeout = @intCast(@divTrunc(ctx.wake_at - now + 999_999, 1_000_000));
        if (need_frame and !presenting and !uncapped) timeout = @intCast(@max(0, @divTrunc(@as(i64, @intCast(next_frame)) - @as(i64, @intCast(now)) + 999_999, 1_000_000)));
        if (exit_after_ns) |limit| {
            const left: i32 = @intCast(@max(0, @divTrunc(@as(i64, @intCast(limit)) - @as(i64, @intCast(now)), 1_000_000) + 1));
            timeout = if (timeout < 0) left else @min(timeout, left);
        }
        if (print_stats) timeout = if (timeout < 0) 1000 else @min(timeout, 1000);
        if (need_frame and !presenting and uncapped) timeout = 0;
        var pfd = [_]std.posix.pollfd{.{ .fd = fd, .events = std.posix.POLL.IN, .revents = 0 }};
        if (x.XPending(dpy) == 0) _ = std.posix.poll(&pfd, timeout) catch {};
    }
}

pub fn buildAndDraw(state: anytype, comptime view: anytype, ctx: *ui.Context, sc: *scene.Scene, rend: *scene.Renderer, fonts: *text.Fonts, buf: *Buffer, scale: f32, timer: *std.time.Timer, stats: *Stats) void {
    const t0 = timer.read();
    const lw = @as(f32, @floatFromInt(buf.width)) / scale;
    const lh = @as(f32, @floatFromInt(buf.height)) / scale;
    ctx.build(state, view, lw, lh, t0);
    sc.reset(buf.width, buf.height, scale, ctx.theme.background);
    ctx.endFrame(sc);
    const t1 = timer.read();
    const cv = raster.Canvas{ .pixels = buf.pixels(), .width = buf.width, .height = buf.height, .stride = buf.stride };
    const px_before = rend.stats.pixels;
    _ = rend.render(sc, fonts, &cv);
    const t2 = timer.read();
    stats.frames += 1;
    stats.build_ns += t1 - t0;
    stats.raster_ns += t2 - t1;
    stats.pixels += rend.stats.pixels - px_before;
}

fn presentDamage(dpy: *x.Display, win: x.Window, gc: x.GC, buf: *Buffer, scrolls: []const scene.Scroll, damage: []const geom.IRect, use_shm: bool, stats: *Stats, timer: *std.time.Timer) void {
    const t0 = timer.read();
    const img = buf.img orelse return;
    // Scrolled layers move on the server: nothing crosses the connection.
    for (scrolls) |sc| {
        const c = sc.clip;
        const w: c_uint = @intCast(c.x1 - c.x0);
        const h: c_uint = @intCast(c.y1 - c.y0 - @as(i32, @intCast(@abs(sc.dy))));
        if (sc.dy > 0) {
            _ = x.XCopyArea(dpy, win, win, gc, c.x0, c.y0 + sc.dy, w, h, c.x0, c.y0);
        } else {
            _ = x.XCopyArea(dpy, win, win, gc, c.x0, c.y0, w, h, c.x0, c.y0 - sc.dy);
        }
    }
    for (damage, 0..) |d, i| {
        if (d.isEmpty()) continue;
        const last = i == damage.len - 1;
        if (use_shm) {
            _ = x.XShmPutImage(dpy, win, gc, img, d.x0, d.y0, d.x0, d.y0, @intCast(d.x1 - d.x0), @intCast(d.y1 - d.y0), @intFromBool(last));
        } else {
            _ = x.XPutImage(dpy, win, gc, img, d.x0, d.y0, d.x0, d.y0, @intCast(d.x1 - d.x0), @intCast(d.y1 - d.y0));
        }
    }
    _ = x.XFlush(dpy);
    stats.present_ns += timer.read() - t0;
}

fn resize(dpy: *x.Display, visual: *x.Visual, depth: c_int, use_shm: bool, buf: *Buffer, w: i32, h: i32) !void {
    freeBuffer(dpy, buf);
    const uw: c_uint = @intCast(@max(1, w));
    const uh: c_uint = @intCast(@max(1, h));
    if (use_shm) {
        const img = x.XShmCreateImage(dpy, visual, @intCast(depth), x.ZPixmap, null, &buf.shm, uw, uh) orelse return error.ShmImage;
        const size: usize = @as(usize, @intCast(img.bytes_per_line)) * uh;
        buf.shm.shmid = x.shmget(x.IPC_PRIVATE, size, x.IPC_CREAT | 0o600);
        if (buf.shm.shmid < 0) return error.Shm;
        const addr = x.shmat(buf.shm.shmid, null, 0) orelse return error.Shm;
        buf.shm.shmaddr = @ptrCast(addr);
        img.data = @ptrCast(addr);
        _ = x.XShmAttach(dpy, &buf.shm);
        _ = x.XSync(dpy, 0);
        // The segment goes once both sides detach.
        _ = x.shmctl(buf.shm.shmid, x.IPC_RMID, null);
        buf.img = img;
        buf.stride = @intCast(@divExact(img.bytes_per_line, 4));
    } else {
        const owned = try std.heap.c_allocator.alloc(u32, uw * uh);
        buf.owned = owned;
        buf.img = x.XCreateImage(dpy, visual, @intCast(depth), x.ZPixmap, 0, @ptrCast(owned.ptr), uw, uh, 32, 0) orelse return error.Image;
        buf.stride = uw;
    }
    buf.width = @intCast(uw);
    buf.height = @intCast(uh);
}

fn freeBuffer(dpy: *x.Display, buf: *Buffer) void {
    const img = buf.img orelse return;
    if (buf.shm.shmaddr) |addr| {
        _ = x.XShmDetach(dpy, &buf.shm);
        _ = x.XSync(dpy, 0);
        _ = x.shmdt(addr);
        buf.shm = .{};
    }
    img.data = null;
    _ = x.XFree(img);
    if (buf.owned.len > 0) std.heap.c_allocator.free(buf.owned);
    buf.* = .{};
}

fn modsOf(state: c_uint) ui.Mods {
    return .{ .shift = state & x.ShiftMask != 0, .ctrl = state & x.ControlMask != 0, .alt = state & x.Mod1Mask != 0, .super = state & x.Mod4Mask != 0 };
}

/// keyText is the text of a key press, UTF-8, through the input method or,
/// without one, from its Latin-1 keysym text.
fn keyText(ic: ?*anyopaque, ev: *x.InputEvent, out: *[64]u8) usize {
    if (ic) |i| {
        var status: c_int = 0;
        const n = x.Xutf8LookupString(i, ev, out, out.len, null, &status);
        return if (n > 0 and status != x.XBufferOverflow) @intCast(n) else 0;
    }
    var latin: [16]u8 = undefined;
    const n: usize = @intCast(@max(0, x.XLookupString(ev, &latin, latin.len, null, null)));
    var o: usize = 0;
    for (latin[0..n]) |b| o += std.unicode.utf8Encode(b, out[o..]) catch break;
    return o;
}

fn mapKey(sym: c_ulong) ui.Key {
    if (sym >= 'a' and sym <= 'z') return ui.Key.letter(@intCast(sym));
    if (sym >= 'A' and sym <= 'Z') return ui.Key.letter(@intCast(sym));
    return switch (sym) {
        0xffff => .delete,
        0xff8d => .enter,
        0xfe20 => .tab, // Shift+Tab's
        0xff52 => .up,
        0xff54 => .down,
        0xff51 => .left,
        0xff53 => .right,
        0xff0d => .enter,
        0xff1b => .escape,
        0x20 => .space,
        0xff55 => .page_up,
        0xff56 => .page_down,
        0xff50 => .home,
        0xff57 => .end,
        0xff09 => .tab,
        0xff08 => .backspace,
        else => .none,
    };
}

/// writePixels writes a canvas of 0x00RRGGBB pixels to path, as a PPM
/// image: what MYZIG_DUMP=path saves of a window's first frame.
pub fn writePixels(path: []const u8, pixels: []const u32, w: i32, h: i32, stride: usize) !void {
    var f = try std.fs.cwd().createFile(path, .{});
    defer f.close();
    var wbuf: [64 << 10]u8 = undefined;
    var fw = f.writer(&wbuf);
    try fw.interface.print("P6\n{d} {d}\n255\n", .{ w, h });
    for (0..@intCast(h)) |y| for (pixels[y * stride ..][0..@intCast(w)]) |p| {
        try fw.interface.writeAll(&.{ @truncate(p >> 16), @truncate(p >> 8), @truncate(p) });
    };
    try fw.interface.flush();
}

pub fn printStats(label: []const u8, s: Stats, elapsed_ns: u64) void {
    const secs = @as(f64, @floatFromInt(elapsed_ns)) / 1e9;
    const f = @as(f64, @floatFromInt(@max(1, s.frames)));
    std.debug.print("myzig {s}: fps={d:.1} build={d:.3}ms raster={d:.3}ms present={d:.3}ms px/frame={d:.0}\n", .{
        label,
        @as(f64, @floatFromInt(s.frames)) / secs,
        @as(f64, @floatFromInt(s.build_ns)) / f / 1e6,
        @as(f64, @floatFromInt(s.raster_ns)) / f / 1e6,
        @as(f64, @floatFromInt(s.present_ns)) / f / 1e6,
        @as(f64, @floatFromInt(s.pixels)) / f,
    });
}

/// Clipboard is X11's CLIPBOARD selection: copying owns it, and the window
/// answers the requests of the apps that paste; pasting asks its owner.
const Clipboard = struct {
    gpa: std.mem.Allocator,
    dpy: *x.Display,
    win: x.Window,
    clipboard: x.Atom,
    utf8: x.Atom,
    targets: x.Atom,
    text_atom: x.Atom,
    property: x.Atom,
    owned: std.ArrayListUnmanaged(u8) = .empty,

    fn init(gpa: std.mem.Allocator, dpy: *x.Display, win: x.Window) Clipboard {
        return .{
            .gpa = gpa,
            .dpy = dpy,
            .win = win,
            .clipboard = x.XInternAtom(dpy, "CLIPBOARD", 0),
            .utf8 = x.XInternAtom(dpy, "UTF8_STRING", 0),
            .targets = x.XInternAtom(dpy, "TARGETS", 0),
            .text_atom = x.XInternAtom(dpy, "TEXT", 0),
            .property = x.XInternAtom(dpy, "MYZIG_CLIPBOARD", 0),
        };
    }

    fn deinit(c: *Clipboard) void {
        c.owned.deinit(c.gpa);
    }

    fn interface(c: *Clipboard) ui.Clipboard {
        return .{ .ctx = c, .get = get, .set = set };
    }

    fn set(ctx: *anyopaque, s: []const u8) void {
        const c: *Clipboard = @ptrCast(@alignCast(ctx));
        c.owned.clearRetainingCapacity();
        c.owned.appendSlice(c.gpa, s) catch return;
        _ = x.XSetSelectionOwner(c.dpy, c.clipboard, c.win, 0);
    }

    fn get(ctx: *anyopaque, gpa: std.mem.Allocator) ?[]u8 {
        const c: *Clipboard = @ptrCast(@alignCast(ctx));
        const owner = x.XGetSelectionOwner(c.dpy, c.clipboard);
        if (owner == c.win) return gpa.dupe(u8, c.owned.items) catch null;
        if (owner == 0) return null;
        _ = x.XConvertSelection(c.dpy, c.clipboard, c.utf8, c.property, c.win, 0);
        _ = x.XFlush(c.dpy);
        // The owner answers with SelectionNotify; wait for it, briefly.
        var ev: x.XEvent = undefined;
        var waited: u32 = 0;
        while (x.XCheckTypedWindowEvent(c.dpy, c.win, x.SelectionNotify, &ev) == 0) : (waited += 1) {
            if (waited > 250) return null;
            std.Thread.sleep(std.time.ns_per_ms);
        }
        if (ev.selection.property == 0) return null;
        var actual: x.Atom = 0;
        var format: c_int = 0;
        var n: c_ulong = 0;
        var after: c_ulong = 0;
        var data: ?[*]u8 = null;
        if (x.XGetWindowProperty(c.dpy, c.win, c.property, 0, 1 << 20, 1, x.AnyPropertyType, &actual, &format, &n, &after, &data) != 0) return null;
        const p = data orelse return null;
        defer _ = x.XFree(p);
        if (format != 8) return null;
        return gpa.dupe(u8, p[0..n]) catch null;
    }

    /// answer gives the text owned to the app that asked for it.
    fn answer(c: *Clipboard, req: *const x.SelectionRequestEvent) void {
        var reply = x.XEvent{ .selection = .{
            .type = x.SelectionNotify,
            .serial = 0,
            .send_event = 1,
            .display = c.dpy,
            .requestor = req.requestor,
            .selection = req.selection,
            .target = req.target,
            .property = 0,
            .time = req.time,
        } };
        const prop = if (req.property != 0) req.property else req.target;
        if (req.target == c.targets) {
            const atoms = [_]x.Atom{ c.targets, c.utf8, x.XA_STRING, c.text_atom };
            _ = x.XChangeProperty(c.dpy, req.requestor, prop, x.XA_ATOM, 32, x.PropModeReplace, @ptrCast(&atoms), atoms.len);
            reply.selection.property = prop;
        } else if (req.target == c.utf8 or req.target == x.XA_STRING or req.target == c.text_atom) {
            _ = x.XChangeProperty(c.dpy, req.requestor, prop, if (req.target == c.text_atom) c.utf8 else req.target, 8, x.PropModeReplace, c.owned.items.ptr, @intCast(c.owned.items.len));
            reply.selection.property = prop;
        }
        _ = x.XSendEvent(c.dpy, req.requestor, 0, 0, &reply);
        _ = x.XFlush(c.dpy);
    }
};
