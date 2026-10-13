//! OpenGL contexts on X11 through GLX, from libGL loaded at run time.

const std = @import("std");
const x = @import("../x11.zig");

const FBConfig = *opaque {};
const Context = *opaque {};

pub const Glx = struct {
    lib: std.DynLib,
    chooseFBConfig: *const fn (*x.Display, c_int, [*]const c_int, *c_int) callconv(.c) ?[*]FBConfig,
    getVisualFromFBConfig: *const fn (*x.Display, FBConfig) callconv(.c) ?*x.XVisualInfo,
    getProcAddress: *const fn ([*:0]const u8) callconv(.c) ?*const anyopaque,
    makeCurrent: *const fn (*x.Display, x.Window, ?Context) callconv(.c) c_int,
    swapBuffers: *const fn (*x.Display, x.Window) callconv(.c) void,
    destroyContext: *const fn (*x.Display, Context) callconv(.c) void,
    fbc: ?FBConfig = null,
    ctx: ?Context = null,

    pub fn open() !Glx {
        var lib = std.DynLib.open("libGL.so.1") catch try std.DynLib.open("libGL.so");
        errdefer lib.close();
        return .{
            .lib = lib,
            .chooseFBConfig = lib.lookup(@FieldType(Glx, "chooseFBConfig"), "glXChooseFBConfig") orelse return error.NoGlx,
            .getVisualFromFBConfig = lib.lookup(@FieldType(Glx, "getVisualFromFBConfig"), "glXGetVisualFromFBConfig") orelse return error.NoGlx,
            .getProcAddress = lib.lookup(@FieldType(Glx, "getProcAddress"), "glXGetProcAddressARB") orelse return error.NoGlx,
            .makeCurrent = lib.lookup(@FieldType(Glx, "makeCurrent"), "glXMakeCurrent") orelse return error.NoGlx,
            .swapBuffers = lib.lookup(@FieldType(Glx, "swapBuffers"), "glXSwapBuffers") orelse return error.NoGlx,
            .destroyContext = lib.lookup(@FieldType(Glx, "destroyContext"), "glXDestroyContext") orelse return error.NoGlx,
        };
    }

    /// visual chooses a double-buffered true color configuration and
    /// returns the visual a window drawn with it needs. With want, the
    /// visual of a window that exists, it chooses one with that visual.
    pub fn visual(g: *Glx, dpy: *x.Display, screen: c_int, want: ?c_ulong) !*x.XVisualInfo {
        const attrs = [_]c_int{
            0x8012, 1, // GLX_X_RENDERABLE
            0x8010, 1, // GLX_DRAWABLE_TYPE: window
            0x8011, 1, // GLX_RENDER_TYPE: RGBA
            0x22, 0x8002, // GLX_X_VISUAL_TYPE: true color
            8, 8, 9, 8, 10, 8, // red, green, blue sizes
            5, 1, // GLX_DOUBLEBUFFER
            0,
        };
        var n: c_int = 0;
        const list = g.chooseFBConfig(dpy, screen, &attrs, &n) orelse return error.NoFBConfig;
        defer _ = x.XFree(@ptrCast(list));
        for (list[0..@intCast(n)]) |fbc| {
            const vi = g.getVisualFromFBConfig(dpy, fbc) orelse continue;
            if (want == null or vi.visualid == want.?) {
                g.fbc = fbc;
                return vi;
            }
            _ = x.XFree(vi);
        }
        return error.NoFBConfig;
    }

    /// context makes an OpenGL 3.3 core context current on win.
    pub fn context(g: *Glx, dpy: *x.Display, win: x.Window) !void {
        const Create = *const fn (*x.Display, FBConfig, ?Context, c_int, [*]const c_int) callconv(.c) ?Context;
        const create: Create = @ptrCast(@alignCast(g.getProcAddress("glXCreateContextAttribsARB") orelse return error.NoCreateContext));
        const attrs = [_]c_int{ 0x2091, 3, 0x2092, 3, 0x9126, 1, 0 };
        g.ctx = create(dpy, g.fbc.?, null, 1, &attrs) orelse return error.CreateContext;
        if (g.makeCurrent(dpy, win, g.ctx) == 0) return error.MakeCurrent;
        // Vsync where the driver has it; the loop paces frames anyway.
        const Interval = *const fn (*x.Display, x.Window, c_int) callconv(.c) void;
        if (g.getProcAddress("glXSwapIntervalEXT")) |p| @as(Interval, @ptrCast(@alignCast(p)))(dpy, win, 1);
    }

    pub fn release(g: *Glx, dpy: *x.Display) void {
        if (g.ctx) |c| {
            _ = g.makeCurrent(dpy, 0, null);
            g.destroyContext(dpy, c);
            g.ctx = null;
        }
    }

    pub fn proc(g: *Glx, name: [*:0]const u8) ?*const anyopaque {
        return g.getProcAddress(name);
    }
};
