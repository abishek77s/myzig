const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const strip = b.option(bool, "strip", "Strip debug info from executables") orelse (optimize != .Debug);

    const mod = b.addModule("myzig", .{
        .root_source_file = b.path("src/myzig.zig"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    mod.addCSourceFile(.{ .file = b.path("vendor/stb_truetype.c"), .flags = &.{ "-O2", "-fno-sanitize=undefined" } });
    mod.linkSystemLibrary("X11", .{});
    mod.linkSystemLibrary("Xext", .{});

    const examples = [_][]const u8{ "counter", "quotes", "list" };
    const test_step = b.step("test", "Run the tests");
    inline for (examples) |name| {
        const exe = b.addExecutable(.{
            .name = name,
            .root_module = b.createModule(.{
                .root_source_file = b.path("examples/" ++ name ++ ".zig"),
                .target = target,
                .optimize = optimize,
                .strip = strip,
                .imports = &.{.{ .name = "myzig", .module = mod }},
            }),
        });
        b.installArtifact(exe);
        const run = b.addRunArtifact(exe);
        b.step(name, "Run the " ++ name ++ " example").dependOn(&run.step);
        const t = b.addTest(.{ .root_module = exe.root_module });
        test_step.dependOn(&b.addRunArtifact(t).step);
    }

    const bench = b.addExecutable(.{
        .name = "frames",
        .root_module = b.createModule(.{
            .root_source_file = b.path("bench/frames.zig"),
            .target = target,
            .optimize = optimize,
            .strip = strip,
            .imports = &.{.{ .name = "myzig", .module = mod }},
        }),
    });
    bench.root_module.addAnonymousImport("counter", .{ .root_source_file = b.path("examples/counter.zig"), .imports = &.{.{ .name = "myzig", .module = mod }} });
    bench.root_module.addAnonymousImport("quotes", .{ .root_source_file = b.path("examples/quotes.zig"), .imports = &.{.{ .name = "myzig", .module = mod }} });
    bench.root_module.addAnonymousImport("list", .{ .root_source_file = b.path("examples/list.zig"), .imports = &.{.{ .name = "myzig", .module = mod }} });
    b.installArtifact(bench);
    const run_bench = b.addRunArtifact(bench);
    b.step("bench", "Time headless frames of each workload").dependOn(&run_bench.step);

    const unit = b.addTest(.{ .root_module = mod });
    test_step.dependOn(&b.addRunArtifact(unit).step);
}
