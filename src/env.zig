//! Environment variables on every platform. MyZig links libc, so on POSIX
//! systems they come from getenv; Windows' environment is UTF-16.

const std = @import("std");
const builtin = @import("builtin");

/// get returns the value of the variable name, or null. On Windows the
/// value is converted to UTF-8 and kept for the life of the process.
pub fn get(comptime name: []const u8) ?[:0]const u8 {
    if (builtin.os.tag == .windows) {
        const env: std.process.Environ = .{ .block = .global };
        const v = env.getWindows(std.unicode.utf8ToUtf16LeStringLiteral(name)) orelse return null;
        return std.unicode.wtf16LeToWtf8AllocZ(std.heap.page_allocator, v) catch null;
    }
    const v = std.c.getenv(name ++ "") orelse return null;
    return std.mem.span(v);
}

pub fn has(comptime name: []const u8) bool {
    return get(name) != null;
}
