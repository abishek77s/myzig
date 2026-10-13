//! What MyZig needs from the operating system: the time, sleeping, whole
//! files, stderr and libraries opened at run time. Zig 0.16's std puts
//! these behind std.Io, whose vtable links every implementation it has
//! (DNS, processes, ...) into each binary; MyZig links libc, so this asks
//! libc, and Windows, directly.

const std = @import("std");
const builtin = @import("builtin");
const is_windows = builtin.os.tag == .windows;

const win = struct {
    extern "kernel32" fn QueryPerformanceCounter(count: *i64) callconv(.winapi) i32;
    extern "kernel32" fn QueryPerformanceFrequency(freq: *i64) callconv(.winapi) i32;
    extern "kernel32" fn Sleep(ms: u32) callconv(.winapi) void;
    extern "c" fn _wfopen(name: [*:0]const u16, mode: [*:0]const u16) ?*std.c.FILE;
};

/// nanos is the time on the monotonic clock, in nanoseconds.
pub fn nanos() u64 {
    if (is_windows) {
        var count: i64 = 0;
        var freq: i64 = 1;
        _ = win.QueryPerformanceCounter(&count);
        _ = win.QueryPerformanceFrequency(&freq);
        const c: u128 = @intCast(count);
        return @intCast(c * std.time.ns_per_s / @as(u128, @intCast(freq)));
    }
    var ts: std.c.timespec = undefined;
    _ = std.c.clock_gettime(.MONOTONIC, &ts);
    return @as(u64, @intCast(ts.sec)) * std.time.ns_per_s + @as(u64, @intCast(ts.nsec));
}

/// Timer measures time since it was started or last reset.
pub const Timer = struct {
    started: u64,

    pub fn start() error{}!Timer {
        return .{ .started = nanos() };
    }

    pub fn read(t: *const Timer) u64 {
        return nanos() -| t.started;
    }

    pub fn reset(t: *Timer) void {
        t.started = nanos();
    }

    pub fn lap(t: *Timer) u64 {
        const now = nanos();
        defer t.started = now;
        return now -| t.started;
    }
};

pub fn sleep(ns: u64) void {
    if (is_windows) return win.Sleep(@intCast(@max(1, ns / std.time.ns_per_ms)));
    var ts: std.c.timespec = .{ .sec = @intCast(ns / std.time.ns_per_s), .nsec = @intCast(ns % std.time.ns_per_s) };
    while (std.c.nanosleep(&ts, &ts) != 0) {}
}

fn fopen(path: []const u8, comptime mode: []const u8) !*std.c.FILE {
    if (is_windows) {
        var buf: [1024]u16 = undefined;
        const n = std.unicode.wtf8ToWtf16Le(buf[0 .. buf.len - 1], path) catch return error.FileNotFound;
        buf[n] = 0;
        return win._wfopen(buf[0..n :0], std.unicode.utf8ToUtf16LeStringLiteral(mode)) orelse error.FileNotFound;
    }
    var buf: [4096]u8 = undefined;
    if (path.len >= buf.len) return error.NameTooLong;
    @memcpy(buf[0..path.len], path);
    buf[path.len] = 0;
    return std.c.fopen(buf[0..path.len :0], mode ++ "") orelse error.FileNotFound;
}

/// readFile reads the file at path, of at most limit bytes.
pub fn readFile(gpa: std.mem.Allocator, path: []const u8, limit: usize) ![]u8 {
    const f = try fopen(path, "rb");
    defer _ = std.c.fclose(f);
    var bytes: std.ArrayList(u8) = .empty;
    errdefer bytes.deinit(gpa);
    while (true) {
        try bytes.ensureUnusedCapacity(gpa, 64 << 10);
        const room = bytes.unusedCapacitySlice();
        const n = std.c.fread(room.ptr, 1, room.len, f);
        bytes.items.len += n;
        if (bytes.items.len > limit) return error.FileTooBig;
        if (n < room.len) break;
    }
    return bytes.toOwnedSlice(gpa);
}

/// writeFile replaces the file at path with data.
pub fn writeFile(path: []const u8, data: []const u8) !void {
    var buf: [0]u8 = undefined;
    var f = try create(path, &buf);
    defer f.close();
    try f.interface.writeAll(data);
    try f.interface.flush();
}

/// exists says whether there is a file at path.
pub fn exists(path: []const u8) bool {
    if (is_windows) {
        const f = fopen(path, "rb") catch return false;
        _ = std.c.fclose(f);
        return true;
    }
    var buf: [4096]u8 = undefined;
    if (path.len >= buf.len) return false;
    @memcpy(buf[0..path.len], path);
    buf[path.len] = 0;
    return std.c.access(buf[0..path.len :0], std.c.F_OK) == 0;
}

/// File is a file being written, through interface.
pub const File = struct {
    file: *std.c.FILE,
    interface: std.Io.Writer,
    failed: bool = false,

    pub fn close(f: *File) void {
        _ = std.c.fclose(f.file);
    }

    fn drain(w: *std.Io.Writer, data: []const []const u8, splat: usize) std.Io.Writer.Error!usize {
        const f: *File = @alignCast(@fieldParentPtr("interface", w));
        try f.put(w.buffered());
        w.end = 0;
        var n: usize = 0;
        for (data[0 .. data.len - 1]) |d| {
            try f.put(d);
            n += d.len;
        }
        const last = data[data.len - 1];
        for (0..splat) |_| try f.put(last);
        return n + last.len * splat;
    }

    fn put(f: *File, bytes: []const u8) std.Io.Writer.Error!void {
        if (bytes.len == 0) return;
        if (std.c.fwrite(bytes.ptr, 1, bytes.len, f.file) != bytes.len) return error.WriteFailed;
    }
};

/// create makes (or truncates) the file at path, for writing through
/// the returned File's interface, with buffer.
pub fn create(path: []const u8, buffer: []u8) !File {
    return .{ .file = try fopen(path, "wb"), .interface = .{ .buffer = buffer, .vtable = &.{ .drain = File.drain } } };
}

// File descriptors, on POSIX systems, which std.posix no longer wraps
// all of.

pub fn close(fd: std.c.fd_t) void {
    _ = std.c.close(fd);
}

pub fn write(fd: std.c.fd_t, bytes: []const u8) !usize {
    while (true) {
        const n = std.c.write(fd, bytes.ptr, bytes.len);
        if (n >= 0) return @intCast(n);
        if (std.c.errno(n) == .INTR) continue;
        return error.WriteFailed;
    }
}

/// pipe returns the read and write ends of a new close-on-exec pipe.
pub fn pipe() ![2]std.c.fd_t {
    var fds: [2]std.c.fd_t = undefined;
    if (std.c.pipe2(&fds, .{ .CLOEXEC = true }) != 0) return error.NoPipe;
    return fds;
}

pub fn ftruncate(fd: std.c.fd_t, size: usize) !void {
    if (std.c.ftruncate(fd, @intCast(size)) != 0) return error.Truncate;
}

/// DynLib is a library opened at run time: std.DynLib, and on Windows,
/// which Zig 0.16's std leaves out, LoadLibrary.
pub const DynLib = if (is_windows) WinLib else std.DynLib;

const WinLib = struct {
    const windows = std.os.windows;
    extern "kernel32" fn LoadLibraryW(name: [*:0]const u16) callconv(.winapi) ?windows.HMODULE;
    extern "kernel32" fn GetProcAddress(m: windows.HMODULE, name: [*:0]const u8) callconv(.winapi) ?*const anyopaque;
    extern "kernel32" fn FreeLibrary(m: windows.HMODULE) callconv(.winapi) windows.BOOL;

    module: windows.HMODULE,

    pub fn open(path: []const u8) !WinLib {
        var buf: [260]u16 = undefined;
        const n = std.unicode.wtf8ToWtf16Le(buf[0 .. buf.len - 1], path) catch return error.FileNotFound;
        buf[n] = 0;
        return .{ .module = LoadLibraryW(buf[0..n :0]) orelse return error.FileNotFound };
    }

    pub fn close(l: *WinLib) void {
        _ = FreeLibrary(l.module);
    }

    pub fn lookup(l: *WinLib, comptime T: type, name: [:0]const u8) ?T {
        return @ptrCast(@alignCast(GetProcAddress(l.module, name.ptr) orelse return null));
    }
};

/// print writes a message to stderr: std.debug.print and std.log go
/// through std.Io.
pub fn print(comptime fmt: []const u8, args: anytype) void {
    var buf: [2048]u8 = undefined;
    const s = std.fmt.bufPrint(&buf, fmt, args) catch &buf;
    writeStderr(s);
}

/// log is std.log's, through print: messages under the build's log level
/// are left out.
pub fn log(comptime level: std.log.Level, comptime fmt: []const u8, args: anytype) void {
    if (comptime !std.log.logEnabled(level, .default)) return;
    logFn(level, .default, fmt, args);
}

/// out writes s to stdout.
pub fn out(s: []const u8) void {
    writeStd(1, s);
}

fn writeStderr(s: []const u8) void {
    writeStd(2, s);
}

fn writeStd(comptime fd: u2, s: []const u8) void {
    if (is_windows) {
        const w = struct {
            extern "kernel32" fn GetStdHandle(n: u32) callconv(.winapi) ?*anyopaque;
            extern "kernel32" fn WriteFile(h: *anyopaque, buf: [*]const u8, n: u32, written: ?*u32, overlapped: ?*anyopaque) callconv(.winapi) i32;
        };
        const h = w.GetStdHandle(@bitCast(@as(i32, if (fd == 1) -11 else -12))) orelse return;
        var written: u32 = 0;
        _ = w.WriteFile(h, s.ptr, @intCast(s.len), &written, null);
        return;
    }
    var rest = s;
    while (rest.len > 0) {
        const n = write(fd, rest) catch return;
        rest = rest[n..];
    }
}

/// exeName is the file name of the running program, on Linux.
pub fn exeName(buf: []u8) []const u8 {
    const n = std.c.readlink("/proc/self/exe", buf.ptr, buf.len);
    if (n <= 0) return "myzig";
    return std.fs.path.basename(buf[0..@intCast(n)]);
}

/// panicFn is a panic handler for release builds: the message, then
/// abort. Zig's default prints a stack trace, which links a DWARF reader.
pub fn panicFn(msg: []const u8, _: ?usize) noreturn {
    @branchHint(.cold);
    print("panic: {s}\n", .{msg});
    std.c.abort();
}

/// logFn is a std.options.logFn writing with print.
pub fn logFn(comptime level: std.log.Level, comptime scope: @EnumLiteral(), comptime fmt: []const u8, args: anytype) void {
    const prefix = if (scope == .default) "" else "(" ++ @tagName(scope) ++ ") ";
    print(level.asText() ++ ": " ++ prefix ++ fmt ++ "\n", args);
}
