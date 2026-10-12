//! The Linux backend: a window of the X server through Xlib, its pixels in
//! shared memory (MIT-SHM), presented one damaged rectangle at a time.
//!
//! Xlib is declared by hand rather than translated from its headers, which
//! keeps the C translation out of every build.

const std = @import("std");

pub const Display = opaque {};
pub const Visual = opaque {};
pub const GC = *opaque {};
pub const Window = c_ulong;
pub const Atom = c_ulong;

pub const XImage = extern struct {
    width: c_int,
    height: c_int,
    xoffset: c_int,
    format: c_int,
    data: ?[*]u8,
    byte_order: c_int,
    bitmap_unit: c_int,
    bitmap_bit_order: c_int,
    bitmap_pad: c_int,
    depth: c_int,
    bytes_per_line: c_int,
    // the rest is Xlib's.
};

pub const ShmSegmentInfo = extern struct {
    shmseg: c_ulong = 0,
    shmid: c_int = -1,
    shmaddr: ?[*]u8 = null,
    read_only: c_int = 0,
};

pub const XEvent = extern union {
    type: c_int,
    any: AnyEvent,
    key: InputEvent,
    button: InputEvent,
    motion: InputEvent,
    configure: ConfigureEvent,
    expose: ExposeEvent,
    client: ClientMessageEvent,
    pad: [24]c_long,
};

pub const AnyEvent = extern struct { type: c_int, serial: c_ulong, send_event: c_int, display: ?*Display, window: Window };
pub const InputEvent = extern struct {
    type: c_int,
    serial: c_ulong,
    send_event: c_int,
    display: ?*Display,
    window: Window,
    root: Window,
    subwindow: Window,
    time: c_ulong,
    x: c_int,
    y: c_int,
    x_root: c_int,
    y_root: c_int,
    state: c_uint,
    detail: c_uint, // keycode or button
    same_screen: c_int,
};
pub const ConfigureEvent = extern struct {
    type: c_int,
    serial: c_ulong,
    send_event: c_int,
    display: ?*Display,
    event: Window,
    window: Window,
    x: c_int,
    y: c_int,
    width: c_int,
    height: c_int,
    border_width: c_int,
    above: Window,
    override_redirect: c_int,
};
pub const ExposeEvent = extern struct { type: c_int, serial: c_ulong, send_event: c_int, display: ?*Display, window: Window, x: c_int, y: c_int, width: c_int, height: c_int, count: c_int };
pub const ClientMessageEvent = extern struct { type: c_int, serial: c_ulong, send_event: c_int, display: ?*Display, window: Window, message_type: Atom, format: c_int, data: [5]c_long };

pub const KeyPress = 2;
pub const ButtonPress = 4;
pub const ButtonRelease = 5;
pub const MotionNotify = 6;
pub const LeaveNotify = 8;
pub const Expose = 12;
pub const GraphicsExpose = 13;
pub const ConfigureNotify = 22;
pub const ClientMessage = 33;

pub const KeyPressMask = 1 << 0;
pub const ButtonPressMask = 1 << 2;
pub const ButtonReleaseMask = 1 << 3;
pub const LeaveWindowMask = 1 << 5;
pub const PointerMotionMask = 1 << 6;
pub const ExposureMask = 1 << 15;
pub const StructureNotifyMask = 1 << 17;
pub const ZPixmap = 2;

pub extern "X11" fn XOpenDisplay(name: ?[*:0]const u8) ?*Display;
pub extern "X11" fn XCloseDisplay(d: *Display) c_int;
pub extern "X11" fn XDefaultScreen(d: *Display) c_int;
pub extern "X11" fn XRootWindow(d: *Display, screen: c_int) Window;
pub extern "X11" fn XDefaultVisual(d: *Display, screen: c_int) *Visual;
pub extern "X11" fn XDefaultDepth(d: *Display, screen: c_int) c_int;
pub extern "X11" fn XCreateSimpleWindow(d: *Display, parent: Window, x: c_int, y: c_int, w: c_uint, h: c_uint, bw: c_uint, border: c_ulong, bg: c_ulong) Window;
pub extern "X11" fn XSelectInput(d: *Display, w: Window, mask: c_long) c_int;
pub extern "X11" fn XStoreName(d: *Display, w: Window, name: [*:0]const u8) c_int;
pub extern "X11" fn XInternAtom(d: *Display, name: [*:0]const u8, only_if_exists: c_int) Atom;
pub extern "X11" fn XSetWMProtocols(d: *Display, w: Window, protocols: [*]Atom, count: c_int) c_int;
pub extern "X11" fn XMapWindow(d: *Display, w: Window) c_int;
pub extern "X11" fn XPending(d: *Display) c_int;
pub extern "X11" fn XNextEvent(d: *Display, ev: *XEvent) c_int;
pub extern "X11" fn XFlush(d: *Display) c_int;
pub extern "X11" fn XSync(d: *Display, discard: c_int) c_int;
pub extern "X11" fn XConnectionNumber(d: *Display) c_int;
pub extern "X11" fn XCreateGC(d: *Display, drawable: Window, mask: c_ulong, values: ?*anyopaque) GC;
pub extern "X11" fn XCreateImage(d: *Display, v: *Visual, depth: c_uint, format: c_int, offset: c_int, data: ?[*]u8, w: c_uint, h: c_uint, pad: c_int, bpl: c_int) ?*XImage;
pub extern "X11" fn XPutImage(d: *Display, drawable: Window, gc: GC, img: *XImage, sx: c_int, sy: c_int, dx: c_int, dy: c_int, w: c_uint, h: c_uint) c_int;
pub extern "X11" fn XCopyArea(d: *Display, src: Window, dst: Window, gc: GC, sx: c_int, sy: c_int, w: c_uint, h: c_uint, dx: c_int, dy: c_int) c_int;
pub extern "X11" fn XFree(p: ?*anyopaque) c_int;
pub extern "X11" fn XLookupKeysym(ev: *InputEvent, index: c_int) c_ulong;

pub extern "Xext" fn XShmQueryExtension(d: *Display) c_int;
pub extern "Xext" fn XShmGetEventBase(d: *Display) c_int;
pub extern "Xext" fn XShmCreateImage(d: *Display, v: *Visual, depth: c_uint, format: c_int, data: ?[*]u8, info: *ShmSegmentInfo, w: c_uint, h: c_uint) ?*XImage;
pub extern "Xext" fn XShmAttach(d: *Display, info: *ShmSegmentInfo) c_int;
pub extern "Xext" fn XShmDetach(d: *Display, info: *ShmSegmentInfo) c_int;
pub extern "Xext" fn XShmPutImage(d: *Display, drawable: Window, gc: GC, img: *XImage, sx: c_int, sy: c_int, dx: c_int, dy: c_int, w: c_uint, h: c_uint, send_event: c_int) c_int;

pub extern "c" fn shmget(key: c_int, size: usize, flags: c_int) c_int;
pub extern "c" fn shmat(id: c_int, addr: ?*anyopaque, flags: c_int) ?*anyopaque;
pub extern "c" fn shmdt(addr: ?*anyopaque) c_int;
pub extern "c" fn shmctl(id: c_int, cmd: c_int, buf: ?*anyopaque) c_int;
pub const IPC_PRIVATE = 0;
pub const IPC_CREAT = 0o1000;
pub const IPC_RMID = 0;
