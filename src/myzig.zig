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

pub const ui = @import("ui.zig");
pub const geom = @import("geom.zig");
pub const text = @import("text.zig");
pub const scene = @import("scene.zig");
pub const raster = @import("raster.zig");
pub const Headless = @import("headless.zig").Headless;
pub const app = @import("app.zig");
pub const WindowOptions = app.WindowOptions;
pub const run = app.run;

test {
    _ = geom;
    _ = raster;
}
