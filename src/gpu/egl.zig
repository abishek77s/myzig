//! OpenGL contexts on Wayland through EGL, from libEGL and libwayland-egl
//! loaded at run time.

const std = @import("std");

const Display = *opaque {};
const Config = *opaque {};
const Context = *opaque {};
const Surface = *opaque {};
const Window = opaque {};

pub const Egl = struct {
    lib: std.DynLib,
    wlegl: std.DynLib,
    getDisplay: *const fn (?*anyopaque) callconv(.c) ?Display,
    initialize: *const fn (Display, ?*c_int, ?*c_int) callconv(.c) c_uint,
    terminate: *const fn (Display) callconv(.c) c_uint,
    bindAPI: *const fn (c_uint) callconv(.c) c_uint,
    chooseConfig: *const fn (Display, [*]const c_int, [*]Config, c_int, *c_int) callconv(.c) c_uint,
    createContext: *const fn (Display, Config, ?Context, [*]const c_int) callconv(.c) ?Context,
    destroyContext: *const fn (Display, Context) callconv(.c) c_uint,
    createWindowSurface: *const fn (Display, Config, *Window, ?[*]const c_int) callconv(.c) ?Surface,
    destroySurface: *const fn (Display, Surface) callconv(.c) c_uint,
    makeCurrent: *const fn (Display, ?Surface, ?Surface, ?Context) callconv(.c) c_uint,
    swapBuffers: *const fn (Display, Surface) callconv(.c) c_uint,
    swapInterval: *const fn (Display, c_int) callconv(.c) c_uint,
    getProcAddress: *const fn ([*:0]const u8) callconv(.c) ?*const anyopaque,
    windowCreate: *const fn (*anyopaque, c_int, c_int) callconv(.c) ?*Window,
    windowDestroy: *const fn (*Window) callconv(.c) void,
    windowResize: *const fn (*Window, c_int, c_int, c_int, c_int) callconv(.c) void,
    dpy: ?Display = null,
    ctx: ?Context = null,
    surface: ?Surface = null,
    window: ?*Window = null,

    pub fn open() !Egl {
        var lib = std.DynLib.open("libEGL.so.1") catch try std.DynLib.open("libEGL.so");
        errdefer lib.close();
        var wlegl = std.DynLib.open("libwayland-egl.so.1") catch try std.DynLib.open("libwayland-egl.so");
        errdefer wlegl.close();
        var g: Egl = undefined;
        g.lib = lib;
        g.wlegl = wlegl;
        const names = .{
            .{ "getDisplay", "eglGetDisplay" },
            .{ "initialize", "eglInitialize" },
            .{ "terminate", "eglTerminate" },
            .{ "bindAPI", "eglBindAPI" },
            .{ "chooseConfig", "eglChooseConfig" },
            .{ "createContext", "eglCreateContext" },
            .{ "destroyContext", "eglDestroyContext" },
            .{ "createWindowSurface", "eglCreateWindowSurface" },
            .{ "destroySurface", "eglDestroySurface" },
            .{ "makeCurrent", "eglMakeCurrent" },
            .{ "swapBuffers", "eglSwapBuffers" },
            .{ "swapInterval", "eglSwapInterval" },
            .{ "getProcAddress", "eglGetProcAddress" },
        };
        inline for (names) |n| {
            @field(g, n[0]) = lib.lookup(@FieldType(Egl, n[0]), n[1]) orelse return error.NoEgl;
        }
        g.windowCreate = wlegl.lookup(@FieldType(Egl, "windowCreate"), "wl_egl_window_create") orelse return error.NoEgl;
        g.windowDestroy = wlegl.lookup(@FieldType(Egl, "windowDestroy"), "wl_egl_window_destroy") orelse return error.NoEgl;
        g.windowResize = wlegl.lookup(@FieldType(Egl, "windowResize"), "wl_egl_window_resize") orelse return error.NoEgl;
        g.dpy = null;
        g.ctx = null;
        g.surface = null;
        g.window = null;
        return g;
    }

    /// context makes an OpenGL 3.3 core context current on a wl_surface
    /// of width×height pixels.
    pub fn context(g: *Egl, display: *anyopaque, surface: *anyopaque, width: i32, height: i32) !void {
        const dpy = g.getDisplay(display) orelse return error.NoEglDisplay;
        if (g.initialize(dpy, null, null) == 0) return error.EglInitialize;
        g.dpy = dpy;
        if (g.bindAPI(0x30A2) == 0) return error.NoOpenGlApi; // EGL_OPENGL_API
        const attrs = [_]c_int{
            0x3033, 0x0004, // EGL_SURFACE_TYPE: window
            0x3040, 0x0008, // EGL_RENDERABLE_TYPE: OpenGL
            0x3024, 8, 0x3023, 8, 0x3022, 8, // red, green, blue
            0x3038, // EGL_NONE
        };
        var config: [1]Config = undefined;
        var n: c_int = 0;
        if (g.chooseConfig(dpy, &attrs, &config, 1, &n) == 0 or n == 0) return error.NoEglConfig;
        const ctx_attrs = [_]c_int{
            0x3098, 3, // EGL_CONTEXT_MAJOR_VERSION
            0x30FB, 3, // EGL_CONTEXT_MINOR_VERSION
            0x30FD, 1, // EGL_CONTEXT_OPENGL_PROFILE_MASK: core
            0x3038,
        };
        g.ctx = g.createContext(dpy, config[0], null, &ctx_attrs) orelse return error.CreateContext;
        g.window = g.windowCreate(surface, width, height) orelse return error.NoEglWindow;
        g.surface = g.createWindowSurface(dpy, config[0], g.window.?, null) orelse return error.NoEglSurface;
        if (g.makeCurrent(dpy, g.surface, g.surface, g.ctx) == 0) return error.MakeCurrent;
        // The window paces frames with the compositor's frame callbacks; a
        // swap that waited for one too could wait for ever on a hidden window.
        _ = g.swapInterval(dpy, 0);
    }

    pub fn resize(g: *Egl, width: i32, height: i32) void {
        if (g.window) |w| g.windowResize(w, width, height, 0, 0);
    }

    pub fn swap(g: *Egl) void {
        if (g.dpy) |d| if (g.surface) |s| {
            _ = g.swapBuffers(d, s);
        };
    }

    pub fn release(g: *Egl) void {
        if (g.dpy) |d| {
            _ = g.makeCurrent(d, null, null, null);
            if (g.surface) |s| _ = g.destroySurface(d, s);
            if (g.ctx) |c| _ = g.destroyContext(d, c);
            _ = g.terminate(d);
        }
        if (g.window) |w| g.windowDestroy(w);
        g.dpy = null;
        g.surface = null;
        g.ctx = null;
        g.window = null;
    }

    pub fn proc(g: *Egl, name: [*:0]const u8) ?*const anyopaque {
        return g.getProcAddress(name);
    }
};
