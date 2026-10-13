//! Which renderer a window draws with: a GPU's, as gpui's are. Direct3D 11
//! on Windows (the hardware's, else WARP, its software rasterizer), Metal
//! on macOS, Vulkan elsewhere, else OpenGL. One API is loaded at a time:
//! one that fails is unloaded before the next is tried, and the CPU
//! renderer is only the last resort, where no GPU API works at all.
//! WindowOptions.renderer = .cpu, or MYZIG_RENDERER (cpu, gpu, d3d11, gl,
//! vulkan or metal), chooses otherwise.

const std = @import("std");
const builtin = @import("builtin");
const env = @import("../env.zig");

pub const Kind = enum { cpu, gl, vulkan, metal, d3d11 };

/// Renderer is what an app asks for in WindowOptions.
pub const Renderer = enum {
    /// The platform's GPU API, the CPU's where none works.
    gpu,
    /// The CPU's, always.
    cpu,
};

pub const Choice = struct {
    kind: Kind,
    /// forced is set when the user named the renderer: nothing else is
    /// tried after it.
    forced: bool = false,
};

pub fn choose(asked: Renderer) Choice {
    if (env.get("MYZIG_RENDERER")) |v| {
        if (eql(v, "cpu")) return .{ .kind = .cpu, .forced = true };
        if (eql(v, "gl") or eql(v, "opengl")) return .{ .kind = .gl, .forced = true };
        if (eql(v, "vulkan") or eql(v, "vk")) return .{ .kind = .vulkan, .forced = true };
        if (eql(v, "metal")) return .{ .kind = .metal, .forced = true };
        if (eql(v, "d3d11") or eql(v, "d3d") or eql(v, "directx")) return .{ .kind = .d3d11, .forced = true };
        if (eql(v, "gpu")) return gpu();
    }
    return switch (asked) {
        .cpu => .{ .kind = .cpu },
        .gpu => gpu(),
    };
}

/// gpu is the platform's GPU API, as gpui's: Direct3D 11 on Windows, Metal
/// on macOS, Vulkan (falling back to OpenGL) elsewhere.
pub fn gpu() Choice {
    return .{ .kind = switch (builtin.os.tag) {
        .windows => .d3d11,
        .macos => .metal,
        else => .vulkan,
    } };
}

fn eql(a: []const u8, b: []const u8) bool {
    return std.mem.eql(u8, a, b);
}
