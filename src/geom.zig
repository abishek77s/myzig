//! Rectangles and colors shared by the toolkit, the scene and the renderer.

const std = @import("std");

pub const Rect = struct {
    x: f32 = 0,
    y: f32 = 0,
    w: f32 = 0,
    h: f32 = 0,

    pub fn contains(r: Rect, px: f32, py: f32) bool {
        return px >= r.x and py >= r.y and px < r.x + r.w and py < r.y + r.h;
    }

    pub fn isEmpty(r: Rect) bool {
        return r.w <= 0 or r.h <= 0;
    }

    pub fn inset(r: Rect, d: f32) Rect {
        return .{ .x = r.x + d, .y = r.y + d, .w = @max(0, r.w - 2 * d), .h = @max(0, r.h - 2 * d) };
    }
};

/// IRect is a rectangle of whole device pixels, what the renderer and the
/// damage tracker work in.
pub const IRect = struct {
    x0: i32 = 0,
    y0: i32 = 0,
    x1: i32 = 0,
    y1: i32 = 0,

    pub fn fromRect(r: Rect, scale: f32) IRect {
        return .{
            .x0 = @intFromFloat(@floor(r.x * scale)),
            .y0 = @intFromFloat(@floor(r.y * scale)),
            .x1 = @intFromFloat(@ceil((r.x + r.w) * scale)),
            .y1 = @intFromFloat(@ceil((r.y + r.h) * scale)),
        };
    }

    pub fn isEmpty(r: IRect) bool {
        return r.x1 <= r.x0 or r.y1 <= r.y0;
    }

    pub fn intersect(a: IRect, b: IRect) IRect {
        return .{ .x0 = @max(a.x0, b.x0), .y0 = @max(a.y0, b.y0), .x1 = @min(a.x1, b.x1), .y1 = @min(a.y1, b.y1) };
    }

    pub fn unite(a: IRect, b: IRect) IRect {
        if (a.isEmpty()) return b;
        if (b.isEmpty()) return a;
        return .{ .x0 = @min(a.x0, b.x0), .y0 = @min(a.y0, b.y0), .x1 = @max(a.x1, b.x1), .y1 = @max(a.y1, b.y1) };
    }

    pub fn overlaps(a: IRect, b: IRect) bool {
        return !a.intersect(b).isEmpty();
    }

    pub fn area(r: IRect) i64 {
        if (r.isEmpty()) return 0;
        return @as(i64, r.x1 - r.x0) * @as(i64, r.y1 - r.y0);
    }
};

/// Color is straight (not premultiplied) 8-bit RGBA.
pub const Color = packed struct(u32) {
    b: u8 = 0,
    g: u8 = 0,
    r: u8 = 0,
    a: u8 = 255,

    pub const transparent: Color = .{ .a = 0 };
    pub const white: Color = rgb(255, 255, 255);
    pub const black: Color = rgb(0, 0, 0);

    pub fn rgb(r: u8, g: u8, b: u8) Color {
        return .{ .r = r, .g = g, .b = b, .a = 255 };
    }

    pub fn rgba(r: u8, g: u8, b: u8, a: f32) Color {
        return .{ .r = r, .g = g, .b = b, .a = @intFromFloat(@round(std.math.clamp(a, 0, 1) * 255)) };
    }

    /// hex parses "#rrggbb" at compile time.
    pub fn hex(comptime s: []const u8) Color {
        comptime {
            std.debug.assert(s.len == 7 and s[0] == '#');
            var v: u24 = 0;
            for (s[1..]) |ch| v = v * 16 + switch (ch) {
                '0'...'9' => ch - '0',
                'a'...'f' => ch - 'a' + 10,
                'A'...'F' => ch - 'A' + 10,
                else => unreachable,
            };
            return rgb(@truncate(v >> 16), @truncate(v >> 8), @truncate(v));
        }
    }

    pub fn withAlpha(c: Color, a: f32) Color {
        var out = c;
        out.a = @intFromFloat(@round(@as(f32, @floatFromInt(c.a)) * std.math.clamp(a, 0, 1)));
        return out;
    }

    pub fn eql(a: Color, b: Color) bool {
        return @as(u32, @bitCast(a)) == @as(u32, @bitCast(b));
    }
};

test "irect" {
    const a = IRect{ .x0 = 0, .y0 = 0, .x1 = 10, .y1 = 10 };
    const b = IRect{ .x0 = 5, .y0 = 5, .x1 = 20, .y1 = 20 };
    try std.testing.expect(a.overlaps(b));
    try std.testing.expectEqual(@as(i64, 25), a.intersect(b).area());
    try std.testing.expectEqual(@as(i32, 20), a.unite(b).x1);
}
