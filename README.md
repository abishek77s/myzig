# MyZig

**Desktop apps in Zig with native UI that MyZig draws itself.** A port of
the native UI of [MyGo](https://github.com/egoist/mygo) to Zig: views are Zig
functions of the app's state, laid out with flexbox, drawn on the CPU and
shown in an X11 window, with no webview, no GPU driver and no toolkit.

```zig
const myzig = @import("myzig");
const ui = myzig.ui;

const Counter = struct { n: i64 = 0 };

fn view(s: *Counter, c: *ui.Context) void {
    c.column(.{ .fill = true, .center = true, .gap = 16 });
    defer c.end();
    c.textf("{d}", .{s.n}, .{ .size = 56, .bold = true });
    {
        c.row(.{ .gap = 8 });
        defer c.end();
        if (c.button("−", .{ .key = "decrement", .width = 44 })) s.n -= 1;
        if (c.button("Reset", .{ .disabled = s.n == 0 })) s.n = 0;
        if (c.button("+", .{ .key = "increment", .primary = true, .width = 44 })) s.n += 1;
    }
}

pub fn main() !void {
    var s = Counter{};
    try myzig.run(&s, view, .{ .title = "Counter", .width = 360, .height = 260 });
}
```

The same counter as MyGo's `examples/counter-native`, on the left in MyZig and
on the right in MyGo:

| MyZig | MyGo |
| --- | --- |
| ![MyZig counter](docs/counter-myzig.png) | ![MyGo counter](docs/counter-mygo.png) |

## Getting started

With [Zig](https://ziglang.org/download/) 0.15.2 and the X11 development
libraries (`libx11-dev libxext-dev` on Debian and Ubuntu):

```sh
zig build counter                        # MyGo's counter-native
zig build quotes                         # a table of prices streaming every frame
AUTOSCROLL=1 zig build list              # 10,000 rows scrolling themselves
zig build test                           # the views, clicked and typed at without a window
zig build bench -Doptimize=ReleaseFast   # what a frame costs, headless
```

`MYZIG_STATS=1` prints each second the frames drawn and what each cost,
`MYZIG_SCALE=2` draws at twice the density, and `MYZIG_UNCAPPED=1` draws as
fast as the X server takes frames instead of at 60 Hz.

## How it works

```
 view (Zig) ──► ui: build, measure, arrange, paint ──► scene: ops, hashed ──► renderer: diff, damage, CPU raster ──► X11 (MIT-SHM)
```

- **Views are rebuilt, pixels are not.** A view runs only when input, a
  resize or an animation asks for a frame; nothing polls, and an idle window
  takes no CPU. The tree it builds lives in arrays the context reuses, so a
  frame that builds what the last one built allocates nothing.
- **Damage from hashes.** Painting writes a display list; every op is
  hashed with its content, place and clip. The renderer compares the hashes
  with the last frame's and draws again only the rectangles of ops that
  came, went or changed, merged so that no pixel is drawn twice, and the
  window presents only those rectangles from shared memory.
- **Scroll layers.** A scrolled list is a layer whose ops hash where they
  are in its content, so scrolling changes none of them: the X server
  moves the layer's pixels (`XCopyArea`) and MyZig draws only the strip
  that scrolled into view, the idea of
  [gpui-fast](https://github.com/longbridge/gpui-fast)'s scroll layers on
  the CPU.
- **Retained text.** Glyphs are rasterized once per face, size and quarter
  pixel, and text widths are memoized, so a label that comes back frame
  after frame costs a hash lookup.
- **Clicks show in the frame that reports them.** When a view acts on
  input (a button reports a click, a shortcut fires), the frame is built
  again before it paints, as MyGo rebuilds after callback actions.
- **Small.** Fonts are drawn with
  [stb_truetype](https://github.com/nothings/stb) behind a five-function C
  surface, and Xlib is declared by hand rather than translated from its
  headers, which keeps C translation out of every build.

`myzig.Headless` runs a view without a window, as MyGo's `ui.Tester` does:
it clicks labels, presses keys and scrolls, and its frames are the pixels a
window would show. The list's test checks that frames drawn incrementally
while scrolling match frames drawn from scratch, pixel for pixel.

## Benchmarks

See [bench/README.md](bench/README.md) for the numbers, how they were
measured and how to reproduce them.

## What is ported

MyGo is a whole desktop framework: web pages in the system webview with
typed IPC, native UI with dozens of widgets, menus, dialogs, tray, updates,
installers, and macOS, Windows, Linux and browser backends. MyZig ports the
core of its native UI on Linux X11: the view model, flexbox rows and
columns, text, buttons, virtualized lists, keyboard shortcuts, the CPU
renderer with damage tracking, and the headless tester. Webviews, text
editing, the other widgets, and other platforms are not ported yet.

## License

MIT. MyZig is a port of MyGo (MIT, © MyGo contributors) and borrows ideas
from gpui-fast (Apache-2.0). stb_truetype is public domain.
