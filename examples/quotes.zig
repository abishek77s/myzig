//! Quotes streams prices into a table that changes every frame, the
//! "streaming quotes" workload of gpui-fast's benchmarks.
//!
//!     zig build quotes
const std = @import("std");
const myzig = @import("myzig");
const ui = myzig.ui;

pub const rows = 40;

pub const Quotes = struct {
    symbols: [rows][4]u8 = undefined,
    price: [rows]f64 = undefined,
    open: [rows]f64 = undefined,
    volume: [rows]u64 = undefined,
    rng: std.Random.DefaultPrng = std.Random.DefaultPrng.init(42),
    ticks: u64 = 0,

    pub fn init() Quotes {
        var q = Quotes{};
        const r = q.rng.random();
        for (0..rows) |i| {
            for (&q.symbols[i]) |*ch| ch.* = 'A' + r.uintLessThan(u8, 26);
            q.price[i] = 20 + r.float(f64) * 480;
            q.open[i] = q.price[i];
            q.volume[i] = r.uintLessThan(u64, 1_000_000);
        }
        return q;
    }

    /// tick moves a quarter of the prices, as a feed does.
    pub fn tick(q: *Quotes) void {
        const r = q.rng.random();
        q.ticks += 1;
        for (0..rows / 4) |_| {
            const i = r.uintLessThan(usize, rows);
            q.price[i] *= 1 + (r.float(f64) - 0.5) * 0.004;
            q.volume[i] += r.uintLessThan(u64, 500);
        }
    }
};

pub fn view(q: *Quotes, c: *ui.Context) void {
    q.tick();
    c.animationFrame();
    const t = c.theme;
    c.column(.{ .fill = true, .padding = 16, .gap = 2 });
    defer c.end();
    c.textf("Streaming quotes · tick {d}", .{q.ticks}, .{ .size = 18, .bold = true });
    header(c);
    for (0..rows) |i| {
        c.row(.{ .gap = 12, .padding_x = 8, .height = 22, .@"align" = .center, .background = if (i % 2 == 0) t.surface else .transparent, .radius = 4 });
        defer c.end();
        const ch = q.price[i] - q.open[i];
        const pct = ch / q.open[i] * 100;
        const col = if (ch >= 0) t.success else t.danger;
        c.text(&q.symbols[i], .{ .width = 80, .bold = true });
        c.textf("{d:.2}", .{q.price[i]}, .{ .width = 90, .@"align" = .end });
        c.textf("{d:.2}", .{ch}, .{ .width = 80, .@"align" = .end, .color = col });
        c.textf("{d:.2}%", .{pct}, .{ .width = 80, .@"align" = .end, .color = col });
        c.textf("{d}", .{q.volume[i]}, .{ .width = 100, .@"align" = .end, .color = t.text_muted });
    }
}

fn header(c: *ui.Context) void {
    const t = c.theme;
    c.row(.{ .gap = 12, .padding_x = 8, .height = 24, .@"align" = .center });
    defer c.end();
    c.text("Symbol", .{ .width = 80, .color = t.text_muted, .size = 12 });
    c.text("Price", .{ .width = 90, .@"align" = .end, .color = t.text_muted, .size = 12 });
    c.text("Change", .{ .width = 80, .@"align" = .end, .color = t.text_muted, .size = 12 });
    c.text("%", .{ .width = 80, .@"align" = .end, .color = t.text_muted, .size = 12 });
    c.text("Volume", .{ .width = 100, .@"align" = .end, .color = t.text_muted, .size = 12 });
}

pub fn main() !void {
    var q = Quotes.init();
    try myzig.run(&q, view, .{ .title = "Quotes", .width = 560, .height = 1040 });
}
