//! What text editing needs apart from drawing: the rows a multi-line text
//! breaks into, and the history that undo and redo walk.

const std = @import("std");
const textm = @import("text.zig");

/// Row is one line on screen: a line of the text, or a part of one that
/// wrapped. end is before its newline; a soft row (one that wrapped) keeps
/// the spaces it broke at. line is the text's line it is in, from 0.
pub const Row = struct {
    start: u32,
    end: u32,
    line: u32,
    soft: bool = false,
};

/// wrap breaks s into rows: at its newlines, and when width is set at
/// spaces too so that each fits width (a word wider than it is broken
/// between characters). It returns the widest row's width.
pub fn wrap(rows: *std.ArrayListUnmanaged(Row), gpa: std.mem.Allocator, fonts: *textm.Fonts, weight: textm.Weight, size: f32, s: []const u8, width: ?f32) !f32 {
    rows.clearRetainingCapacity();
    const space = fonts.measure(weight, size, " ");
    var widest: f32 = 0;
    var line: u32 = 0;
    var a: u32 = 0;
    while (true) : (line += 1) {
        const b: u32 = @intCast(std.mem.indexOfScalarPos(u8, s, a, '\n') orelse s.len);
        if (width) |lim| {
            var start = a;
            while (true) {
                const whole = fonts.measure(weight, size, s[start..b]);
                if (whole <= lim) {
                    try rows.append(gpa, .{ .start = start, .end = b, .line = line });
                    widest = @max(widest, whole);
                    break;
                }
                // The words that fit, with the spaces after them.
                var e = start;
                var w: f32 = 0;
                var j = start;
                while (j < b) {
                    var k = j;
                    while (k < b and s[k] != ' ') k += 1;
                    const ww = fonts.measure(weight, size, s[j..k]);
                    if (w + ww > lim) break;
                    w += ww;
                    while (k < b and s[k] == ' ') : (k += 1) w += space;
                    e = k;
                    j = k;
                }
                if (e == start) {
                    // One word wider than the row: as much of it as fits.
                    e = start + fit(fonts, weight, size, s[start..b], lim);
                    w = fonts.prefixWidth(weight, size, s[start..b], e - start);
                }
                try rows.append(gpa, .{ .start = start, .end = e, .line = line, .soft = true });
                widest = @max(widest, @min(w, lim));
                start = e;
            }
        } else {
            try rows.append(gpa, .{ .start = a, .end = b, .line = line });
            widest = @max(widest, fonts.measure(weight, size, s[a..b]));
        }
        if (b >= s.len) break;
        a = b + 1;
    }
    return widest;
}

/// fit is how many bytes of s fit width, at least one code point.
fn fit(fonts: *textm.Fonts, weight: textm.Weight, size: f32, s: []const u8, width: f32) u32 {
    var i = fonts.indexAt(weight, size, s, width);
    while (i > 0 and fonts.prefixWidth(weight, size, s, i) > width) {
        i -= 1;
        while (i > 0 and s[i] & 0xc0 == 0x80) i -= 1;
    }
    if (i == 0 and s.len > 0) {
        i = 1;
        while (i < s.len and s[i] & 0xc0 == 0x80) i += 1;
    }
    return @intCast(i);
}

/// rowOf is the index of the row that offset at is in: the last that
/// starts at or before it.
pub fn rowOf(rows: []const Row, at: u32) usize {
    var lo: usize = 0;
    var hi: usize = rows.len;
    while (hi - lo > 1) {
        const mid = (lo + hi) / 2;
        if (rows[mid].start <= at) lo = mid else hi = mid;
    }
    return lo;
}

/// History is the edits made to a text, for undo and redo: each keeps the
/// bytes it removed and inserted. Typing coalesces by words.
pub const History = struct {
    edits: std.ArrayListUnmanaged(Edit) = .empty,
    bytes: std.ArrayListUnmanaged(u8) = .empty,
    /// done is how many edits undo can take back; the rest redo puts back.
    done: u32 = 0,
    /// joinable is set while typing may go onto the last edit.
    joinable: bool = false,

    pub const Edit = struct {
        at: u32,
        /// offsets and lengths in bytes
        removed: u32,
        removed_len: u32,
        inserted: u32,
        inserted_len: u32,
        /// where the caret and the anchor were before
        caret: u32,
        anchor: u32,
    };

    const max_bytes = 4 << 20;
    const max_edits = 4096;

    pub fn deinit(h: *History, gpa: std.mem.Allocator) void {
        h.edits.deinit(gpa);
        h.bytes.deinit(gpa);
    }

    pub fn clear(h: *History) void {
        h.edits.clearRetainingCapacity();
        h.bytes.clearRetainingCapacity();
        h.done = 0;
        h.joinable = false;
    }

    /// record notes that removed, at at, was replaced with inserted, the
    /// caret and anchor where they were before. What was undone is lost.
    pub fn record(h: *History, gpa: std.mem.Allocator, at: u32, removed: []const u8, inserted: []const u8, caret: u32, anchor: u32) !void {
        if (removed.len == 0 and inserted.len == 0) return;
        if (h.done < h.edits.items.len) {
            const first = h.edits.items[h.done];
            h.bytes.shrinkRetainingCapacity(first.removed);
            h.edits.shrinkRetainingCapacity(h.done);
            h.joinable = false;
        }
        // Typing goes onto the edit before it, until a word begins.
        const typing = removed.len == 0 and inserted.len <= 8 and std.mem.indexOfScalar(u8, inserted, '\n') == null;
        defer h.joinable = typing;
        if (h.joinable and typing and h.edits.items.len > 0) {
            const last = &h.edits.items[h.edits.items.len - 1];
            const tail = last.inserted + last.inserted_len == h.bytes.items.len;
            const after = at == last.at + last.inserted_len;
            const word_starts = last.inserted_len > 0 and isSpace(h.bytes.items[h.bytes.items.len - 1]) and !isSpace(inserted[0]);
            if (last.removed_len == 0 and last.inserted_len > 0 and tail and after and !word_starts) {
                try h.bytes.appendSlice(gpa, inserted);
                last.inserted_len += @intCast(inserted.len);
                return;
            }
        }
        const off: u32 = @intCast(h.bytes.items.len);
        try h.bytes.appendSlice(gpa, removed);
        try h.bytes.appendSlice(gpa, inserted);
        try h.edits.append(gpa, .{
            .at = at,
            .removed = off,
            .removed_len = @intCast(removed.len),
            .inserted = off + @as(u32, @intCast(removed.len)),
            .inserted_len = @intCast(inserted.len),
            .caret = caret,
            .anchor = anchor,
        });
        h.done = @intCast(h.edits.items.len);
        if (h.bytes.items.len > max_bytes or h.edits.items.len > max_edits) h.dropOldest();
    }

    /// breakGroup makes the next typing an edit of its own.
    pub fn breakGroup(h: *History) void {
        h.joinable = false;
    }

    fn isSpace(b: u8) bool {
        return b == ' ' or b == '\t' or b == '\n';
    }

    /// dropOldest forgets the older half of the history.
    fn dropOldest(h: *History) void {
        const keep_from = h.edits.items.len / 2;
        if (keep_from == 0) {
            h.clear();
            return;
        }
        const base = h.edits.items[keep_from].removed;
        const n = h.bytes.items.len - base;
        std.mem.copyForwards(u8, h.bytes.items[0..n], h.bytes.items[base..]);
        h.bytes.shrinkRetainingCapacity(n);
        const m = h.edits.items.len - keep_from;
        std.mem.copyForwards(Edit, h.edits.items[0..m], h.edits.items[keep_from..]);
        h.edits.shrinkRetainingCapacity(m);
        for (h.edits.items) |*e| {
            e.removed -= base;
            e.inserted -= base;
        }
        h.done = @intCast(m);
    }

    pub fn removedOf(h: *const History, e: Edit) []const u8 {
        return h.bytes.items[e.removed..][0..e.removed_len];
    }

    pub fn insertedOf(h: *const History, e: Edit) []const u8 {
        return h.bytes.items[e.inserted..][0..e.inserted_len];
    }
};

test "rows break at newlines, and at spaces to fit" {
    var fonts = try textm.Fonts.init(std.testing.allocator);
    defer fonts.deinit();
    var rows: std.ArrayListUnmanaged(Row) = .empty;
    defer rows.deinit(std.testing.allocator);
    const s = "one two three\n\nfour";
    _ = try wrap(&rows, std.testing.allocator, &fonts, .regular, 14, s, null);
    try std.testing.expectEqual(@as(usize, 3), rows.items.len);
    try std.testing.expectEqualStrings("one two three", s[rows.items[0].start..rows.items[0].end]);
    try std.testing.expectEqualStrings("", s[rows.items[1].start..rows.items[1].end]);
    try std.testing.expectEqual(@as(u32, 2), rows.items[2].line);
    // As wide as "one two": three goes on a row of its own.
    const w = fonts.measure(.regular, 14, "one two ");
    _ = try wrap(&rows, std.testing.allocator, &fonts, .regular, 14, s, w + 1);
    try std.testing.expectEqual(@as(usize, 4), rows.items.len);
    try std.testing.expectEqualStrings("one two ", s[rows.items[0].start..rows.items[0].end]);
    try std.testing.expect(rows.items[0].soft);
    try std.testing.expectEqualStrings("three", s[rows.items[1].start..rows.items[1].end]);
    try std.testing.expectEqual(@as(u32, 0), rows.items[1].line);
    try std.testing.expectEqual(@as(usize, 1), rowOf(rows.items, 10));
    try std.testing.expectEqual(@as(usize, 3), rowOf(rows.items, @intCast(s.len)));
    // A word wider than the row breaks between characters.
    _ = try wrap(&rows, std.testing.allocator, &fonts, .regular, 14, "abcdefghij", fonts.measure(.regular, 14, "abcd") + 0.5);
    try std.testing.expectEqual(@as(usize, 3), rows.items.len);
    try std.testing.expectEqual(@as(u32, 4), rows.items[0].end);
    // Text ending in a newline has an empty last row, for the caret.
    _ = try wrap(&rows, std.testing.allocator, &fonts, .regular, 14, "a\n", null);
    try std.testing.expectEqual(@as(usize, 2), rows.items.len);
}

test "history coalesces typing by words, and drops what was undone" {
    const gpa = std.testing.allocator;
    var h = History{};
    defer h.deinit(gpa);
    try h.record(gpa, 0, "", "a", 0, 0);
    try h.record(gpa, 1, "", "b", 1, 1);
    try h.record(gpa, 2, "", " ", 2, 2);
    try h.record(gpa, 3, "", "c", 3, 3);
    try std.testing.expectEqual(@as(usize, 2), h.edits.items.len);
    try std.testing.expectEqualStrings("ab ", h.insertedOf(h.edits.items[0]));
    try std.testing.expectEqualStrings("c", h.insertedOf(h.edits.items[1]));
    h.done = 1; // undone
    try h.record(gpa, 3, "", "d", 3, 3);
    try std.testing.expectEqual(@as(usize, 2), h.edits.items.len);
    try std.testing.expectEqualStrings("d", h.insertedOf(h.edits.items[1]));
    // Not after the last: an edit of its own.
    try h.record(gpa, 0, "a", "", 0, 1);
    try std.testing.expectEqual(@as(usize, 3), h.edits.items.len);
    try std.testing.expectEqualStrings("a", h.removedOf(h.edits.items[2]));
}
