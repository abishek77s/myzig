//! Gpu is the renderer a window draws with on the GPU: Direct3D 11's,
//! OpenGL's, Vulkan's or Metal's, the same scene and shader either way. The window keeps the
//! platform's side (the GLX or WGL context OpenGL presents with).

const std = @import("std");
const sys = @import("../sys.zig");
const builtin = @import("builtin");
const scene = @import("../scene.zig");
const text = @import("../text.zig");
pub const glr = @import("gl.zig");
pub const vulkan = @import("vulkan.zig");
/// metal is Metal's renderer on macOS, and nothing elsewhere.
pub const metal = if (builtin.os.tag == .macos) @import("metal.zig") else struct {
    pub const Renderer = struct {
        pub fn deinit(_: *Renderer) void {}
        pub fn name(_: *const Renderer) []const u8 {
            return "";
        }
        pub fn draw(_: *Renderer, _: *const scene.Scene, _: *text.Fonts, _: i32, _: i32) !void {}
        pub fn read(_: *Renderer, _: i32, _: i32, _: []u32) void {}
    };
};

/// d3d11 is Direct3D 11's renderer on Windows, and nothing elsewhere.
pub const d3d11 = if (builtin.os.tag == .windows) @import("d3d11.zig") else struct {
    pub const Renderer = struct {
        capture: bool = false,
        pub fn deinit(_: *Renderer) void {}
        pub fn name(_: *const Renderer) []const u8 {
            return "";
        }
        pub fn software(_: *const Renderer) bool {
            return false;
        }
        pub fn draw(_: *Renderer, _: *const scene.Scene, _: *text.Fonts, _: i32, _: i32) !void {}
        pub fn read(_: *Renderer, _: i32, _: i32, _: []u32) void {}
    };
};

pub const Gpu = union(enum) {
    gl: glr.Renderer,
    vulkan: vulkan.Renderer,
    metal: metal.Renderer,
    d3d11: d3d11.Renderer,

    pub fn deinit(g: *Gpu) void {
        switch (g.*) {
            inline else => |*r| r.deinit(),
        }
    }

    /// api names the API: "OpenGL", "Vulkan" or "Metal".
    pub fn api(g: *const Gpu) []const u8 {
        return switch (g.*) {
            .gl => "OpenGL",
            .vulkan => "Vulkan",
            .metal => "Metal",
            .d3d11 => "Direct3D 11",
        };
    }

    pub fn name(g: *const Gpu) []const u8 {
        return switch (g.*) {
            inline else => |*r| r.name(),
        };
    }

    /// software reports whether the API draws on the CPU.
    pub fn software(g: *const Gpu) bool {
        return switch (g.*) {
            .gl => |*r| glr.software(r.name()),
            .vulkan => |*r| r.software(),
            .metal => false,
            .d3d11 => |*r| r.software(),
        };
    }

    /// draw draws s, width×height pixels. Vulkan and Metal also present; OpenGL's
    /// frame is presented by the window's context (swapBuffers).
    pub fn draw(g: *Gpu, s: *const scene.Scene, fonts: *text.Fonts, width: i32, height: i32) !void {
        switch (g.*) {
            inline else => |*r| try r.draw(s, fonts, width, height),
        }
    }

    /// capture makes the frames drawn after it readable with read().
    pub fn capture(g: *Gpu) void {
        switch (g.*) {
            .gl => {},
            .vulkan => |*r| r.capture = true,
            .d3d11 => |*r| r.capture = true,
            .metal => |*r| if (builtin.os.tag == .macos) {
                r.capture = true;
            },
        }
    }

    pub fn read(g: *Gpu, width: i32, height: i32, out: []u32) void {
        switch (g.*) {
            inline else => |*r| r.read(width, height, out),
        }
    }
};

/// writePPM writes the last frame g drew (with capture set) to path.
pub fn writePPM(gpa: std.mem.Allocator, g: *Gpu, w: i32, h: i32, path: []const u8) !void {
    const px = try gpa.alloc(u32, @intCast(w * h));
    defer gpa.free(px);
    g.read(w, h, px);
    var wbuf: [64 << 10]u8 = undefined;
    var fw = try sys.create(path, &wbuf);
    defer fw.close();
    try fw.interface.print("P6\n{d} {d}\n255\n", .{ w, h });
    for (px) |p| try fw.interface.writeAll(&.{ @truncate(p >> 16), @truncate(p >> 8), @truncate(p) });
    try fw.interface.flush();
}
