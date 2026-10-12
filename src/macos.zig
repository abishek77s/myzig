//! The app's event loop and its window on macOS, in Cocoa.
//!
//! The window's content is an NSView of MyZig's own class, hosting a
//! CAMetalLayer that the Metal renderer draws to (or a CALayer showing the
//! CPU renderer's pixels). The loop waits in nextEventMatchingMask until
//! an event comes or the next frame is due; an idle window wakes for
//! nothing. Live resizing runs a loop of AppKit's own, so the window
//! draws from windowDidResize too.
//!
//! Shortcuts follow the Mac: Command is the modifier that copies, pastes
//! and selects (what Control is elsewhere), Option moves by words, and
//! Command with an arrow goes to the line's start or end.

const std = @import("std");
const objc = @import("mac/objc.zig");
const env = @import("env.zig");
const ui = @import("ui.zig");
const text = @import("text.zig");
const scene = @import("scene.zig");
const raster = @import("raster.zig");
const app = @import("app.zig");
const choose = @import("gpu/choose.zig");
const backend = @import("gpu/backend.zig");
const metal = backend.metal;
const vulkan = backend.vulkan;

const Id = objc.Id;
const Sel = objc.Sel;
const send = objc.send;
const msg = objc.msg;
const UInt = objc.NSUInteger;
const BOOL = objc.BOOL;
const NSRect = objc.NSRect;
const NSPoint = objc.NSPoint;
const NSRange = objc.NSRange;

// NSEventModifierFlags
const flag_shift: UInt = 1 << 17;
const flag_control: UInt = 1 << 18;
const flag_option: UInt = 1 << 19;
const flag_command: UInt = 1 << 20;

// CoreGraphics, for the CPU renderer's pixels.
const CG = struct {
    var color_space: ?*anyopaque = null;
    var bitmap_create: *const fn (?*anyopaque, usize, usize, usize, usize, ?*anyopaque, u32) callconv(.c) ?*anyopaque = undefined;
    var bitmap_image: *const fn (?*anyopaque) callconv(.c) ?*anyopaque = undefined;
    var image_release: *const fn (?*anyopaque) callconv(.c) void = undefined;
    var context_release: *const fn (?*anyopaque) callconv(.c) void = undefined;

    fn load() !void {
        const create_cs = objc.symbol(*const fn (?*anyopaque) callconv(.c) ?*anyopaque, "CGColorSpaceCreateWithName") orelse return error.NoCoreGraphics;
        color_space = create_cs(objc.constant("kCGColorSpaceSRGB"));
        bitmap_create = objc.symbol(@TypeOf(bitmap_create), "CGBitmapContextCreate") orelse return error.NoCoreGraphics;
        bitmap_image = objc.symbol(@TypeOf(bitmap_image), "CGBitmapContextCreateImage") orelse return error.NoCoreGraphics;
        image_release = objc.symbol(@TypeOf(image_release), "CGImageRelease") orelse return error.NoCoreGraphics;
        context_release = objc.symbol(@TypeOf(context_release), "CGContextRelease") orelse return error.NoCoreGraphics;
    }
};

pub fn run(state: anytype, comptime view: fn (@TypeOf(state), *ui.Context) void, opts: app.WindowOptions) !void {
    const W = Window(@TypeOf(state), view);
    return W.run(state, opts);
}

fn Window(comptime S: type, comptime view: fn (S, *ui.Context) void) type {
    return struct {
        var g: Self = undefined;
        const Self = @This();

        state: S,
        nsapp: Id = null,
        window: Id = null,
        view: Id = null,
        layer: Id = null,
        fonts: text.Fonts,
        ctx: ui.Context,
        sc: scene.Scene,
        rend: scene.Renderer,
        gpu: ?backend.Gpu = null,
        /// pixels and bitmap are the CPU renderer's canvas.
        pixels: []u32 = &.{},
        bitmap: ?*anyopaque = null,
        width: i32 = 0,
        height: i32 = 0,
        scale: f32 = 1,
        need_frame: bool = true,
        quit: bool = false,
        stats: app.Stats = .{},
        timer: std.time.Timer,
        cursor: ui.Cursor = .arrow,

        fn run(state: S, opts: app.WindowOptions) !void {
            const gpa = std.heap.c_allocator;
            try objc.load();
            const pool = objc.Pool.push();
            defer pool.pop();

            g = .{ .state = state, .fonts = try text.Fonts.init(gpa), .ctx = undefined, .sc = .{ .gpa = gpa }, .rend = .{ .gpa = gpa }, .timer = try std.time.Timer.start() };
            g.ctx = ui.Context.init(gpa, &g.fonts);
            defer g.ctx.deinit();
            defer g.fonts.deinit();
            defer g.sc.deinit();
            defer g.rend.deinit();
            g.ctx.clipboard = .{ .ctx = &g, .get = clipGet, .set = clipSet };

            const cls = try objc.define("MyZigView", "NSView", &methods, &.{ "NSTextInputClient", "NSWindowDelegate" });
            g.nsapp = send(objc.class("NSApplication"), "sharedApplication", .{});
            msg(void, g.nsapp, "setActivationPolicy:", .{@as(objc.NSInteger, 0)}); // regular: a Dock icon, a menu bar

            const content = NSRect{ .size = .{ .width = @floatFromInt(opts.width), .height = @floatFromInt(opts.height) } };
            const style: UInt = 1 | 2 | 4 | 8; // titled, closable, miniaturizable, resizable
            g.window = send(send(objc.class("NSWindow"), "alloc", .{}), "initWithContentRect:styleMask:backing:defer:", .{ content, style, @as(UInt, 2), objc.NO });
            defer objc.release(g.window);
            msg(void, g.window, "setReleasedWhenClosed:", .{objc.NO});
            msg(void, g.window, "setTitle:", .{objc.string(opts.title)});
            msg(void, g.window, "setAcceptsMouseMovedEvents:", .{objc.YES});
            msg(void, g.window, "center", .{});
            g.view = send(send(cls, "alloc", .{}), "initWithFrame:", .{content});
            defer objc.release(g.view);
            msg(void, g.window, "setContentView:", .{g.view});
            msg(void, g.window, "setDelegate:", .{g.view});
            _ = msg(BOOL, g.window, "makeFirstResponder:", .{g.view});
            // The pointer is followed over the whole view, inside the window
            // or not, and its shape set where it enters.
            const area = send(send(objc.class("NSTrackingArea"), "alloc", .{}), "initWithRect:options:owner:userInfo:", .{ NSRect{}, @as(UInt, 0x01 | 0x02 | 0x04 | 0x80 | 0x200), g.view, @as(Id, null) });
            msg(void, g.view, "addTrackingArea:", .{area});
            objc.release(area);
            menu(opts.title);

            g.scale = if (env.get("MYZIG_SCALE")) |v| std.fmt.parseFloat(f32, v) catch 1 else @floatCast(msg(f64, g.window, "backingScaleFactor", .{}));
            g.openGpu(gpa);
            if (g.gpu == null) {
                CG.load() catch |e| {
                    std.log.err("myzig: {s}", .{@errorName(e)});
                    return e;
                };
                g.layer = send(objc.class("CALayer"), "layer", .{});
                msg(void, g.layer, "setContentsGravity:", .{objc.constant("kCAGravityTopLeft")});
            }
            msg(void, g.layer, "setContentsScale:", .{@as(f64, g.scale)});
            msg(void, g.view, "setLayer:", .{g.layer});
            msg(void, g.view, "setWantsLayer:", .{objc.YES});
            defer if (g.gpu) |*r| r.deinit();
            defer g.freeCanvas();
            g.resize();

            msg(void, g.window, "makeKeyAndOrderFront:", .{@as(Id, null)});
            msg(void, g.nsapp, "finishLaunching", .{});
            msg(void, g.nsapp, "activateIgnoringOtherApps:", .{objc.YES});

            const uncapped = env.has("MYZIG_UNCAPPED");
            const print_stats = env.has("MYZIG_STATS");
            const dump = env.get("MYZIG_DUMP");
            if (dump != null) if (g.gpu) |*r| r.capture();
            const exit_after_ns: ?u64 = if (env.get("MYZIG_EXIT_AFTER")) |v| @intFromFloat((std.fmt.parseFloat(f64, v) catch 0) * 1e9) else null;
            var period: u64 = 16_666_667;
            const screen = send(g.window, "screen", .{});
            if (screen != null and msg(BOOL, screen, "respondsToSelector:", .{objc.sel("maximumFramesPerSecond")}) != 0) {
                const fps = msg(objc.NSInteger, screen, "maximumFramesPerSecond", .{});
                if (fps > 0) period = @divTrunc(std.time.ns_per_s, @as(u64, @intCast(fps)));
            }
            const mode = objc.constant("NSDefaultRunLoopMode");
            const NSDate = objc.class("NSDate");
            var next_frame: u64 = 0;
            var last_print: u64 = 0;
            var window_stats = app.Stats{};

            while (!g.quit) {
                const iter = objc.Pool.push();
                defer iter.pop();
                const now = g.timer.read();
                if (exit_after_ns) |limit| if (now >= limit) break;
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
                if (g.need_frame and (uncapped or now >= next_frame)) {
                    g.need_frame = false;
                    g.frame();
                    if (dump) |path| {
                        g.writeDump(gpa, path) catch |e| std.log.err("myzig: {s}: {s}", .{ path, @errorName(e) });
                        break;
                    }
                    next_frame = if (now - next_frame < period) next_frame + period else now + period;
                }
                if (g.ctx.wake_at != 0 and now >= g.ctx.wake_at) {
                    g.ctx.wake_at = 0;
                    g.need_frame = true;
                }

                // Wait for an event, or until the next frame is due.
                var wait_ns: ?u64 = null;
                if (g.need_frame) wait_ns = if (uncapped) 0 else next_frame -| g.timer.read();
                if (g.ctx.wake_at != 0) wait_ns = @min(wait_ns orelse std.math.maxInt(u64), g.ctx.wake_at -| g.timer.read());
                if (print_stats) wait_ns = @min(wait_ns orelse std.math.maxInt(u64), std.time.ns_per_s);
                if (exit_after_ns) |limit| wait_ns = @min(wait_ns orelse std.math.maxInt(u64), limit -| g.timer.read());
                const until = if (wait_ns) |ns|
                    send(NSDate, "dateWithTimeIntervalSinceNow:", .{@as(f64, @floatFromInt(ns)) / 1e9})
                else
                    send(NSDate, "distantFuture", .{});
                var ev = send(g.nsapp, "nextEventMatchingMask:untilDate:inMode:dequeue:", .{ @as(UInt, std.math.maxInt(UInt)), until, mode, objc.YES });
                while (ev != null) {
                    msg(void, g.nsapp, "sendEvent:", .{ev});
                    ev = send(g.nsapp, "nextEventMatchingMask:untilDate:inMode:dequeue:", .{ @as(UInt, std.math.maxInt(UInt)), send(NSDate, "distantPast", .{}), mode, objc.YES });
                }
            }
            msg(void, g.window, "orderOut:", .{@as(Id, null)});
        }

        /// menu is the app's menu bar: Quit (⌘Q) and Close Window (⌘W).
        fn menu(title: []const u8) void {
            const bar = objc.new("NSMenu");
            const item = objc.new("NSMenuItem");
            msg(void, bar, "addItem:", .{item});
            const m = objc.new("NSMenu");
            var buf: [256]u8 = undefined;
            const quit_title = std.fmt.bufPrint(&buf, "Quit {s}", .{title}) catch "Quit";
            const close = send(send(objc.class("NSMenuItem"), "alloc", .{}), "initWithTitle:action:keyEquivalent:", .{ objc.string("Close Window"), objc.sel("performClose:"), objc.string("w") });
            msg(void, m, "addItem:", .{close});
            const quit = send(send(objc.class("NSMenuItem"), "alloc", .{}), "initWithTitle:action:keyEquivalent:", .{ objc.string(quit_title), objc.sel("myzigQuit:"), objc.string("q") });
            msg(void, quit, "setTarget:", .{g.view});
            msg(void, m, "addItem:", .{quit});
            msg(void, item, "setSubmenu:", .{m});
            msg(void, g.nsapp, "setMainMenu:", .{bar});
            for ([_]Id{ bar, item, m, close, quit }) |o| objc.release(o);
        }

        /// openGpu picks the GPU renderer: Metal's, or Vulkan's (MoltenVK)
        /// when MYZIG_RENDERER=vulkan names it.
        fn openGpu(w: *Self, gpa: std.mem.Allocator) void {
            const choice = choose.choose();
            if (choice.kind == .cpu) return;
            const layer = send(objc.class("CAMetalLayer"), "layer", .{});
            if (layer == null) return;
            const vsync = !env.has("MYZIG_UNCAPPED");
            if (choice.kind == .vulkan) vk: {
                const r = vulkan.Renderer.init(gpa, .{ .metal = .{ .layer = layer.? } }, 1, 1, .{
                    .vsync = vsync,
                    .validation = env.has("MYZIG_VK_VALIDATION"),
                }) catch |e| {
                    std.log.warn("myzig: no Vulkan ({s}); drawing with Metal", .{@errorName(e)});
                    break :vk;
                };
                w.gpu = .{ .vulkan = r };
            }
            if (w.gpu == null) {
                const r = metal.Renderer.init(gpa, layer, .{ .vsync = vsync }) catch |e| {
                    std.log.warn("myzig: no Metal ({s}); drawing on the CPU", .{@errorName(e)});
                    return;
                };
                w.gpu = .{ .metal = r };
            }
            w.layer = layer;
            if (env.has("MYZIG_STATS")) std.debug.print("myzig: drawing with {s} on {s}\n", .{ w.gpu.?.api(), w.gpu.?.name() });
            w.rend.raster = false;
            w.rend.retain_pixels = false;
        }

        fn freeCanvas(w: *Self) void {
            if (w.bitmap) |b| CG.context_release(b);
            w.bitmap = null;
            if (w.pixels.len > 0) std.heap.c_allocator.free(w.pixels);
            w.pixels = &.{};
        }

        /// resize follows the view's size, in pixels.
        fn resize(w: *Self) void {
            const b = msg(NSRect, w.view, "bounds", .{});
            const nw: i32 = @max(1, @as(i32, @intFromFloat(@round(b.size.width * w.scale))));
            const nh: i32 = @max(1, @as(i32, @intFromFloat(@round(b.size.height * w.scale))));
            if (nw == w.width and nh == w.height) return;
            w.width = nw;
            w.height = nh;
            if (w.gpu == null) {
                w.freeCanvas();
                const px = std.heap.c_allocator.alloc(u32, @intCast(nw * nh)) catch return;
                w.pixels = px;
                // BGRX in memory, as the CPU renderer writes it.
                const info: u32 = (2 << 12) | 6; // byte order 32 little, alpha none skip first
                w.bitmap = CG.bitmap_create(px.ptr, @intCast(nw), @intCast(nh), 8, @as(usize, @intCast(nw)) * 4, CG.color_space, info);
            }
            w.rend.invalidate();
            w.need_frame = true;
        }

        fn frame(w: *Self) void {
            if (w.gpu == null and w.pixels.len == 0) return;
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
                if (w.rend.damage.items.len > 0) r.draw(&w.sc, &w.fonts, w.width, w.height) catch |e| std.log.err("myzig: {s} frame: {s}", .{ r.api(), @errorName(e) });
            } else if (w.rend.damage.items.len > 0) {
                // The layer shows a copy of the canvas, swapped in without
                // the fade Core Animation would give it.
                const img = CG.bitmap_image(w.bitmap);
                const tx = objc.class("CATransaction");
                msg(void, tx, "begin", .{});
                msg(void, tx, "setDisableActions:", .{objc.YES});
                msg(void, w.layer, "setContents:", .{@as(Id, img)});
                msg(void, tx, "commit", .{});
                CG.image_release(img);
            }
            w.stats.frames += 1;
            w.stats.build_ns += t1 - t0;
            w.stats.raster_ns += t2 - t1;
            w.stats.present_ns += w.timer.read() - t2;
            w.stats.pixels += w.rend.stats.pixels - px;
            if (w.ctx.animating or w.ctx.requested) w.need_frame = true;
            if (w.ctx.cursor != w.cursor) {
                w.cursor = w.ctx.cursor;
                setCursor(w.cursor);
            }
        }

        fn writeDump(w: *Self, gpa: std.mem.Allocator, path: []const u8) !void {
            if (w.gpu) |*r| return backend.writePPM(gpa, r, w.width, w.height, path);
            try app.writePixels(path, w.pixels, w.width, w.height, @intCast(w.width));
        }

        fn setCursor(c: ui.Cursor) void {
            const NSCursor = objc.class("NSCursor");
            const cur = switch (c) {
                .arrow => send(NSCursor, "arrowCursor", .{}),
                .text => send(NSCursor, "IBeamCursor", .{}),
                .pointer => send(NSCursor, "pointingHandCursor", .{}),
            };
            msg(void, cur, "set", .{});
        }

        fn point(ev: Id) [2]f32 {
            const p = msg(NSPoint, ev, "locationInWindow", .{});
            const v = msg(NSPoint, g.view, "convertPoint:fromView:", .{ p, @as(Id, null) });
            return .{ @floatCast(v.x), @floatCast(v.y) };
        }

        // The view's methods, called by AppKit.

        const methods = [_]objc.Method{
            .{ .name = "acceptsFirstResponder", .imp = &yes, .types = "c@:" },
            .{ .name = "isFlipped", .imp = &yes, .types = "c@:" },
            .{ .name = "acceptsFirstMouse:", .imp = &yes1, .types = "c@:@" },
            .{ .name = "mouseDown:", .imp = &mouseDown, .types = "v@:@" },
            .{ .name = "mouseUp:", .imp = &mouseUp, .types = "v@:@" },
            .{ .name = "mouseDragged:", .imp = &mouseMoved, .types = "v@:@" },
            .{ .name = "mouseMoved:", .imp = &mouseMoved, .types = "v@:@" },
            .{ .name = "mouseExited:", .imp = &mouseExited, .types = "v@:@" },
            .{ .name = "scrollWheel:", .imp = &scrollWheel, .types = "v@:@" },
            .{ .name = "keyDown:", .imp = &keyDown, .types = "v@:@" },
            .{ .name = "flagsChanged:", .imp = &flagsChanged, .types = "v@:@" },
            .{ .name = "cursorUpdate:", .imp = &cursorUpdate, .types = "v@:@" },
            .{ .name = "myzigQuit:", .imp = &quitAction, .types = "v@:@" },
            // NSWindowDelegate
            .{ .name = "windowShouldClose:", .imp = &shouldClose, .types = "c@:@" },
            .{ .name = "windowDidResize:", .imp = &didResize, .types = "v@:@" },
            .{ .name = "windowDidChangeBackingProperties:", .imp = &didChangeBacking, .types = "v@:@" },
            // NSTextInputClient: typed text, through the input method.
            .{ .name = "insertText:replacementRange:", .imp = &insertText, .types = "v@:@{_NSRange=QQ}" },
            .{ .name = "doCommandBySelector:", .imp = &doCommand, .types = "v@::" },
            .{ .name = "setMarkedText:selectedRange:replacementRange:", .imp = &setMarkedText, .types = "v@:@{_NSRange=QQ}{_NSRange=QQ}" },
            .{ .name = "unmarkText", .imp = &unmarkText, .types = "v@:" },
            .{ .name = "hasMarkedText", .imp = &no, .types = "c@:" },
            .{ .name = "markedRange", .imp = &emptyRange, .types = "{_NSRange=QQ}@:" },
            .{ .name = "selectedRange", .imp = &emptyRange, .types = "{_NSRange=QQ}@:" },
            .{ .name = "validAttributesForMarkedText", .imp = &noAttributes, .types = "@@:" },
            .{ .name = "attributedSubstringForProposedRange:actualRange:", .imp = &noSubstring, .types = "@@:{_NSRange=QQ}^{_NSRange=QQ}" },
            .{ .name = "characterIndexForPoint:", .imp = &noIndex, .types = "Q@:{CGPoint=dd}" },
            .{ .name = "firstRectForCharacterRange:actualRange:", .imp = &firstRect, .types = "{CGRect={CGPoint=dd}{CGSize=dd}}@:{_NSRange=QQ}^{_NSRange=QQ}" },
        };

        fn yes(_: Id, _: Sel) callconv(.c) BOOL {
            return objc.YES;
        }
        fn yes1(_: Id, _: Sel, _: Id) callconv(.c) BOOL {
            return objc.YES;
        }
        fn no(_: Id, _: Sel) callconv(.c) BOOL {
            return objc.NO;
        }

        fn mouseDown(_: Id, _: Sel, ev: Id) callconv(.c) void {
            g.ctx.setMods(modsOf(msg(UInt, ev, "modifierFlags", .{})));
            const p = point(ev);
            if (g.ctx.pointerDown(p[0], p[1])) g.need_frame = true;
        }
        fn mouseUp(_: Id, _: Sel, ev: Id) callconv(.c) void {
            const p = point(ev);
            if (g.ctx.pointerUp(p[0], p[1])) g.need_frame = true;
        }
        fn mouseMoved(_: Id, _: Sel, ev: Id) callconv(.c) void {
            const p = point(ev);
            if (g.ctx.pointerMove(p[0], p[1])) g.need_frame = true;
        }
        fn mouseExited(_: Id, _: Sel, _: Id) callconv(.c) void {
            if (g.ctx.pointerMove(-1, -1)) g.need_frame = true;
        }
        fn scrollWheel(_: Id, _: Sel, ev: Id) callconv(.c) void {
            const dy = msg(f64, ev, "scrollingDeltaY", .{});
            // A trackpad's deltas are points; a wheel's are lines.
            const precise = msg(BOOL, ev, "hasPreciseScrollingDeltas", .{}) != 0;
            const px: f32 = @floatCast(if (precise) -dy else -dy * 40);
            if (px == 0) return;
            const p = point(ev);
            if (g.ctx.scroll(p[0], p[1], px)) g.need_frame = true;
        }
        fn keyDown(self: Id, _: Sel, ev: Id) callconv(.c) void {
            const flags = msg(UInt, ev, "modifierFlags", .{});
            var mods = modsOf(flags);
            var key = mapKey(msg(u16, ev, "keyCode", .{}), objc.utf8(send(ev, "charactersIgnoringModifiers", .{})));
            if (flags & flag_command != 0 and (key == .left or key == .right)) {
                key = if (key == .left) .home else .end;
                mods.ctrl = false;
            }
            if (g.ctx.keyDownMods(key, mods)) g.need_frame = true;
            if (flags & (flag_command | flag_control) == 0 and g.ctx.input.text_focus) {
                const arr = send(objc.class("NSArray"), "arrayWithObject:", .{ev});
                msg(void, self, "interpretKeyEvents:", .{arr});
            }
        }
        fn flagsChanged(_: Id, _: Sel, ev: Id) callconv(.c) void {
            g.ctx.setMods(modsOf(msg(UInt, ev, "modifierFlags", .{})));
        }
        fn cursorUpdate(_: Id, _: Sel, _: Id) callconv(.c) void {
            setCursor(g.cursor);
        }
        fn quitAction(_: Id, _: Sel, _: Id) callconv(.c) void {
            g.quit = true;
        }
        fn shouldClose(_: Id, _: Sel, _: Id) callconv(.c) BOOL {
            g.quit = true;
            return objc.YES;
        }
        fn didResize(_: Id, _: Sel, _: Id) callconv(.c) void {
            g.resize();
            // Live resizing runs AppKit's loop, not this one: draw now.
            if (g.need_frame) {
                g.need_frame = false;
                g.frame();
            }
        }
        fn didChangeBacking(_: Id, _: Sel, _: Id) callconv(.c) void {
            if (env.get("MYZIG_SCALE") != null) return;
            g.scale = @floatCast(msg(f64, g.window, "backingScaleFactor", .{}));
            msg(void, g.layer, "setContentsScale:", .{@as(f64, g.scale)});
            g.width = 0; // the canvas is made again
            g.resize();
        }
        fn insertText(_: Id, _: Sel, str: Id, _: NSRange) callconv(.c) void {
            var s = str;
            if (msg(BOOL, s, "isKindOfClass:", .{objc.class("NSAttributedString")}) != 0) s = send(s, "string", .{});
            const t = objc.utf8(s);
            // Keys that are not text (Return, Tab) came through keyDown.
            if (t.len == 0 or t[0] < 0x20 or t[0] == 0x7f) return;
            if (g.ctx.typeText(t)) g.need_frame = true;
        }
        fn doCommand(_: Id, _: Sel, _: Sel) callconv(.c) void {}
        fn setMarkedText(_: Id, _: Sel, _: Id, _: NSRange, _: NSRange) callconv(.c) void {}
        fn unmarkText(_: Id, _: Sel) callconv(.c) void {}
        fn emptyRange(_: Id, _: Sel) callconv(.c) NSRange {
            return .{ .location = objc.NSNotFound, .length = 0 };
        }
        fn noAttributes(_: Id, _: Sel) callconv(.c) Id {
            return send(objc.class("NSArray"), "array", .{});
        }
        fn noSubstring(_: Id, _: Sel, _: NSRange, _: ?*NSRange) callconv(.c) Id {
            return null;
        }
        fn noIndex(_: Id, _: Sel, _: NSPoint) callconv(.c) UInt {
            return objc.NSNotFound;
        }
        fn firstRect(_: Id, _: Sel, _: NSRange, _: ?*NSRange) callconv(.c) NSRect {
            // The input method's window goes by the window's corner.
            const f = msg(NSRect, g.window, "frame", .{});
            return .{ .origin = f.origin, .size = .{ .width = 0, .height = 0 } };
        }

        fn clipGet(_: *anyopaque, gpa: std.mem.Allocator) ?[]u8 {
            const pb = send(objc.class("NSPasteboard"), "generalPasteboard", .{});
            const s = send(pb, "stringForType:", .{objc.constant("NSPasteboardTypeString")});
            if (s == null) return null;
            return gpa.dupe(u8, objc.utf8(s)) catch null;
        }
        fn clipSet(_: *anyopaque, s: []const u8) void {
            const pb = send(objc.class("NSPasteboard"), "generalPasteboard", .{});
            _ = msg(objc.NSInteger, pb, "clearContents", .{});
            _ = msg(BOOL, pb, "setString:forType:", .{ objc.string(s), objc.constant("NSPasteboardTypeString") });
        }
    };
}

fn modsOf(flags: UInt) ui.Mods {
    return .{
        .shift = flags & flag_shift != 0,
        .ctrl = flags & flag_command != 0,
        .alt = flags & flag_option != 0,
        .super = flags & flag_control != 0,
    };
}

/// mapKey maps a key's virtual key code (kVK_*), or for letters the
/// character it types in the keyboard's layout.
fn mapKey(code: u16, chars: []const u8) ui.Key {
    if (chars.len == 1) {
        const k = ui.Key.letter(chars[0]);
        if (k != .none) return k;
    }
    return switch (code) {
        0x24, 0x4C => .enter,
        0x30 => .tab,
        0x31 => .space,
        0x33 => .backspace,
        0x35 => .escape,
        0x75 => .delete,
        0x73 => .home,
        0x77 => .end,
        0x74 => .page_up,
        0x79 => .page_down,
        0x7B => .left,
        0x7C => .right,
        0x7D => .down,
        0x7E => .up,
        else => .none,
    };
}
