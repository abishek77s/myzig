//! MyZig: desktop apps in Zig with native UI that MyZig draws itself, a port
//! of MyGo's native UI (github.com/egoist/mygo).
//!
//!     const myzig = @import("myzig");
//!     const ui = myzig.ui;
//!
//!     const Counter = struct { n: i64 = 0 };
//!
//!     fn view(s: *Counter, c: *ui.Context) void {
//!         c.column(.{ .fill = true, .center = true, .gap = 12 });
//!         defer c.end();
//!         c.textf("{d}", .{s.n}, .{ .size = 40, .bold = true });
//!         if (c.button("Increment", .{ .primary = true })) s.n += 1;
//!     }
//!
//!     pub fn main() !void {
//!         var s = Counter{};
//!         try myzig.run(&s, view, .{ .title = "Counter", .width = 360, .height = 260 });
//!     }

const std = @import("std");
pub const ui = @import("ui.zig");
pub const geom = @import("geom.zig");
pub const text = @import("text.zig");
pub const scene = @import("scene.zig");
pub const raster = @import("raster.zig");
pub const Headless = @import("headless.zig").Headless;
pub const Image = @import("image.zig").Image;
pub const env = @import("env.zig");
pub const app = @import("app.zig");
pub const WindowOptions = app.WindowOptions;

/// run opens a window showing view of state and runs the app until it
/// closes: Wayland or X11 on Linux (X11 on the BSDs), Win32 on Windows,
/// Cocoa on macOS.
pub const run = switch (@import("builtin").os.tag) {
    .windows => @import("win32.zig").run,
    .macos => @import("macos.zig").run,
    .linux => runLinux,
    else => app.run,
};

/// runLinux opens a Wayland window in a Wayland session (MYZIG_BACKEND=x11
/// or wayland chooses), and an X11 one otherwise or where Wayland cannot.
fn runLinux(state: anytype, comptime view: fn (@TypeOf(state), *ui.Context) void, opts: WindowOptions) !void {
    const wayland = @import("wayland.zig");
    if (wayland.wanted()) {
        if (wayland.run(state, view, opts)) |_| return else |e| {
            if (e != error.NoWayland) return e;
            std.log.info("myzig: no Wayland; opening an X11 window", .{});
        }
    }
    return app.run(state, view, opts);
}

test {
    _ = geom;
    _ = @import("image.zig");
    _ = raster;
    _ = @import("gpu/instances.zig");
    _ = @import("gpu/vulkan.zig");
    if (@import("builtin").os.tag == .macos) _ = @import("gpu/metal.zig");
    _ = ui;
    _ = scene;
    _ = text;
    _ = @import("textedit.zig");
    if (@import("builtin").os.tag == .linux) {
        _ = @import("x11/xim.zig");
        _ = @import("x11/xi.zig");
        _ = @import("wayland.zig");
    }
}
