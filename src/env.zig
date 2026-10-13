//! Environment variables on every platform. MyZig links libc, so on POSIX
//! systems they come from getenv; Windows' environment is UTF-16.

const std = @import("std");
const builtin = @import("builtin");

/// get returns the value of the variable name, or null. On Windows the
/// value is converted to UTF-8 and kept for the life of the process.
pub fn get(comptime name: []const u8) ?[:0]const u8 {
    if (builtin.os.tag == .windows) {
        const k32 = struct {
            extern "kernel32" fn GetEnvironmentVariableW(name: [*:0]const u16, buf: ?[*]u16, size: u32) callconv(.winapi) u32;
        };
        const key = std.unicode.utf8ToUtf16LeStringLiteral(name);
        var buf: [1024]u16 = undefined;
        const n = k32.GetEnvironmentVariableW(key, &buf, buf.len);
        if (n == 0 or n >= buf.len) return null;
        return std.unicode.wtf16LeToWtf8AllocZ(std.heap.page_allocator, buf[0..n]) catch null;
    }
    const v = std.c.getenv(name ++ "") orelse return null;
    return std.mem.span(v);
}

pub fn has(comptime name: []const u8) bool {
    return get(name) != null;
}
