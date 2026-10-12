//! Which renderer a window draws with: MYZIG_RENDERER=cpu, gl or vulkan,
//! or by default a GPU's where there is one and the CPU's elsewhere.

const std = @import("std");
const builtin = @import("builtin");
const env = @import("../env.zig");

pub const Kind = enum { cpu, gl, vulkan };

pub const Choice = struct {
    kind: Kind,
    /// forced is set when the user named the renderer: it is used even
    /// where OpenGL or Vulkan draws on the CPU.
    forced: bool,
};

pub fn choose() Choice {
    if (env.get("MYZIG_RENDERER")) |v| {
        if (std.mem.eql(u8, v, "cpu")) return .{ .kind = .cpu, .forced = true };
        if (std.mem.eql(u8, v, "gl") or std.mem.eql(u8, v, "opengl")) return .{ .kind = .gl, .forced = true };
        if (std.mem.eql(u8, v, "vulkan") or std.mem.eql(u8, v, "vk")) return .{ .kind = .vulkan, .forced = true };
    }
    return .{ .kind = if (hasGpu()) .gl else .cpu, .forced = false };
}

/// hasGpu reports whether a GPU is there to draw with. On Linux that is a
/// render node of a DRM driver or NVIDIA's device: without one, OpenGL
/// would load Mesa (some 50 MB) only to draw on the CPU. Windows always
/// has a driver, Microsoft's software one at worst, which the renderer
/// name tells apart.
fn hasGpu() bool {
    if (builtin.os.tag == .windows) return true;
    const paths = [_][]const u8{ "/dev/dri/renderD128", "/dev/nvidia0" };
    for (paths) |p| {
        std.fs.accessAbsolute(p, .{}) catch continue;
        return true;
    }
    return false;
}
