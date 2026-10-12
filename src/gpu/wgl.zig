//! OpenGL contexts on Windows through WGL, from opengl32.dll loaded at run
//! time: an app drawing on the CPU never maps the driver.

const std = @import("std");

const WINAPI: std.builtin.CallingConvention = .winapi;
const HDC = *opaque {};
const HGLRC = *opaque {};
const HMODULE = *opaque {};

const PIXELFORMATDESCRIPTOR = extern struct {
    nSize: u16 = @sizeOf(PIXELFORMATDESCRIPTOR),
    nVersion: u16 = 1,
    dwFlags: u32 = 0x4 | 0x20 | 0x1, // draw to window, OpenGL, double buffer
    iPixelType: u8 = 0,
    cColorBits: u8 = 32,
    cRedBits: u8 = 0,
    cRedShift: u8 = 0,
    cGreenBits: u8 = 0,
    cGreenShift: u8 = 0,
    cBlueBits: u8 = 0,
    cBlueShift: u8 = 0,
    cAlphaBits: u8 = 8,
    cAlphaShift: u8 = 0,
    cAccumBits: u8 = 0,
    cAccumRedBits: u8 = 0,
    cAccumGreenBits: u8 = 0,
    cAccumBlueBits: u8 = 0,
    cAccumAlphaBits: u8 = 0,
    cDepthBits: u8 = 0,
    cStencilBits: u8 = 0,
    cAuxBuffers: u8 = 0,
    iLayerType: u8 = 0,
    bReserved: u8 = 0,
    dwLayerMask: u32 = 0,
    dwVisibleMask: u32 = 0,
    dwDamageMask: u32 = 0,
};

extern "kernel32" fn LoadLibraryW(name: [*:0]const u16) callconv(WINAPI) ?HMODULE;
extern "kernel32" fn GetProcAddress(m: HMODULE, name: [*:0]const u8) callconv(WINAPI) ?*const anyopaque;
extern "gdi32" fn ChoosePixelFormat(dc: HDC, pfd: *const PIXELFORMATDESCRIPTOR) callconv(WINAPI) i32;
extern "gdi32" fn SetPixelFormat(dc: HDC, fmt: i32, pfd: *const PIXELFORMATDESCRIPTOR) callconv(WINAPI) i32;
extern "gdi32" fn SwapBuffers(dc: HDC) callconv(WINAPI) i32;

pub const Wgl = struct {
    lib: HMODULE,
    dc: HDC,
    ctx: ?HGLRC = null,
    getProcAddress: *const fn ([*:0]const u8) callconv(WINAPI) ?*const anyopaque,
    makeCurrent: *const fn (?HDC, ?HGLRC) callconv(WINAPI) i32,
    deleteContext: *const fn (HGLRC) callconv(WINAPI) i32,

    /// open makes an OpenGL context current on the window of dc: 3.3 core
    /// where the driver makes one, its default context otherwise.
    pub fn open(dc_ptr: *anyopaque) !Wgl {
        const dc: HDC = @ptrCast(dc_ptr);
        const lib = LoadLibraryW(std.unicode.utf8ToUtf16LeStringLiteral("opengl32.dll")) orelse return error.NoOpenGL;
        const create: *const fn (HDC) callconv(WINAPI) ?HGLRC = @ptrCast(GetProcAddress(lib, "wglCreateContext") orelse return error.NoOpenGL);
        var w = Wgl{
            .lib = lib,
            .dc = dc,
            .getProcAddress = @ptrCast(GetProcAddress(lib, "wglGetProcAddress") orelse return error.NoOpenGL),
            .makeCurrent = @ptrCast(GetProcAddress(lib, "wglMakeCurrent") orelse return error.NoOpenGL),
            .deleteContext = @ptrCast(GetProcAddress(lib, "wglDeleteContext") orelse return error.NoOpenGL),
        };
        const pfd = PIXELFORMATDESCRIPTOR{};
        const fmt = ChoosePixelFormat(dc, &pfd);
        if (fmt == 0 or SetPixelFormat(dc, fmt, &pfd) == 0) return error.PixelFormat;
        const legacy = create(dc) orelse return error.CreateContext;
        if (w.makeCurrent(dc, legacy) == 0) return error.MakeCurrent;
        w.ctx = legacy;
        const CreateAttribs = *const fn (HDC, ?HGLRC, [*]const i32) callconv(WINAPI) ?HGLRC;
        if (w.getProcAddress("wglCreateContextAttribsARB")) |p| {
            const attrs = [_]i32{ 0x2091, 3, 0x2092, 3, 0x9126, 1, 0 };
            if (@as(CreateAttribs, @ptrCast(p))(dc, null, &attrs)) |core| {
                _ = w.makeCurrent(dc, core);
                _ = w.deleteContext(legacy);
                w.ctx = core;
            }
        }
        const Interval = *const fn (i32) callconv(WINAPI) i32;
        if (w.getProcAddress("wglSwapIntervalEXT")) |p| _ = @as(Interval, @ptrCast(p))(1);
        return w;
    }

    pub fn release(w: *Wgl) void {
        if (w.ctx) |c| {
            _ = w.makeCurrent(null, null);
            _ = w.deleteContext(c);
            w.ctx = null;
        }
    }

    /// proc finds an OpenGL function: wglGetProcAddress knows those after
    /// OpenGL 1.1 and answers 0 to 3 or -1 for the rest, which opengl32.dll
    /// exports itself.
    pub fn proc(w: *Wgl, name: [*:0]const u8) ?*const anyopaque {
        if (w.getProcAddress(name)) |p| {
            const v = @intFromPtr(p);
            if (v > 3 and v != std.math.maxInt(usize)) return p;
        }
        return GetProcAddress(w.lib, name);
    }

    pub fn swap(w: *Wgl) void {
        _ = SwapBuffers(w.dc);
    }
};
