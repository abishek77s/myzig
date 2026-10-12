//! Gpu is the renderer a window draws with on the GPU: OpenGL's or
//! Vulkan's, the same scene and shader either way. The window keeps the
//! platform's side (the GLX or WGL context OpenGL presents with).

const std = @import("std");
const scene = @import("../scene.zig");
const text = @import("../text.zig");
pub const glr = @import("gl.zig");
pub const vulkan = @import("vulkan.zig");

pub const Gpu = union(enum) {
    gl: glr.Renderer,
    vulkan: vulkan.Renderer,

    pub fn deinit(g: *Gpu) void {
        switch (g.*) {
            inline else => |*r| r.deinit(),
        }
    }

    /// api names the API: "OpenGL" or "Vulkan".
    pub fn api(g: *const Gpu) []const u8 {
        return switch (g.*) {
            .gl => "OpenGL",
            .vulkan => "Vulkan",
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
        };
    }

    /// draw draws s, width×height pixels. Vulkan also presents; OpenGL's
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
    var f = try std.fs.cwd().createFile(path, .{});
    defer f.close();
    var wbuf: [64 << 10]u8 = undefined;
    var fw = f.writer(&wbuf);
    try fw.interface.print("P6\n{d} {d}\n255\n", .{ w, h });
    for (px) |p| try fw.interface.writeAll(&.{ @truncate(p >> 16), @truncate(p >> 8), @truncate(p) });
    try fw.interface.flush();
}
