# Platform layers: MyZig and gpui-fast

What each OS layer of MyZig does next to gpui-fast's (`crates/gpui_linux`,
`gpui_windows`, `gpui_macos`, `gpui_apple`, `gpui_wgpu` at the commit
`bench/bench.py setup` pins), read for the places a window can fail: a
crash, a hang, input that goes missing, a frame that never shows. ✓ means
MyZig handles it; the notes say how, or what is still missing.

## Linux

| | gpui-fast | MyZig |
| --- | --- | --- |
| Display servers | X11 (x11rb) and native Wayland | ✓ native Wayland in a Wayland session (libwayland-client opened at run time, bindings generated from the protocol XML), X11 (Xlib) otherwise; `MYZIG_BACKEND` chooses |
| Protocol errors | x11rb returns them as values; logged | ✓ an error handler logs them (Xlib's default exits the process) |
| CPU presentation | none: it always draws with the GPU (wgpu) | ✓ MIT-SHM, and plain XPutImage when the server cannot attach the segment (SSH, remote X); on Wayland, three `wl_shm` buffers, each brought up to date with only what changed |
| GPU on Wayland | wgpu (Vulkan) | ✓ Vulkan (`VK_KHR_wayland_surface`), else OpenGL through EGL |
| Decorations | server-side where offered, else its own | ✓ server-side through xdg-decoration where offered; else (GNOME) MyZig's title bar: drag to move, double click to maximize, right click for the window menu, minimize, maximize and close buttons, and the edges resize |
| Frame pacing on Wayland | frame callbacks | ✓ frame callbacks, for every renderer: a hidden window draws nothing, so no present can wait on it |
| Scale on Wayland | integer and fractional | ✓ the output's (or preferred) integer scale, and fractional scales through wp_fractional_scale and a viewport |
| Key repeat on Wayland | its own timer, at the compositor's rate | ✓ the same |
| Hidden windows | Unmap/Visibility stop drawing | ✓ UnmapNotify stops drawing until the next Expose |
| GPU lost | wgpu device recreated | ✓ the window goes on drawing on the CPU |
| Keyboard | XKB through xkbcommon, layout changes | X11: core keysyms, so Ctrl shortcuts on non-Latin layouts may not match. Wayland: xkbcommon (opened at run time) with the compositor's keymap, dead keys and compose sequences |
| Text input | XIM with pre-edit; text-input-v3 on Wayland | ✓ XIM: what is composed shows in the input (callbacks), or the method draws it at the caret; its candidate window follows the caret. Wayland: text-input-v3, the same |
| Mouse | every button | ✓ left, right, middle, back and forward; double and triple clicks |
| Scrolling | XInput 2 smooth deltas, horizontal | ✓ XInput 2.1 smooth deltas both ways (libXi opened at run time); wheel buttons 4 to 7 without it; Shift scrolls sideways |
| Clipboard | CLIPBOARD and PRIMARY, INCR for large data | ✓ CLIPBOARD, INCR both ways; Wayland's data device both ways; no PRIMARY (middle-click paste) |
| Cursor | cursor-shape-v1, else the cursor theme | ✓ the same (libwayland-cursor opened at run time) |

## Windows

| | gpui-fast | MyZig |
| --- | --- | --- |
| Renderer | DirectX 11 | Vulkan, else OpenGL (WGL), else the CPU (GDI) |
| Device lost | `WM_GPUI_GPU_DEVICE_LOST`: devices recreated | ✓ the window goes on drawing on the CPU |
| Alt shortcuts | `WM_SYSKEYDOWN` through its accelerator path | ✓ `WM_SYSKEYDOWN` reaches the view; Alt+F4 and Alt+Space stay Windows'; Alt+letter does not beep |
| Minimum size | `WM_GETMINMAXINFO` | ✓ 160×120 points |
| Minimized | `WM_SIZE` with `SIZE_MINIMIZED` skipped | ✓ skipped (the swapchain keeps its size) |
| Live resize | a timer in the size-move loop keeps frames coming | WM_PAINT draws each step; animations pause while dragging |
| DPI | per-monitor v2, `WM_DPICHANGED` | ✓ the same |
| IME | `WM_IME_COMPOSITION`, candidate window placed | ✓ `WM_IME_COMPOSITION`: what is composed shows in the input; the candidate window follows the caret |
| Mouse | all buttons, horizontal wheel, click counts | ✓ the same: left, right, middle, X buttons, `WM_MOUSEHWHEEL`, double and triple clicks |

## macOS

| | gpui-fast | MyZig |
| --- | --- | --- |
| Bindings | objc / objc2 crates, linked frameworks | the Objective-C runtime from Zig, frameworks opened at run time (no SDK) |
| Renderer | Metal | ✓ Metal; Vulkan through MoltenVK on request |
| Layer | opaque, autoresizing, no drawable timeout | ✓ opaque and autoresizing; drawables keep their one-second timeout, and a missing one skips the frame |
| Hidden windows | occlusion stops the display link | ✓ `windowDidChangeOcclusionState` stops drawing |
| GPU lost | not handled | ✓ the window goes on drawing on the CPU |
| Key equivalents | `performKeyEquivalent` and `keyDown` both, deduplicated | `keyDown` (⌘ shortcuts that no menu takes reach it) |
| Text input | full `NSTextInputClient`, marked text, IME-first for composing sources | ✓ marked text shows in the input, keys go to the method while it composes, `firstRectForCharacterRange` gives the caret |
| Scrolling | precise (trackpad) and line deltas | ✓ the same, both ways, 20 points a line |
| Mouse | every button, click counts | ✓ left, right, middle, back and forward; AppKit's click count |
| Ctrl-click | a right click | ✓ a right click |
| Live resize | `displayLayer` with transactions | the frame is drawn from `windowDidResize` |

## What CI checks

`run_tests` builds and tests on each OS and draws the gallery's first
frame from every renderer the runner has: CPU, OpenGL (llvmpipe) and
Vulkan (lavapipe) on Linux; the CPU on Windows; Metal, the CPU and Vulkan
(MoltenVK) on macOS. The Vulkan and Metal tests compare their pixels with
the CPU renderer's. On Linux, `script/input-test` drives the `events`
example in a real window with xdotool and xclip: every mouse button, the
wheel both ways, a double click, typed UTF-8, and the clipboard both ways
with a 4.5 MB paste that goes in pieces. The same script passes against the
Windows build under Wine (`script/input-test wine zig-out/bin/events.exe`).
`script/wayland-test` runs it again against a native Wayland window, in
Weston nested in Xvfb (copying and pasting through wl-clipboard), after
drawing the gallery's first frame there with the CPU, OpenGL (EGL) and
Vulkan.
