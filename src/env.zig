//! Environment variables on every platform: std.posix.getenv does not
//! exist on Windows, whose environment is UTF-16.

const std = @import("std");
const builtin = @import("builtin");

/// get returns the value of the variable name, or null. On Windows the
/// value is converted once and kept for the life of the process.
pub fn get(comptime name: []const u8) ?[]const u8 {
    if (builtin.os.tag == .windows) {
        return std.process.getEnvVarOwned(std.heap.page_allocator, name) catch null;
    }
    return std.posix.getenv(name);
}

pub fn has(comptime name: []const u8) bool {
    return get(name) != null;
}
