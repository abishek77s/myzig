# Platform layers: MyZig and gpui-fast

What each OS layer of MyZig does next to gpui-fast's (`crates/gpui_linux`,
`gpui_windows`, `gpui_macos`, `gpui_apple`, `gpui_wgpu` at the commit
`bench/bench.py setup` pins), read for the places a window can fail: a
crash, a hang, input that goes missing, a frame that never shows. ✓ means
MyZig handles it; the notes say how, or what is still missing.

## Linux

| | gpui-fast | MyZig |
| --- | --- | --- |
| Display servers | X11 (x11rb) and native Wayland | X11 (Xlib); Wayland through XWayland |
| Protocol errors | x11rb returns them as values; logged | ✓ an error handler logs them (Xlib's default exits the process) |
| CPU presentation | none: it always draws with the GPU (wgpu) | ✓ MIT-SHM, and plain XPutImage when the server cannot attach the segment (SSH, remote X) |
| Hidden windows | Unmap/Visibility stop drawing | ✓ UnmapNotify stops drawing until the next Expose |
| GPU lost | wgpu device recreated | ✓ the window goes on drawing on the CPU |
| Keyboard | XKB through xkbcommon, layout changes | core keysyms; Ctrl shortcuts on non-Latin layouts may not match |
| Text input | XIM with pre-edit | XIM, committed text only |
| Scrolling | XInput 2 smooth deltas, horizontal | wheel notches (buttons 4 and 5) only |
| Clipboard | CLIPBOARD and PRIMARY, INCR for large data | CLIPBOARD; pastes larger than one property (INCR) come back empty |

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
| IME | `WM_IME_COMPOSITION`, candidate window placed | committed text through `WM_CHAR`; the candidate window is not placed |
| Mouse | all buttons, horizontal wheel, click counts | left button and vertical wheel |

## macOS

| | gpui-fast | MyZig |
| --- | --- | --- |
| Bindings | objc / objc2 crates, linked frameworks | the Objective-C runtime from Zig, frameworks opened at run time (no SDK) |
| Renderer | Metal | ✓ Metal; Vulkan through MoltenVK on request |
| Layer | opaque, autoresizing, no drawable timeout | ✓ opaque and autoresizing; drawables keep their one-second timeout, and a missing one skips the frame |
| Hidden windows | occlusion stops the display link | ✓ `windowDidChangeOcclusionState` stops drawing |
| GPU lost | not handled | ✓ the window goes on drawing on the CPU |
| Key equivalents | `performKeyEquivalent` and `keyDown` both, deduplicated | `keyDown` (⌘ shortcuts that no menu takes reach it) |
| Text input | full `NSTextInputClient`, marked text, IME-first for composing sources | `insertText` only; marked text is not shown |
| Scrolling | precise (trackpad) and line deltas | ✓ the same, 20 points a line |
| Ctrl-click | a right click | not handled (no right button yet) |
| Live resize | `displayLayer` with transactions | the frame is drawn from `windowDidResize` |

## What CI checks

`run_tests` builds and tests on each OS and draws the gallery's first
frame from every renderer the runner has: CPU, OpenGL (llvmpipe) and
Vulkan (lavapipe) on Linux; the CPU on Windows; Metal, the CPU and Vulkan
(MoltenVK) on macOS. The Vulkan and Metal tests compare their pixels with
the CPU renderer's.
