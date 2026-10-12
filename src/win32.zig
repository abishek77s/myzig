//! The Windows backend: a Win32 window whose frames are drawn on the CPU
//! (or the GPU, see gpu/) and blitted with StretchDIBits, one damaged
//! rectangle at a time. DPI aware per monitor; frames that follow each
//! other are paced by the compositor (DwmFlush).

const std = @import("std");
const app = @import("app.zig");
const env = @import("env.zig");
const ui = @import("ui.zig");
const text = @import("text.zig");
const scene = @import("scene.zig");
const raster = @import("raster.zig");
const geom = @import("geom.zig");
const choose = @import("gpu/choose.zig");
const glr = @import("gpu/gl.zig");
const Wgl = @import("gpu/wgl.zig").Wgl;

const WINAPI: std.builtin.CallingConvention = .winapi;
const HWND = *opaque {};
const HDC = *opaque {};
const HINSTANCE = *opaque {};
const HANDLE = ?*anyopaque;
const BOOL = i32;
const WPARAM = usize;
const LPARAM = isize;
const LRESULT = isize;
const WNDPROC = *const fn (HWND, u32, WPARAM, LPARAM) callconv(WINAPI) LRESULT;

const POINT = extern struct { x: i32 = 0, y: i32 = 0 };
const RECT = extern struct { left: i32 = 0, top: i32 = 0, right: i32 = 0, bottom: i32 = 0 };
const MSG = extern struct { hwnd: ?HWND, message: u32, wParam: WPARAM, lParam: LPARAM, time: u32, pt: POINT, lPrivate: u32 };
const WNDCLASSEXW = extern struct {
    cbSize: u32 = @sizeOf(WNDCLASSEXW),
    style: u32 = 0,
    lpfnWndProc: WNDPROC,
    cbClsExtra: i32 = 0,
    cbWndExtra: i32 = 0,
    hInstance: ?HINSTANCE,
    hIcon: HANDLE = null,
    hCursor: HANDLE = null,
    hbrBackground: HANDLE = null,
    lpszMenuName: ?[*:0]const u16 = null,
    lpszClassName: [*:0]const u16,
    hIconSm: HANDLE = null,
};
const PAINTSTRUCT = extern struct { hdc: HDC, fErase: BOOL, rcPaint: RECT, fRestore: BOOL, fIncUpdate: BOOL, rgbReserved: [32]u8 };
const BITMAPINFOHEADER = extern struct {
    biSize: u32 = @sizeOf(BITMAPINFOHEADER),
    biWidth: i32,
    biHeight: i32,
    biPlanes: u16 = 1,
    biBitCount: u16 = 32,
    biCompression: u32 = 0, // BI_RGB
    biSizeImage: u32 = 0,
    biXPelsPerMeter: i32 = 0,
    biYPelsPerMeter: i32 = 0,
    biClrUsed: u32 = 0,
    biClrImportant: u32 = 0,
};
const BITMAPINFO = extern struct { bmiHeader: BITMAPINFOHEADER, bmiColors: [1]u32 = .{0} };
const TRACKMOUSEEVENT = extern struct { cbSize: u32 = @sizeOf(TRACKMOUSEEVENT), dwFlags: u32, hwndTrack: HWND, dwHoverTime: u32 = 0 };

extern "kernel32" fn GetModuleHandleW(name: ?[*:0]const u16) callconv(WINAPI) ?HINSTANCE;
extern "user32" fn RegisterClassExW(c: *const WNDCLASSEXW) callconv(WINAPI) u16;
extern "user32" fn CreateWindowExW(ex: u32, class: [*:0]const u16, title: [*:0]const u16, style: u32, x: i32, y: i32, w: i32, h: i32, parent: ?HWND, menu: HANDLE, inst: ?HINSTANCE, param: ?*anyopaque) callconv(WINAPI) ?HWND;
extern "user32" fn DefWindowProcW(h: HWND, m: u32, w: WPARAM, l: LPARAM) callconv(WINAPI) LRESULT;
extern "user32" fn ShowWindow(h: HWND, cmd: i32) callconv(WINAPI) BOOL;
extern "user32" fn PeekMessageW(m: *MSG, h: ?HWND, min: u32, max: u32, remove: u32) callconv(WINAPI) BOOL;
extern "user32" fn TranslateMessage(m: *const MSG) callconv(WINAPI) BOOL;
extern "user32" fn DispatchMessageW(m: *const MSG) callconv(WINAPI) LRESULT;
extern "user32" fn PostQuitMessage(code: i32) callconv(WINAPI) void;
extern "user32" fn MsgWaitForMultipleObjectsEx(n: u32, handles: ?*const HANDLE, ms: u32, wake: u32, flags: u32) callconv(WINAPI) u32;
extern "user32" fn BeginPaint(h: HWND, ps: *PAINTSTRUCT) callconv(WINAPI) ?HDC;
extern "user32" fn EndPaint(h: HWND, ps: *const PAINTSTRUCT) callconv(WINAPI) BOOL;
extern "user32" fn GetDC(h: ?HWND) callconv(WINAPI) ?HDC;
extern "user32" fn ReleaseDC(h: ?HWND, dc: HDC) callconv(WINAPI) i32;
extern "user32" fn InvalidateRect(h: HWND, r: ?*const RECT, erase: BOOL) callconv(WINAPI) BOOL;
extern "user32" fn LoadCursorW(inst: ?HINSTANCE, name: usize) callconv(WINAPI) HANDLE;
extern "user32" fn SetCapture(h: HWND) callconv(WINAPI) ?HWND;
extern "user32" fn ReleaseCapture() callconv(WINAPI) BOOL;
extern "user32" fn TrackMouseEvent(t: *TRACKMOUSEEVENT) callconv(WINAPI) BOOL;
extern "user32" fn ScreenToClient(h: HWND, p: *POINT) callconv(WINAPI) BOOL;
extern "user32" fn AdjustWindowRectEx(r: *RECT, style: u32, menu: BOOL, ex: u32) callconv(WINAPI) BOOL;
extern "user32" fn SetWindowPos(h: HWND, after: ?HWND, x: i32, y: i32, cx: i32, cy: i32, flags: u32) callconv(WINAPI) BOOL;
extern "user32" fn GetClientRect(h: HWND, r: *RECT) callconv(WINAPI) BOOL;
extern "user32" fn SetProcessDpiAwarenessContext(ctx: isize) callconv(WINAPI) BOOL;
extern "user32" fn GetDpiForWindow(h: HWND) callconv(WINAPI) u32;
extern "user32" fn GetDpiForSystem() callconv(WINAPI) u32;
extern "gdi32" fn StretchDIBits(dc: HDC, xd: i32, yd: i32, wd: i32, hd: i32, xs: i32, ys: i32, ws: i32, hs: i32, bits: ?*const anyopaque, bmi: *const BITMAPINFO, usage: u32, rop: u32) callconv(WINAPI) i32;
extern "winmm" fn timeBeginPeriod(ms: u32) callconv(WINAPI) u32;
extern "dwmapi" fn DwmFlush() callconv(WINAPI) i32;

const WM_DESTROY = 0x0002;
const WM_SIZE = 0x0005;
const WM_PAINT = 0x000F;
const WM_CLOSE = 0x0010;
const WM_QUIT = 0x0012;
const WM_ERASEBKGND = 0x0014;
const WM_KEYDOWN = 0x0100;
const WM_MOUSEMOVE = 0x0200;
const WM_LBUTTONDOWN = 0x0201;
const WM_LBUTTONUP = 0x0202;
const WM_MOUSEWHEEL = 0x020A;
const WM_MOUSELEAVE = 0x02A3;
const WM_DPICHANGED = 0x02E0;
const WS_OVERLAPPEDWINDOW = 0x00CF0000;
const CW_USEDEFAULT: i32 = @bitCast(@as(u32, 0x80000000));
const CS_HREDRAW = 0x2;
const CS_VREDRAW = 0x1;
const CS_OWNDC = 0x20;
const PM_REMOVE = 1;
const QS_ALLINPUT = 0x04FF;
const MWMO_INPUTAVAILABLE = 0x4;
const INFINITE: u32 = 0xFFFFFFFF;
const SRCCOPY = 0x00CC0020;
const TME_LEAVE = 2;
const SWP_NOZORDER = 0x4;
const SWP_NOACTIVATE = 0x10;
const IDC_ARROW = 32512;

pub fn run(state: anytype, comptime view: fn (@TypeOf(state), *ui.Context) void, opts: app.WindowOptions) !void {
    const W = Window(@TypeOf(state), view);
    return W.run(state, opts);
}

fn Window(comptime S: type, comptime view: fn (S, *ui.Context) void) type {
    return struct {
        var g: Self = undefined;
        const Self = @This();

        state: S,
        hwnd: HWND,
        fonts: text.Fonts,
        ctx: ui.Context,
        sc: scene.Scene,
        rend: scene.Renderer,
        pixels: []u32 = &.{},
        width: i32 = 0,
        height: i32 = 0,
        scale: f32 = 1,
        need_frame: bool = true,
        tracking: bool = false,
        stats: app.Stats = .{},
        timer: std.time.Timer,
        wgl: ?Wgl = null,
        gpu: ?glr.Renderer = null,

        fn run(state: S, opts: app.WindowOptions) !void {
            const gpa = std.heap.c_allocator;
            _ = SetProcessDpiAwarenessContext(-4); // per monitor, v2
            const inst = GetModuleHandleW(null);
            const class = std.unicode.utf8ToUtf16LeStringLiteral("MyZigWindow");
            const wc = WNDCLASSEXW{ .style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC, .lpfnWndProc = proc, .hInstance = inst, .hCursor = LoadCursorW(null, IDC_ARROW), .lpszClassName = class };
            if (RegisterClassExW(&wc) == 0) return error.RegisterClass;

            const dpi = GetDpiForSystem();
            const s0 = @as(f32, @floatFromInt(dpi)) / 96;
            var r = RECT{ .right = @intFromFloat(@round(@as(f32, @floatFromInt(opts.width)) * s0)), .bottom = @intFromFloat(@round(@as(f32, @floatFromInt(opts.height)) * s0)) };
            _ = AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, 0, 0);
            const title = try std.unicode.utf8ToUtf16LeAllocZ(gpa, opts.title);
            defer gpa.free(title);

            g = .{ .state = state, .hwnd = undefined, .fonts = try text.Fonts.init(gpa), .ctx = undefined, .sc = .{ .gpa = gpa }, .rend = .{ .gpa = gpa }, .timer = try std.time.Timer.start() };
            g.ctx = ui.Context.init(gpa, &g.fonts);
            g.scale = if (env.get("MYZIG_SCALE")) |v| std.fmt.parseFloat(f32, v) catch s0 else s0;
            g.hwnd = CreateWindowExW(0, class, title.ptr, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, null, null, inst, null) orelse return error.CreateWindow;
            if (env.get("MYZIG_SCALE") == null) g.scale = @as(f32, @floatFromInt(GetDpiForWindow(g.hwnd))) / 96;
            g.openGpu(gpa, env.has("MYZIG_STATS"));
            var cr = RECT{};
            _ = GetClientRect(g.hwnd, &cr);
            g.resize(cr.right, cr.bottom);
            _ = ShowWindow(g.hwnd, 1);

            const uncapped = env.has("MYZIG_UNCAPPED");
            const print_stats = env.has("MYZIG_STATS");
            const exit_after_ns: ?u64 = if (env.get("MYZIG_EXIT_AFTER")) |v| @intFromFloat((std.fmt.parseFloat(f64, v) catch 0) * 1e9) else null;
            var last_print: u64 = 0;
            var window_stats = app.Stats{};
            var msg: MSG = undefined;
            const period: u64 = 16_666_667;
            var next_frame: u64 = 0;
            _ = timeBeginPeriod(1);
            while (true) {
                while (PeekMessageW(&msg, null, 0, 0, PM_REMOVE) != 0) {
                    if (msg.message == WM_QUIT) return;
                    _ = TranslateMessage(&msg);
                    _ = DispatchMessageW(&msg);
                }
                const now = g.timer.read();
                if (exit_after_ns) |limit| if (now >= limit) return;
                if (print_stats and now - last_print >= std.time.ns_per_s) {
                    const st = g.stats;
                    app.printStats("1s", .{
                        .frames = st.frames - window_stats.frames,
                        .build_ns = st.build_ns - window_stats.build_ns,
                        .raster_ns = st.raster_ns - window_stats.raster_ns,
                        .present_ns = st.present_ns - window_stats.present_ns,
                        .pixels = st.pixels - window_stats.pixels,
                    }, now - last_print);
                    window_stats = st;
                    last_print = now;
                }
                if (g.need_frame) {
                    g.need_frame = false;
                    g.frame();
                    if (g.need_frame and !uncapped) {
                        // Frames that follow each other wait for the compositor,
                        // and for the period where it does not wait (no DWM).
                        _ = DwmFlush();
                        const t = g.timer.read();
                        if (t < next_frame) std.Thread.sleep(next_frame - t);
                        next_frame = @max(next_frame + period, g.timer.read() -| period);
                        if (next_frame < g.timer.read()) next_frame = g.timer.read() + period;
                    }
                    continue;
                }
                var timeout: u32 = INFINITE;
                if (print_stats) timeout = 1000;
                if (exit_after_ns != null) timeout = @min(timeout, 50);
                _ = MsgWaitForMultipleObjectsEx(0, null, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
        }

        fn openGpu(w: *Self, gpa: std.mem.Allocator, verbose: bool) void {
            const choice = choose.choose();
            if (choice.kind == .cpu) return;
            if (choice.kind == .vulkan) std.log.warn("myzig: Vulkan is not ready on Windows yet; drawing with OpenGL", .{});
            const dc = GetDC(w.hwnd) orelse return;
            w.wgl = Wgl.open(dc) catch |e| {
                std.log.warn("myzig: no OpenGL ({s}); drawing on the CPU", .{@errorName(e)});
                return;
            };
            const gl = glr.Gl.load(&w.wgl.?, Wgl.proc) catch return w.closeGpu();
            w.gpu = glr.Renderer.init(gpa, gl) catch return w.closeGpu();
            const name = w.gpu.?.name();
            if (!choice.forced and glr.software(name)) return w.closeGpu();
            if (verbose) std.debug.print("myzig: drawing with OpenGL on {s}\n", .{name});
            w.rend.raster = false;
            w.rend.retain_pixels = false;
        }

        fn closeGpu(w: *Self) void {
            if (w.gpu) |*r| r.deinit();
            w.gpu = null;
            if (w.wgl) |*x| x.release();
            w.wgl = null;
        }

        fn resize(w: *Self, cw: i32, ch: i32) void {
            const nw = @max(1, cw);
            const nh = @max(1, ch);
            if (nw == w.width and nh == w.height) return;
            if (w.pixels.len > 0) std.heap.c_allocator.free(w.pixels);
            w.pixels = std.heap.c_allocator.alloc(u32, @intCast(nw * nh)) catch &.{};
            w.width = nw;
            w.height = nh;
            w.rend.invalidate();
            w.need_frame = true;
        }

        fn frame(w: *Self) void {
            if (w.pixels.len == 0) return;
            const t0 = w.timer.read();
            const lw = @as(f32, @floatFromInt(w.width)) / w.scale;
            const lh = @as(f32, @floatFromInt(w.height)) / w.scale;
            w.ctx.build(w.state, view, lw, lh, t0);
            w.sc.reset(w.width, w.height, w.scale, w.ctx.theme.background);
            w.ctx.endFrame(&w.sc);
            const t1 = w.timer.read();
            const cv = raster.Canvas{ .pixels = w.pixels, .width = w.width, .height = w.height, .stride = @intCast(w.width) };
            const px = w.rend.stats.pixels;
            _ = w.rend.render(&w.sc, &w.fonts, &cv);
            const t2 = w.timer.read();
            if (w.gpu) |*r| {
                if (w.rend.damage.items.len > 0) {
                    r.draw(&w.sc, &w.fonts, w.width, w.height) catch |e| std.log.err("myzig: OpenGL frame: {s}", .{@errorName(e)});
                    w.wgl.?.swap();
                }
            } else if (GetDC(w.hwnd)) |dc| {
                // The pixels of scrolled layers moved in the canvas: they go too.
                for (w.rend.scrolls.items) |s| w.blit(dc, s.clip);
                for (w.rend.damage.items) |d| w.blit(dc, d);
                _ = ReleaseDC(w.hwnd, dc);
            }
            w.stats.frames += 1;
            w.stats.build_ns += t1 - t0;
            w.stats.raster_ns += t2 - t1;
            w.stats.present_ns += w.timer.read() - t2;
            w.stats.pixels += w.rend.stats.pixels - px;
            if (w.ctx.animating or w.ctx.requested) w.need_frame = true;
        }

        fn blit(w: *Self, dc: HDC, r: geom.IRect) void {
            const d = r.intersect(.{ .x1 = w.width, .y1 = w.height });
            if (d.isEmpty()) return;
            const bmi = BITMAPINFO{ .bmiHeader = .{ .biWidth = w.width, .biHeight = -w.height } };
            // A top-down DIB: rows count from the top, as the canvas's do.
            _ = StretchDIBits(dc, d.x0, d.y0, d.x1 - d.x0, d.y1 - d.y0, d.x0, w.height - d.y1, d.x1 - d.x0, d.y1 - d.y0, w.pixels.ptr, &bmi, 0, SRCCOPY);
        }

        fn point(w: *Self, l: LPARAM) [2]f32 {
            const x: i16 = @truncate(l);
            const y: i16 = @truncate(l >> 16);
            return .{ @as(f32, @floatFromInt(x)) / w.scale, @as(f32, @floatFromInt(y)) / w.scale };
        }

        fn proc(hwnd: HWND, m: u32, wp: WPARAM, lp: LPARAM) callconv(WINAPI) LRESULT {
            const w = &g;
            switch (m) {
                WM_CLOSE, WM_DESTROY => {
                    PostQuitMessage(0);
                    return 0;
                },
                WM_ERASEBKGND => return 1,
                WM_SIZE => {
                    const cw: i32 = @intCast(lp & 0xffff);
                    const ch: i32 = @intCast((lp >> 16) & 0xffff);
                    if (w.hwnd == hwnd) w.resize(cw, ch);
                    return 0;
                },
                WM_PAINT => {
                    var ps: PAINTSTRUCT = undefined;
                    const dc = BeginPaint(hwnd, &ps) orelse return 0;
                    if (w.hwnd == hwnd) {
                        // Live resizing runs a loop of its own: draw here.
                        if (w.gpu != null) w.rend.invalidate();
                        if (!w.rend.valid) {
                            w.need_frame = false;
                            w.frame();
                        }
                        if (w.gpu == null) w.blit(dc, .{ .x0 = ps.rcPaint.left, .y0 = ps.rcPaint.top, .x1 = ps.rcPaint.right, .y1 = ps.rcPaint.bottom });
                    }
                    _ = EndPaint(hwnd, &ps);
                    return 0;
                },
                WM_DPICHANGED => {
                    w.scale = @as(f32, @floatFromInt(wp & 0xffff)) / 96;
                    const r: *const RECT = @ptrFromInt(@as(usize, @bitCast(lp)));
                    _ = SetWindowPos(hwnd, null, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
                    w.rend.invalidate();
                    w.need_frame = true;
                    return 0;
                },
                WM_MOUSEMOVE => {
                    if (!w.tracking) {
                        var t = TRACKMOUSEEVENT{ .dwFlags = TME_LEAVE, .hwndTrack = hwnd };
                        w.tracking = TrackMouseEvent(&t) != 0;
                    }
                    const p = w.point(lp);
                    if (w.ctx.pointerMove(p[0], p[1])) w.need_frame = true;
                    return 0;
                },
                WM_MOUSELEAVE => {
                    w.tracking = false;
                    if (w.ctx.pointerMove(-1, -1)) w.need_frame = true;
                    return 0;
                },
                WM_LBUTTONDOWN => {
                    _ = SetCapture(hwnd);
                    const p = w.point(lp);
                    if (w.ctx.pointerDown(p[0], p[1])) w.need_frame = true;
                    return 0;
                },
                WM_LBUTTONUP => {
                    _ = ReleaseCapture();
                    const p = w.point(lp);
                    if (w.ctx.pointerUp(p[0], p[1])) w.need_frame = true;
                    return 0;
                },
                WM_MOUSEWHEEL => {
                    const delta: i16 = @bitCast(@as(u16, @truncate(wp >> 16)));
                    var pt = POINT{ .x = @as(i16, @truncate(lp)), .y = @as(i16, @truncate(lp >> 16)) };
                    _ = ScreenToClient(hwnd, &pt);
                    const dy = -@as(f32, @floatFromInt(delta)) / 120 * 48;
                    if (w.ctx.scroll(@as(f32, @floatFromInt(pt.x)) / w.scale, @as(f32, @floatFromInt(pt.y)) / w.scale, dy)) w.need_frame = true;
                    return 0;
                },
                WM_KEYDOWN => {
                    if (w.ctx.keyDown(mapKey(wp))) w.need_frame = true;
                    return 0;
                },
                else => return DefWindowProcW(hwnd, m, wp, lp),
            }
        }
    };
}

fn mapKey(vk: WPARAM) ui.Key {
    return switch (vk) {
        0x26 => .up,
        0x28 => .down,
        0x25 => .left,
        0x27 => .right,
        0x0D => .enter,
        0x1B => .escape,
        0x20 => .space,
        0x21 => .page_up,
        0x22 => .page_down,
        0x24 => .home,
        0x23 => .end,
        0x09 => .tab,
        0x08 => .backspace,
        else => .none,
    };
}
