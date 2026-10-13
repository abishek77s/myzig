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
    selection_request: SelectionRequestEvent,
    selection: SelectionEvent,
    property: PropertyEvent,
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

pub const SelectionRequestEvent = extern struct { type: c_int, serial: c_ulong, send_event: c_int, display: ?*Display, owner: Window, requestor: Window, selection: Atom, target: Atom, property: Atom, time: c_ulong };
pub const PropertyEvent = extern struct { type: c_int, serial: c_ulong, send_event: c_int, display: ?*Display, window: Window, atom: Atom, time: c_ulong, state: c_int };
pub const SelectionEvent = extern struct { type: c_int, serial: c_ulong, send_event: c_int, display: ?*Display, requestor: Window, selection: Atom, target: Atom, property: Atom, time: c_ulong };

pub const KeyPress = 2;
pub const SelectionClear = 29;
pub const SelectionRequest = 30;
pub const SelectionNotify = 31;
pub const FocusIn = 9;
pub const FocusOut = 10;
pub const FocusChangeMask = 1 << 21;
pub const ShiftMask = 1 << 0;
pub const LockMask = 1 << 1;
pub const ControlMask = 1 << 2;
pub const Mod1Mask = 1 << 3; // Alt
pub const Mod4Mask = 1 << 6; // Super
pub const PropModeReplace = 0;
pub const XA_ATOM: Atom = 4;
pub const XA_STRING: Atom = 31;
pub const AnyPropertyType: Atom = 0;
pub const XC_left_ptr = 68;
pub const XC_xterm = 152;
pub const XC_hand2 = 60;
pub const XIMPreeditNothing = 0x0008;
pub const XIMStatusNothing = 0x0400;
pub const XBufferOverflow = -1;
pub const ButtonPress = 4;
pub const ButtonRelease = 5;
pub const MotionNotify = 6;
pub const EnterNotify = 7;
pub const LeaveNotify = 8;
pub const PropertyNotify = 28;
pub const PropertyNewValue = 0;
pub const PropertyDelete = 1;
pub const Expose = 12;
pub const GraphicsExpose = 13;
pub const ConfigureNotify = 22;
pub const UnmapNotify = 18;
pub const MapNotify = 19;
pub const ClientMessage = 33;

pub const KeyPressMask = 1 << 0;
pub const ButtonPressMask = 1 << 2;
pub const ButtonReleaseMask = 1 << 3;
pub const EnterWindowMask = 1 << 4;
pub const LeaveWindowMask = 1 << 5;
pub const PropertyChangeMask = 1 << 22;
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
pub const XErrorEvent = extern struct {
    type: c_int,
    display: ?*Display,
    resourceid: c_ulong,
    serial: c_ulong,
    error_code: u8,
    request_code: u8,
    minor_code: u8,
};
pub const ErrorHandler = *const fn (*Display, *XErrorEvent) callconv(.c) c_int;
pub extern "X11" fn XSetErrorHandler(h: ?ErrorHandler) ?ErrorHandler;
pub extern "X11" fn XConnectionNumber(d: *Display) c_int;
pub extern "X11" fn XCreateGC(d: *Display, drawable: Window, mask: c_ulong, values: ?*anyopaque) GC;
pub extern "X11" fn XCreateImage(d: *Display, v: *Visual, depth: c_uint, format: c_int, offset: c_int, data: ?[*]u8, w: c_uint, h: c_uint, pad: c_int, bpl: c_int) ?*XImage;
pub extern "X11" fn XPutImage(d: *Display, drawable: Window, gc: GC, img: *XImage, sx: c_int, sy: c_int, dx: c_int, dy: c_int, w: c_uint, h: c_uint) c_int;
pub extern "X11" fn XCopyArea(d: *Display, src: Window, dst: Window, gc: GC, sx: c_int, sy: c_int, w: c_uint, h: c_uint, dx: c_int, dy: c_int) c_int;
pub extern "X11" fn XFree(p: ?*anyopaque) c_int;
pub extern "X11" fn XLookupKeysym(ev: *InputEvent, index: c_int) c_ulong;

pub extern "X11" fn XLookupString(ev: *InputEvent, buf: [*]u8, len: c_int, sym: ?*c_ulong, status: ?*anyopaque) c_int;
pub extern "X11" fn Xutf8LookupString(ic: *anyopaque, ev: *InputEvent, buf: [*]u8, len: c_int, sym: ?*c_ulong, status: ?*c_int) c_int;
pub extern "X11" fn XSetLocaleModifiers(mods: [*:0]const u8) ?[*:0]u8;
pub extern "X11" fn XOpenIM(d: *Display, db: ?*anyopaque, name: ?[*:0]const u8, class: ?[*:0]const u8) ?*anyopaque;
pub extern "X11" fn XCloseIM(im: *anyopaque) c_int;
pub extern "X11" fn XCreateIC(im: *anyopaque, ...) ?*anyopaque;
pub extern "X11" fn XDestroyIC(ic: *anyopaque) void;
pub extern "X11" fn XSetICFocus(ic: *anyopaque) void;
pub extern "X11" fn XUnsetICFocus(ic: *anyopaque) void;
pub extern "X11" fn XFilterEvent(ev: *XEvent, w: Window) c_int;
pub extern "X11" fn XSetSelectionOwner(d: *Display, selection: Atom, owner: Window, time: c_ulong) c_int;
pub extern "X11" fn XGetSelectionOwner(d: *Display, selection: Atom) Window;
pub extern "X11" fn XConvertSelection(d: *Display, selection: Atom, target: Atom, property: Atom, requestor: Window, time: c_ulong) c_int;
pub extern "X11" fn XChangeProperty(d: *Display, w: Window, property: Atom, type: Atom, format: c_int, mode: c_int, data: [*]const u8, n: c_int) c_int;
pub extern "X11" fn XGetWindowProperty(d: *Display, w: Window, property: Atom, offset: c_long, length: c_long, delete: c_int, req_type: Atom, actual_type: *Atom, actual_format: *c_int, nitems: *c_ulong, bytes_after: *c_ulong, prop: *?[*]u8) c_int;
pub extern "X11" fn XMaxRequestSize(d: *Display) c_long;
pub extern "X11" fn XExtendedMaxRequestSize(d: *Display) c_long;
pub extern "X11" fn XDeleteProperty(d: *Display, w: Window, property: Atom) c_int;
pub extern "X11" fn XSendEvent(d: *Display, w: Window, propagate: c_int, mask: c_long, ev: *XEvent) c_int;
pub extern "X11" fn XCheckTypedWindowEvent(d: *Display, w: Window, type: c_int, ev: *XEvent) c_int;
pub extern "X11" fn XCreateFontCursor(d: *Display, shape: c_uint) c_ulong;
pub extern "X11" fn XDefineCursor(d: *Display, w: Window, cursor: c_ulong) c_int;
pub extern "c" fn setlocale(category: c_int, locale: ?[*:0]const u8) ?[*:0]u8;

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

pub const XVisualInfo = extern struct {
    visual: *Visual,
    visualid: c_ulong,
    screen: c_int,
    depth: c_int,
    class: c_int,
    red_mask: c_ulong,
    green_mask: c_ulong,
    blue_mask: c_ulong,
    colormap_size: c_int,
    bits_per_rgb: c_int,
};

pub const XSetWindowAttributes = extern struct {
    background_pixmap: c_ulong = 0,
    background_pixel: c_ulong = 0,
    border_pixmap: c_ulong = 0,
    border_pixel: c_ulong = 0,
    bit_gravity: c_int = 0,
    win_gravity: c_int = 0,
    backing_store: c_int = 0,
    backing_planes: c_ulong = 0,
    backing_pixel: c_ulong = 0,
    save_under: c_int = 0,
    event_mask: c_long = 0,
    do_not_propagate_mask: c_long = 0,
    override_redirect: c_int = 0,
    colormap: c_ulong = 0,
    cursor: c_ulong = 0,
};

pub const CWBackPixel = 1 << 1;
pub const CWBorderPixel = 1 << 3;
pub const CWEventMask = 1 << 11;
pub const CWColormap = 1 << 13;
pub const InputOutput = 1;
pub const AllocNone = 0;

pub extern "X11" fn XCreateWindow(d: *Display, parent: Window, x: c_int, y: c_int, w: c_uint, h: c_uint, bw: c_uint, depth: c_int, class: c_uint, visual: *Visual, mask: c_ulong, attrs: *XSetWindowAttributes) Window;
pub extern "X11" fn XCreateColormap(d: *Display, w: Window, visual: *Visual, alloc: c_int) c_ulong;
