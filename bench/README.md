# Benchmarks

`bench/bench.py` measures MyZig against [MyGo](https://github.com/egoist/mygo)
and [gpui-fast](https://github.com/longbridge/gpui-fast) on the same samples:

- **counter**: MyGo's `counter-native`, written in each framework
  (`examples/counter.zig`, `bench/go/counter`, `bench/gpui/src/bin/counter.rs`).
- **quotes**: a table of 40 rows of prices that change every frame.
- **list**: 10,000 rows of 36 px, scrolled.

```sh
python3 bench/bench.py setup     # clones MyGo and gpui-fast at the pinned commits into bench/.cache
python3 bench/bench.py all       # or one of: compile size memory window frames
```

Results go to `bench/results/latest.json`. The steps need Zig 0.15.2, Go,
Rust (stable), Xvfb, xdotool and GNU time.

## What each number is

- **compile**: a clean build (every cache removed) and an incremental one
  (the counter's title edited, the median of three), for the counter, in
  each framework's debug and release modes.
- **size**: the release executables (MyZig's ReleaseSmall and ReleaseFast;
  Go's `-trimpath -ldflags='-s -w'`; gpui-fast's release profile, thin LTO
  and stripped).
- **memory**: the proportional set size (PSS, `smaps_rollup`) of the window
  and its children, two seconds after it opened, the median of three runs.
- **window**: frames per second and the process's CPU time per frame,
  over ten seconds in Xvfb: quotes streaming at the display's rate (and,
  for MyZig, uncapped), and the list scrolled by 60 wheel notches a second.
- **frames**: headless frame cost, with no window: MyZig's `zig build bench`,
  MyGo's `go test -bench` with its `ui.Tester`, and gpui-fast's own
  `gpui_perf` harness running the same three scenarios
  (`bench/gpui/scenarios/myzig.rs`). gpui-fast's number is the CPU time
  before rasterization, which gpui leaves to the GPU; MyZig reports both.

The machine has no GPU, so windows draw with each framework's CPU path
(gpui-fast cannot open a window without one, so it has no window numbers).
