//! The Direct3D 11 renderer, Windows' GPU API and the one gpui draws with
//! there: the hardware's device, else WARP (Direct3D's own rasterizer on
//! the CPU), the whole scene in one instanced draw of instances.zig's
//! shader, compiled from HLSL at run time by d3dcompiler_47 (part of
//! Windows since 10). d3d11.dll is loaded at run time, so an app that
//! draws otherwise never maps it; COM is called through its vtables.

const std = @import("std");
const sys = @import("../sys.zig");
const scene = @import("../scene.zig");
const text = @import("../text.zig");
const inst = @import("instances.zig");

const HRESULT = i32;
const BOOL = i32;
const HWND = *anyopaque;
const CC: std.builtin.CallingConvention = .winapi;

/// Com is an interface pointer: its first word is its vtable.
const Com = opaque {
    fn fnAt(c: *Com, comptime T: type, i: usize) T {
        const vtbl: *const [*]const ?*const anyopaque = @ptrCast(@alignCast(c));
        return @ptrCast(vtbl.*[i].?);
    }

    fn release(c: *Com) void {
        _ = c.fnAt(*const fn (*Com) callconv(CC) u32, 2)(c);
    }
};

fn release(c: anytype) void {
    if (c.*) |p| p.release();
    c.* = null;
}

const Guid = extern struct { a: u32, b: u16, c: u16, d: [8]u8 };
const IID_ID3D11Texture2D = Guid{ .a = 0x6f15aaf2, .b = 0xd208, .c = 0x4e89, .d = .{ 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };
const IID_IDXGIDevice = Guid{ .a = 0x54ec77fa, .b = 0x1377, .c = 0x44e6, .d = .{ 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c } };

const DXGI_FORMAT_UNKNOWN = 0;
const DXGI_FORMAT_R32G32B32A32_FLOAT = 2;
const DXGI_FORMAT_B8G8R8A8_UNORM = 87;
const DXGI_FORMAT_R8_UNORM = 61;

const SwapChainDesc = extern struct {
    width: u32,
    height: u32,
    refresh_num: u32 = 0,
    refresh_den: u32 = 0,
    format: u32 = DXGI_FORMAT_B8G8R8A8_UNORM,
    scanline_ordering: u32 = 0,
    scaling: u32 = 0,
    sample_count: u32 = 1,
    sample_quality: u32 = 0,
    buffer_usage: u32 = 0x20, // DXGI_USAGE_RENDER_TARGET_OUTPUT
    buffer_count: u32,
    window: HWND,
    windowed: BOOL = 1,
    swap_effect: u32,
    flags: u32 = 0,
};

const BufferDesc = extern struct { byte_width: u32, usage: u32, bind: u32, cpu_access: u32 = 0, misc: u32 = 0, stride: u32 = 0 };
const Texture2DDesc = extern struct {
    width: u32,
    height: u32,
    mip_levels: u32 = 1,
    array_size: u32 = 1,
    format: u32,
    sample_count: u32 = 1,
    sample_quality: u32 = 0,
    usage: u32 = 0,
    bind: u32 = 0,
    cpu_access: u32 = 0,
    misc: u32 = 0,
};
const InputElement = extern struct { semantic: [*:0]const u8, index: u32, format: u32, slot: u32, offset: u32, class: u32, step: u32 };
const SamplerDesc = extern struct {
    filter: u32,
    address_u: u32 = 3, // clamp
    address_v: u32 = 3,
    address_w: u32 = 3,
    mip_lod_bias: f32 = 0,
    max_anisotropy: u32 = 1,
    comparison: u32 = 1, // never
    border: [4]f32 = .{ 0, 0, 0, 0 },
    min_lod: f32 = 0,
    max_lod: f32 = std.math.floatMax(f32),
};
const TargetBlend = extern struct {
    enable: BOOL,
    src: u32,
    dst: u32,
    op: u32,
    src_alpha: u32,
    dst_alpha: u32,
    op_alpha: u32,
    write_mask: u8,
};
const BlendDesc = extern struct { alpha_to_coverage: BOOL = 0, independent: BOOL = 0, targets: [8]TargetBlend };
const RasterizerDesc = extern struct {
    fill: u32 = 3, // solid
    cull: u32 = 1, // none
    front_ccw: BOOL = 0,
    depth_bias: i32 = 0,
    depth_bias_clamp: f32 = 0,
    slope_scaled_depth_bias: f32 = 0,
    depth_clip: BOOL = 1,
    scissor: BOOL = 0,
    multisample: BOOL = 0,
    antialiased_line: BOOL = 0,
};
const Viewport = extern struct { x: f32 = 0, y: f32 = 0, w: f32, h: f32, min_depth: f32 = 0, max_depth: f32 = 1 };
const Box = extern struct { left: u32, top: u32, front: u32 = 0, right: u32, bottom: u32, back: u32 = 1 };
const Mapped = extern struct { data: ?[*]u8, row_pitch: u32, depth_pitch: u32 };
const AdapterDesc = extern struct {
    description: [128]u16,
    vendor: u32,
    device: u32,
    subsys: u32,
    revision: u32,
    dedicated_video: usize,
    dedicated_system: usize,
    shared_system: usize,
    luid: [2]u32,
};

const USAGE_DEFAULT = 0;
const USAGE_DYNAMIC = 2;
const USAGE_STAGING = 3;
const BIND_VERTEX_BUFFER = 0x1;
const BIND_CONSTANT_BUFFER = 0x4;
const BIND_SHADER_RESOURCE = 0x8;
const CPU_ACCESS_WRITE = 0x10000;
const CPU_ACCESS_READ = 0x20000;
const MAP_READ = 1;
const MAP_WRITE_DISCARD = 4;

// Vtable slots of the methods called, after IUnknown's three.
const dev_create_buffer = 3;
const dev_create_texture2d = 5;
const dev_create_srv = 7;
const dev_create_rtv = 9;
const dev_create_input_layout = 11;
const dev_create_vertex_shader = 12;
const dev_create_pixel_shader = 15;
const dev_create_blend_state = 20;
const dev_create_rasterizer_state = 22;
const dev_create_sampler_state = 23;
const ctx_vs_set_constant_buffers = 7;
const ctx_ps_set_shader_resources = 8;
const ctx_ps_set_shader = 9;
const ctx_ps_set_samplers = 10;
const ctx_vs_set_shader = 11;
const ctx_map = 14;
const ctx_unmap = 15;
const ctx_ia_set_input_layout = 17;
const ctx_ia_set_vertex_buffers = 18;
const ctx_draw_instanced = 21;
const ctx_ia_set_primitive_topology = 24;
const ctx_om_set_render_targets = 33;
const ctx_om_set_blend_state = 35;
const ctx_rs_set_state = 43;
const ctx_rs_set_viewports = 44;
const ctx_copy_resource = 47;
const ctx_update_subresource = 48;
const ctx_clear_rtv = 50;
const ctx_clear_state = 110;
const sc_present = 8;
const sc_get_buffer = 9;
const sc_resize_buffers = 13;
const blob_pointer = 3;
const blob_size = 4;

const CreateDeviceAndSwapChain = *const fn (?*Com, u32, ?*anyopaque, u32, ?[*]const u32, u32, u32, *const SwapChainDesc, *?*Com, *?*Com, ?*u32, *?*Com) callconv(CC) HRESULT;
const Compile = *const fn ([*]const u8, usize, ?[*:0]const u8, ?*const anyopaque, ?*anyopaque, [*:0]const u8, [*:0]const u8, u32, u32, *?*Com, ?*?*Com) callconv(CC) HRESULT;

fn ok(hr: HRESULT) !void {
    if (hr < 0) return error.Direct3D;
}

pub const Renderer = struct {
    gpa: std.mem.Allocator,
    lib: sys.DynLib,
    device: ?*Com = null,
    ctx: ?*Com = null,
    swap: ?*Com = null,
    rtv: ?*Com = null,
    vs: ?*Com = null,
    ps: ?*Com = null,
    layout: ?*Com = null,
    blend: ?*Com = null,
    raster: ?*Com = null,
    samplers: [2]?*Com = .{ null, null },
    viewport: ?*Com = null,
    vbo: ?*Com = null,
    vbo_size: usize = 0,
    atlas_tex: ?*Com = null,
    atlas_srv: ?*Com = null,
    tex_size: u32 = 0,
    images_tex: ?*Com = null,
    images_srv: ?*Com = null,
    images_size: u32 = 0,
    /// staging holds the last frame drawn for read, when capture is set.
    staging: ?*Com = null,
    capture: bool = false,
    vsync: bool,
    width: u32,
    height: u32,
    atlas: inst.Atlas,
    images: inst.ImageAtlas,
    instances: std.ArrayListUnmanaged(inst.Instance) = .empty,
    name_buf: [128]u8 = undefined,
    name_len: usize = 0,
    warp: bool = false,

    pub const Options = struct { vsync: bool = true, warp: bool = false };

    /// init makes a device and a swap chain for hwnd, width×height pixels:
    /// on the hardware, or on WARP with opts.warp.
    pub fn init(gpa: std.mem.Allocator, hwnd: HWND, width: i32, height: i32, opts: Options) !Renderer {
        var lib = try sys.DynLib.open("d3d11.dll");
        errdefer lib.close();
        const create = lib.lookup(CreateDeviceAndSwapChain, "D3D11CreateDeviceAndSwapChain") orelse return error.NoDirect3D;
        var r = Renderer{
            .gpa = gpa,
            .lib = lib,
            .vsync = opts.vsync,
            .width = @intCast(@max(1, width)),
            .height = @intCast(@max(1, height)),
            .atlas = .{ .gpa = gpa },
            .images = .{ .gpa = gpa },
            .warp = opts.warp,
        };
        errdefer r.releaseAll();
        const levels = [_]u32{ 0xb000, 0xa100, 0xa000 }; // 11_0, 10_1, 10_0
        const driver: u32 = if (opts.warp) 5 else 1; // WARP, hardware
        // A flip-model swap chain (Windows 10), else the older blit model.
        for ([_]u32{ 4, 0 }) |effect| { // FLIP_DISCARD, DISCARD
            const desc = SwapChainDesc{ .width = r.width, .height = r.height, .buffer_count = if (effect == 4) 2 else 1, .window = hwnd, .swap_effect = effect };
            const hr = create(null, driver, null, 0x20, &levels, levels.len, 7, &desc, &r.swap, &r.device, null, &r.ctx); // BGRA support, SDK 7
            if (hr >= 0) break;
        } else return error.NoDevice;
        try r.adapterName();
        try r.pipeline();
        try r.target();
        return r;
    }

    fn adapterName(r: *Renderer) !void {
        const dev = r.device.?;
        var dxgi: ?*Com = null;
        try ok(dev.fnAt(*const fn (*Com, *const Guid, *?*Com) callconv(CC) HRESULT, 0)(dev, &IID_IDXGIDevice, &dxgi));
        defer release(&dxgi);
        var adapter: ?*Com = null;
        try ok(dxgi.?.fnAt(*const fn (*Com, *?*Com) callconv(CC) HRESULT, 7)(dxgi.?, &adapter));
        defer release(&adapter);
        var desc: AdapterDesc = undefined;
        try ok(adapter.?.fnAt(*const fn (*Com, *AdapterDesc) callconv(CC) HRESULT, 8)(adapter.?, &desc));
        const wide = std.mem.sliceTo(&desc.description, 0);
        var n: usize = 0;
        var it = std.unicode.Utf16LeIterator.init(wide);
        while (it.nextCodepoint() catch null) |cp| {
            var b: [4]u8 = undefined;
            const l = std.unicode.utf8Encode(cp, &b) catch break;
            if (n + l > r.name_buf.len) break;
            @memcpy(r.name_buf[n..][0..l], b[0..l]);
            n += l;
        }
        r.name_len = n;
        // Microsoft's Basic Render Driver is WARP, whichever was asked for.
        if (desc.vendor == 0x1414 and desc.device == 0x8c) r.warp = true;
    }

    /// compile compiles entry of hlsl for profile. The blob it returns is
    /// the compiler's: lib stays loaded while it is used.
    fn compile(lib: *sys.DynLib, entry: [*:0]const u8, profile: [*:0]const u8) !*Com {
        const d3dCompile = lib.lookup(Compile, "D3DCompile") orelse return error.NoCompiler;
        var code: ?*Com = null;
        var errors: ?*Com = null;
        defer release(&errors);
        const hr = d3dCompile(hlsl.ptr, hlsl.len, "myzig.hlsl", null, null, entry, profile, 1 << 15, 0, &code, &errors); // optimization level 3
        if (hr < 0) {
            if (errors) |e| {
                const p = e.fnAt(*const fn (*Com) callconv(CC) [*]const u8, blob_pointer)(e);
                const n = e.fnAt(*const fn (*Com) callconv(CC) usize, blob_size)(e);
                sys.log(.err, "myzig: HLSL: {s}", .{p[0..n]});
            }
            return error.ShaderCompile;
        }
        return code.?;
    }

    fn blob(b: *Com) []const u8 {
        const p = b.fnAt(*const fn (*Com) callconv(CC) [*]const u8, blob_pointer)(b);
        const n = b.fnAt(*const fn (*Com) callconv(CC) usize, blob_size)(b);
        return p[0..n];
    }

    fn pipeline(r: *Renderer) !void {
        const dev = r.device.?;
        var compiler = try sys.DynLib.open("d3dcompiler_47.dll");
        defer compiler.close();
        var vs_code: ?*Com = try compile(&compiler, "vs", "vs_4_0");
        defer release(&vs_code);
        var ps_code: ?*Com = try compile(&compiler, "ps", "ps_4_0");
        defer release(&ps_code);
        const vsb = blob(vs_code.?);
        const psb = blob(ps_code.?);
        const CreateShader = *const fn (*Com, [*]const u8, usize, ?*anyopaque, *?*Com) callconv(CC) HRESULT;
        try ok(dev.fnAt(CreateShader, dev_create_vertex_shader)(dev, vsb.ptr, vsb.len, null, &r.vs));
        try ok(dev.fnAt(CreateShader, dev_create_pixel_shader)(dev, psb.ptr, psb.len, null, &r.ps));
        const names = [_][*:0]const u8{ "RECT", "CLIP", "COLOR", "BORDER", "PARAMS", "UV" };
        var elems: [6]InputElement = undefined;
        for (&elems, names, 0..) |*e, n, i| e.* = .{ .semantic = n, .index = 0, .format = DXGI_FORMAT_R32G32B32A32_FLOAT, .slot = 0, .offset = @intCast(i * 16), .class = 1, .step = 1 };
        try ok(dev.fnAt(*const fn (*Com, [*]const InputElement, u32, [*]const u8, usize, *?*Com) callconv(CC) HRESULT, dev_create_input_layout)(dev, &elems, elems.len, vsb.ptr, vsb.len, &r.layout));
        var blend = BlendDesc{ .targets = undefined };
        for (&blend.targets) |*t| t.* = .{ .enable = 0, .src = 2, .dst = 1, .op = 1, .src_alpha = 2, .dst_alpha = 1, .op_alpha = 1, .write_mask = 0xf };
        // Premultiplied alpha: one, one minus the source's alpha.
        blend.targets[0] = .{ .enable = 1, .src = 2, .dst = 6, .op = 1, .src_alpha = 2, .dst_alpha = 6, .op_alpha = 1, .write_mask = 0xf };
        try ok(dev.fnAt(*const fn (*Com, *const BlendDesc, *?*Com) callconv(CC) HRESULT, dev_create_blend_state)(dev, &blend, &r.blend));
        try ok(dev.fnAt(*const fn (*Com, *const RasterizerDesc, *?*Com) callconv(CC) HRESULT, dev_create_rasterizer_state)(dev, &.{}, &r.raster));
        const CreateSampler = *const fn (*Com, *const SamplerDesc, *?*Com) callconv(CC) HRESULT;
        try ok(dev.fnAt(CreateSampler, dev_create_sampler_state)(dev, &.{ .filter = 0 }, &r.samplers[0])); // point, for glyphs
        try ok(dev.fnAt(CreateSampler, dev_create_sampler_state)(dev, &.{ .filter = 0x15 }, &r.samplers[1])); // linear, for images
        try ok(dev.fnAt(*const fn (*Com, *const BufferDesc, ?*const anyopaque, *?*Com) callconv(CC) HRESULT, dev_create_buffer)(dev, &.{ .byte_width = 16, .usage = USAGE_DYNAMIC, .bind = BIND_CONSTANT_BUFFER, .cpu_access = CPU_ACCESS_WRITE }, null, &r.viewport));
    }

    /// target makes the render target view of the swap chain's buffer.
    fn target(r: *Renderer) !void {
        const sc = r.swap.?;
        var buf: ?*Com = null;
        try ok(sc.fnAt(*const fn (*Com, u32, *const Guid, *?*Com) callconv(CC) HRESULT, sc_get_buffer)(sc, 0, &IID_ID3D11Texture2D, &buf));
        defer release(&buf);
        const dev = r.device.?;
        try ok(dev.fnAt(*const fn (*Com, *Com, ?*const anyopaque, *?*Com) callconv(CC) HRESULT, dev_create_rtv)(dev, buf.?, null, &r.rtv));
    }

    fn releaseAll(r: *Renderer) void {
        if (r.ctx) |c| c.fnAt(*const fn (*Com) callconv(CC) void, ctx_clear_state)(c);
        inline for (.{ "staging", "images_srv", "images_tex", "atlas_srv", "atlas_tex", "vbo", "viewport", "raster", "blend", "layout", "ps", "vs", "rtv", "swap", "ctx", "device" }) |f| release(&@field(r, f));
        for (&r.samplers) |*s| release(s);
    }

    pub fn deinit(r: *Renderer) void {
        r.releaseAll();
        r.lib.close();
        r.atlas.deinit();
        r.images.deinit();
        r.instances.deinit(r.gpa);
    }

    /// name is the adapter's, as DXGI describes it.
    pub fn name(r: *const Renderer) []const u8 {
        return r.name_buf[0..r.name_len];
    }

    /// software reports whether the device is WARP's, on the CPU.
    pub fn software(r: *const Renderer) bool {
        return r.warp;
    }

    fn texture(r: *Renderer, size: u32, format: u32, tex: *?*Com, srv: *?*Com) !void {
        release(srv);
        release(tex);
        const dev = r.device.?;
        try ok(dev.fnAt(*const fn (*Com, *const Texture2DDesc, ?*const anyopaque, *?*Com) callconv(CC) HRESULT, dev_create_texture2d)(dev, &.{ .width = size, .height = size, .format = format, .bind = BIND_SHADER_RESOURCE }, null, tex));
        try ok(dev.fnAt(*const fn (*Com, *Com, ?*const anyopaque, *?*Com) callconv(CC) HRESULT, dev_create_srv)(dev, tex.*.?, null, srv));
    }

    fn upload(r: *Renderer, tex: *Com, x: u32, y: u32, w: u32, h: u32, data: [*]const u8, pitch: u32) void {
        const c = r.ctx.?;
        const box = Box{ .left = x, .top = y, .right = x + w, .bottom = y + h };
        c.fnAt(*const fn (*Com, *Com, u32, ?*const Box, [*]const u8, u32, u32) callconv(CC) void, ctx_update_subresource)(c, tex, 0, &box, data, pitch, 0);
    }

    /// draw draws s, width×height pixels, and presents it.
    pub fn draw(r: *Renderer, s: *const scene.Scene, fonts: *text.Fonts, width: i32, height: i32) !void {
        const c = r.ctx.?;
        const dev = r.device.?;
        const w: u32 = @intCast(@max(1, width));
        const h: u32 = @intCast(@max(1, height));
        if (w != r.width or h != r.height) {
            release(&r.rtv);
            const sc = r.swap.?;
            try ok(sc.fnAt(*const fn (*Com, u32, u32, u32, u32, u32) callconv(CC) HRESULT, sc_resize_buffers)(sc, 0, w, h, DXGI_FORMAT_UNKNOWN, 0));
            r.width = w;
            r.height = h;
            try r.target();
        }
        try inst.build(s, fonts, &r.atlas, &r.images, &r.instances);
        if (r.atlas.grown or r.tex_size != r.atlas.size) {
            try r.texture(r.atlas.size, DXGI_FORMAT_R8_UNORM, &r.atlas_tex, &r.atlas_srv);
            r.tex_size = r.atlas.size;
            r.atlas.grown = false;
        }
        for (r.atlas.uploads.items) |u| {
            const px = fonts.coverage.items[u.off..][0 .. u.w * u.h];
            if (u.w > 0 and u.h > 0) r.upload(r.atlas_tex.?, u.x, u.y, u.w, u.h, px.ptr, u.w);
        }
        r.atlas.uploads.clearRetainingCapacity();
        // Images: one texel until one is drawn.
        const images_size = if (r.images.used) r.images.size else 1;
        if (r.images_size != images_size or (r.images.grown and r.images.used)) {
            try r.texture(images_size, DXGI_FORMAT_B8G8R8A8_UNORM, &r.images_tex, &r.images_srv);
            r.images_size = images_size;
            r.images.grown = false;
        }
        for (r.images.uploads.items) |u| {
            r.upload(r.images_tex.?, u.x, u.y, u.image.width, u.image.height, @ptrCast(u.image.pixels.ptr), u.image.width * 4);
        }
        r.images.uploads.clearRetainingCapacity();

        // Instances, in a buffer grown to fit.
        const bytes = r.instances.items.len * @sizeOf(inst.Instance);
        if (bytes > r.vbo_size) {
            release(&r.vbo);
            const size = @max(bytes * 3 / 2, 64 * @sizeOf(inst.Instance));
            try ok(dev.fnAt(*const fn (*Com, *const BufferDesc, ?*const anyopaque, *?*Com) callconv(CC) HRESULT, dev_create_buffer)(dev, &.{ .byte_width = @intCast(size), .usage = USAGE_DYNAMIC, .bind = BIND_VERTEX_BUFFER, .cpu_access = CPU_ACCESS_WRITE }, null, &r.vbo));
            r.vbo_size = size;
        }
        const MapFn = *const fn (*Com, *Com, u32, u32, u32, *Mapped) callconv(CC) HRESULT;
        const UnmapFn = *const fn (*Com, *Com, u32) callconv(CC) void;
        var m: Mapped = undefined;
        if (bytes > 0) {
            try ok(c.fnAt(MapFn, ctx_map)(c, r.vbo.?, 0, MAP_WRITE_DISCARD, 0, &m));
            @memcpy(m.data.?[0..bytes], std.mem.sliceAsBytes(r.instances.items));
            c.fnAt(UnmapFn, ctx_unmap)(c, r.vbo.?, 0);
        }
        try ok(c.fnAt(MapFn, ctx_map)(c, r.viewport.?, 0, MAP_WRITE_DISCARD, 0, &m));
        @as(*align(1) [4]f32, @ptrCast(m.data.?)).* = .{ @floatFromInt(w), @floatFromInt(h), 0, 0 };
        c.fnAt(UnmapFn, ctx_unmap)(c, r.viewport.?, 0);

        // The pipeline, set each frame: the flip model unbinds the target.
        const views = [_]?*Com{r.rtv};
        c.fnAt(*const fn (*Com, u32, [*]const ?*Com, ?*Com) callconv(CC) void, ctx_om_set_render_targets)(c, 1, &views, null);
        const bg = s.background;
        const clear = [4]f32{ @as(f32, @floatFromInt(bg.r)) / 255, @as(f32, @floatFromInt(bg.g)) / 255, @as(f32, @floatFromInt(bg.b)) / 255, 1 };
        c.fnAt(*const fn (*Com, *Com, *const [4]f32) callconv(CC) void, ctx_clear_rtv)(c, r.rtv.?, &clear);
        if (bytes > 0) {
            const vp = Viewport{ .w = @floatFromInt(w), .h = @floatFromInt(h) };
            c.fnAt(*const fn (*Com, u32, [*]const Viewport) callconv(CC) void, ctx_rs_set_viewports)(c, 1, @ptrCast(&vp));
            c.fnAt(*const fn (*Com, ?*Com) callconv(CC) void, ctx_rs_set_state)(c, r.raster);
            c.fnAt(*const fn (*Com, ?*Com, ?*const [4]f32, u32) callconv(CC) void, ctx_om_set_blend_state)(c, r.blend, null, 0xffffffff);
            c.fnAt(*const fn (*Com, ?*Com) callconv(CC) void, ctx_ia_set_input_layout)(c, r.layout);
            c.fnAt(*const fn (*Com, u32) callconv(CC) void, ctx_ia_set_primitive_topology)(c, 5); // triangle strip
            const vbs = [_]?*Com{r.vbo};
            const strides = [_]u32{@sizeOf(inst.Instance)};
            const offsets = [_]u32{0};
            c.fnAt(*const fn (*Com, u32, u32, [*]const ?*Com, [*]const u32, [*]const u32) callconv(CC) void, ctx_ia_set_vertex_buffers)(c, 0, 1, &vbs, &strides, &offsets);
            const cbs = [_]?*Com{r.viewport};
            c.fnAt(*const fn (*Com, u32, u32, [*]const ?*Com) callconv(CC) void, ctx_vs_set_constant_buffers)(c, 0, 1, &cbs);
            const SetShader = *const fn (*Com, ?*Com, ?*anyopaque, u32) callconv(CC) void;
            c.fnAt(SetShader, ctx_vs_set_shader)(c, r.vs, null, 0);
            c.fnAt(SetShader, ctx_ps_set_shader)(c, r.ps, null, 0);
            const srvs = [_]?*Com{ r.atlas_srv, r.images_srv };
            c.fnAt(*const fn (*Com, u32, u32, [*]const ?*Com) callconv(CC) void, ctx_ps_set_shader_resources)(c, 0, 2, &srvs);
            c.fnAt(*const fn (*Com, u32, u32, [*]const ?*Com) callconv(CC) void, ctx_ps_set_samplers)(c, 0, 2, &r.samplers);
            c.fnAt(*const fn (*Com, u32, u32, u32, u32) callconv(CC) void, ctx_draw_instanced)(c, 4, @intCast(r.instances.items.len), 0, 0);
        }
        if (r.capture) try r.keep();
        const sc = r.swap.?;
        const hr = sc.fnAt(*const fn (*Com, u32, u32) callconv(CC) HRESULT, sc_present)(sc, if (r.vsync) 1 else 0, 0);
        // DXGI_ERROR_DEVICE_REMOVED, DXGI_ERROR_DEVICE_RESET.
        if (hr == @as(HRESULT, @bitCast(@as(u32, 0x887A0005))) or hr == @as(HRESULT, @bitCast(@as(u32, 0x887A0007)))) return error.DeviceLost;
    }

    /// keep copies the frame into a texture read can map.
    fn keep(r: *Renderer) !void {
        const dev = r.device.?;
        const c = r.ctx.?;
        release(&r.staging);
        try ok(dev.fnAt(*const fn (*Com, *const Texture2DDesc, ?*const anyopaque, *?*Com) callconv(CC) HRESULT, dev_create_texture2d)(dev, &.{ .width = r.width, .height = r.height, .format = DXGI_FORMAT_B8G8R8A8_UNORM, .usage = USAGE_STAGING, .cpu_access = CPU_ACCESS_READ }, null, &r.staging));
        const sc = r.swap.?;
        var buf: ?*Com = null;
        try ok(sc.fnAt(*const fn (*Com, u32, *const Guid, *?*Com) callconv(CC) HRESULT, sc_get_buffer)(sc, 0, &IID_ID3D11Texture2D, &buf));
        defer release(&buf);
        c.fnAt(*const fn (*Com, *Com, *Com) callconv(CC) void, ctx_copy_resource)(c, r.staging.?, buf.?);
    }

    /// read copies the frame kept (see capture) into out as 0x00RRGGBB.
    pub fn read(r: *Renderer, width: i32, height: i32, out: []u32) void {
        const c = r.ctx.?;
        const st = r.staging orelse return;
        var m: Mapped = undefined;
        if (c.fnAt(*const fn (*Com, *Com, u32, u32, u32, *Mapped) callconv(CC) HRESULT, ctx_map)(c, st, 0, MAP_READ, 0, &m) < 0) return;
        defer c.fnAt(*const fn (*Com, *Com, u32) callconv(CC) void, ctx_unmap)(c, st, 0);
        const w: usize = @intCast(width);
        for (0..@intCast(height)) |y| {
            const row: [*]align(1) const u32 = @ptrCast(m.data.? + y * m.row_pitch);
            for (out[y * w ..][0..w], row[0..w]) |*o, p| o.* = p & 0xffffff;
        }
    }
};

/// hlsl is instances.zig's shader for Direct3D.
const hlsl =
    \\cbuffer Frame : register(b0) { float2 viewport; float2 unused; };
    \\Texture2D<float> atlas : register(t0);
    \\Texture2D<float4> images : register(t1);
    \\SamplerState point_sampler : register(s0);
    \\SamplerState linear_sampler : register(s1);
    \\struct In {
    \\    float4 rect : RECT; float4 clip : CLIP; float4 color : COLOR;
    \\    float4 border : BORDER; float4 params : PARAMS; float4 uv : UV;
    \\    uint vid : SV_VertexID;
    \\};
    \\struct Out {
    \\    float4 position : SV_Position;
    \\    float2 pos : POS;
    \\    float2 uv : TEXCOORD0;
    \\    nointerpolation float4 rect : RECT;
    \\    nointerpolation float4 clip : CLIP;
    \\    nointerpolation float4 color : COLOR;
    \\    nointerpolation float4 border : BORDER;
    \\    nointerpolation float4 params : PARAMS;
    \\    nointerpolation float4 seg : SEG;
    \\};
    \\Out vs(In i) {
    \\    float2 corner = float2(float(i.vid & 1), float((i.vid >> 1) & 1));
    \\    float2 pos = i.rect.xy + corner * i.rect.zw;
    \\    Out o;
    \\    o.pos = pos;
    \\    o.uv = lerp(i.uv.xy, i.uv.zw, corner);
    \\    o.rect = i.rect; o.clip = i.clip; o.color = i.color;
    \\    o.border = i.border; o.params = i.params; o.seg = i.uv;
    \\    float2 ndc = pos / viewport * 2.0 - 1.0;
    \\    o.position = float4(ndc.x, -ndc.y, 0.0, 1.0);
    \\    return o;
    \\}
    \\float box(float2 p, float2 c, float2 h, float r) {
    \\    float2 q = abs(p - c) - (h - r);
    \\    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    \\}
    \\float segment(float2 p, float4 rect, float4 s) {
    \\    float2 a = rect.xy + s.xy * rect.zw;
    \\    float2 d = rect.xy + s.zw * rect.zw - a;
    \\    float l2 = dot(d, d);
    \\    float t = l2 > 0.0 ? clamp(dot(p - a, d) / l2, 0.0, 1.0) : 0.0;
    \\    return length(p - a - t * d);
    \\}
    \\float4 ps(Out i) : SV_Target {
    \\    if (i.pos.x < i.clip.x || i.pos.y < i.clip.y || i.pos.x >= i.clip.z || i.pos.y >= i.clip.w) discard;
    \\    float4 fill = float4(i.color.rgb * i.color.a, i.color.a);
    \\    if (i.params.z > 3.5) {
    \\        uint tw, th;
    \\        images.GetDimensions(tw, th);
    \\        float2 half_texel = 0.5 / float2(tw, th);
    \\        float2 uv = clamp(i.uv, min(i.seg.xy, i.seg.zw) + half_texel, max(i.seg.xy, i.seg.zw) - half_texel);
    \\        float2 h = i.rect.zw * 0.5;
    \\        float cover = clamp(0.5 - box(i.pos, i.rect.xy + h, h, i.params.x), 0.0, 1.0);
    \\        return images.Sample(linear_sampler, uv) * (i.color.a * cover);
    \\    }
    \\    if (i.params.z > 2.5) {
    \\        float2 h = i.seg.zw * 0.5;
    \\        float d = box(i.pos, i.seg.xy + h, h, i.params.x);
    \\        return fill * (1.0 - smoothstep(-i.params.w, i.params.w, d));
    \\    }
    \\    if (i.params.z > 1.5) {
    \\        float d = min(segment(i.pos, i.rect, i.seg), segment(i.pos, i.rect, i.border)) - i.params.y * 0.5;
    \\        return fill * clamp(0.5 - d, 0.0, 1.0);
    \\    }
    \\    if (i.params.z > 0.5) {
    \\        return fill * atlas.Sample(point_sampler, i.uv);
    \\    }
    \\    float2 h = i.rect.zw * 0.5;
    \\    float2 ctr = i.rect.xy + h;
    \\    float r = i.params.x;
    \\    float bw = i.params.y;
    \\    float outer = clamp(0.5 - box(i.pos, ctr, h, r), 0.0, 1.0);
    \\    if (bw > 0.0) {
    \\        float4 border = float4(i.border.rgb * i.border.a, i.border.a);
    \\        float inner = clamp(0.5 - box(i.pos, ctr, h - bw, max(r - bw, 0.0)), 0.0, 1.0);
    \\        return lerp(border, fill, inner) * outer;
    \\    }
    \\    return fill * outer;
    \\}
;
