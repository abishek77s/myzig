//! Images: decoded once (PNG, JPEG, GIF's first frame, BMP) into
//! premultiplied pixels, then drawn by every renderer at any size, with
//! rounded corners. An image belongs to the app, which keeps it while the
//! view shows it:
//!
//!     s.logo = try myzig.Image.decode(gpa, @embedFile("logo.png"));
//!     ...
//!     c.image(s.logo, .{ .width = 64, .height = 64, .radius = 12 });

const std = @import("std");

extern fn mz_image_decode(data: [*]const u8, len: c_int, w: *c_int, h: *c_int) ?[*]u8;
extern fn mz_image_error() ?[*:0]const u8;
extern fn mz_image_free(p: [*]u8) void;

var next_id = std.atomic.Value(u32).init(1);

pub const Image = struct {
    gpa: std.mem.Allocator,
    /// id names the image to the renderers' caches.
    id: u32,
    width: u32,
    height: u32,
    /// pixels are 0xAARRGGBB, premultiplied by alpha, row by row.
    pixels: []u32,
    /// opaque is set when every pixel is.
    @"opaque": bool,
    /// small is a copy no larger than a GPU atlas takes, made when a GPU
    /// renderer first draws an image larger than that.
    small: ?*Image = null,

    /// decode decodes an encoded image (PNG, JPEG, GIF, BMP).
    pub fn decode(gpa: std.mem.Allocator, bytes: []const u8) !*Image {
        var w: c_int = 0;
        var h: c_int = 0;
        const rgba = mz_image_decode(bytes.ptr, @intCast(bytes.len), &w, &h) orelse return error.BadImage;
        defer mz_image_free(rgba);
        return fromRgba(gpa, @intCast(w), @intCast(h), rgba[0..@as(usize, @intCast(w * h * 4))]);
    }

    /// load reads and decodes the image file at path.
    pub fn load(gpa: std.mem.Allocator, path: []const u8) !*Image {
        const bytes = try std.fs.cwd().readFileAlloc(gpa, path, 256 << 20);
        defer gpa.free(bytes);
        return decode(gpa, bytes);
    }

    /// fromRgba makes an image of RGBA bytes (straight alpha), w×h.
    pub fn fromRgba(gpa: std.mem.Allocator, w: u32, h: u32, rgba: []const u8) !*Image {
        const img = try gpa.create(Image);
        errdefer gpa.destroy(img);
        const px = try gpa.alloc(u32, @as(usize, w) * h);
        var all_opaque = true;
        for (px, 0..) |*p, i| {
            const r: u32 = rgba[i * 4];
            const g: u32 = rgba[i * 4 + 1];
            const b: u32 = rgba[i * 4 + 2];
            const a: u32 = rgba[i * 4 + 3];
            if (a != 255) all_opaque = false;
            p.* = a << 24 | mul(r, a) << 16 | mul(g, a) << 8 | mul(b, a);
        }
        img.* = .{ .gpa = gpa, .id = next_id.fetchAdd(1, .monotonic), .width = w, .height = h, .pixels = px, .@"opaque" = all_opaque };
        return img;
    }

    pub fn deinit(img: *Image) void {
        if (img.small) |s| s.deinit();
        img.gpa.free(img.pixels);
        img.gpa.destroy(img);
    }

    fn mul(c: u32, a: u32) u32 {
        return (c * a + 127) / 255;
    }

    /// fitted is the image, or a copy shrunk to fit max texels a side.
    pub fn fitted(img: *Image, max: u32) *Image {
        if (img.width <= max and img.height <= max) return img;
        if (img.small) |s| return s;
        const k = @max(@as(f32, @floatFromInt(img.width)) / @as(f32, @floatFromInt(max)), @as(f32, @floatFromInt(img.height)) / @as(f32, @floatFromInt(max)));
        const w: u32 = @max(1, @as(u32, @intFromFloat(@as(f32, @floatFromInt(img.width)) / k)));
        const h: u32 = @max(1, @as(u32, @intFromFloat(@as(f32, @floatFromInt(img.height)) / k)));
        const s = img.gpa.create(Image) catch return img;
        const px = img.gpa.alloc(u32, @as(usize, w) * h) catch {
            img.gpa.destroy(s);
            return img;
        };
        // A box filter: each texel is the mean of the pixels it covers.
        for (0..h) |y| for (0..w) |x| {
            const sx0: usize = x * img.width / w;
            const sx1: usize = @max(sx0 + 1, (x + 1) * img.width / w);
            const sy0: usize = y * img.height / h;
            const sy1: usize = @max(sy0 + 1, (y + 1) * img.height / h);
            var sum = [4]u64{ 0, 0, 0, 0 };
            for (sy0..sy1) |sy| for (img.pixels[sy * img.width ..][sx0..sx1]) |p| {
                inline for (0..4) |c| sum[c] += (p >> (c * 8)) & 0xff;
            };
            const n: u64 = (sx1 - sx0) * (sy1 - sy0);
            var v: u32 = 0;
            inline for (0..4) |c| v |= @as(u32, @intCast((sum[c] + n / 2) / n)) << (c * 8);
            px[y * w + x] = v;
        };
        s.* = .{ .gpa = img.gpa, .id = next_id.fetchAdd(1, .monotonic), .width = w, .height = h, .pixels = px, .@"opaque" = img.@"opaque" };
        img.small = s;
        return s;
    }

    /// sample is the image at (u, v) in texels, bilinear, premultiplied.
    pub inline fn sample(img: *const Image, u: f32, v: f32) u32 {
        const fx = std.math.clamp(u - 0.5, 0, @as(f32, @floatFromInt(img.width - 1)));
        const fy = std.math.clamp(v - 0.5, 0, @as(f32, @floatFromInt(img.height - 1)));
        const x0: u32 = @intFromFloat(fx);
        const y0: u32 = @intFromFloat(fy);
        const x1 = @min(x0 + 1, img.width - 1);
        const y1 = @min(y0 + 1, img.height - 1);
        const tx: u32 = @intFromFloat((fx - @as(f32, @floatFromInt(x0))) * 256);
        const ty: u32 = @intFromFloat((fy - @as(f32, @floatFromInt(y0))) * 256);
        const w = img.width;
        const a = img.pixels[y0 * w + x0];
        const b = img.pixels[y0 * w + x1];
        const c = img.pixels[y1 * w + x0];
        const d = img.pixels[y1 * w + x1];
        var out: u32 = 0;
        inline for (0..4) |k| {
            const sh = k * 8;
            const top = ((a >> sh) & 0xff) * (256 - tx) + ((b >> sh) & 0xff) * tx;
            const bot = ((c >> sh) & 0xff) * (256 - tx) + ((d >> sh) & 0xff) * tx;
            out |= (((top * (256 - ty) + bot * ty) + 32768) >> 16) << sh;
        }
        return out;
    }
};

test "a PNG decodes to premultiplied pixels" {
    // 2×1: opaque red, and white at half alpha.
    const png = [_]u8{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0xf4, 0x22, 0x7f,
        0x8a, 0x00, 0x00, 0x00, 0x0f, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
        0x1f, 0x08, 0x1a, 0x00, 0x16, 0x73, 0x05, 0x7c, 0x9f, 0xec, 0xbd, 0x95, 0x00, 0x00, 0x00, 0x00,
        0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
    };
    const img = try Image.decode(std.testing.allocator, &png);
    defer img.deinit();
    try std.testing.expectEqual(@as(u32, 2), img.width);
    try std.testing.expectEqual(@as(u32, 1), img.height);
    try std.testing.expectEqual(@as(u32, 0xffff0000), img.pixels[0]);
    try std.testing.expectEqual(@as(u32, 0x80808080), img.pixels[1]);
    try std.testing.expect(!img.@"opaque");
    try std.testing.expectError(error.BadImage, Image.decode(std.testing.allocator, "not an image"));
}

test "a large image fits a GPU atlas as a shrunk copy" {
    const gpa = std.testing.allocator;
    const rgba = try gpa.alloc(u8, 300 * 100 * 4);
    defer gpa.free(rgba);
    @memset(rgba, 200);
    const img = try Image.fromRgba(gpa, 300, 100, rgba);
    defer img.deinit();
    const s = img.fitted(150);
    try std.testing.expectEqual(@as(u32, 150), s.width);
    try std.testing.expectEqual(@as(u32, 50), s.height);
    try std.testing.expect(s.id != img.id);
    try std.testing.expectEqual(img.pixels[0], s.pixels[0]);
    try std.testing.expectEqual(s, img.fitted(150));
}
