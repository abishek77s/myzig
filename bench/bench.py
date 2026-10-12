#!/usr/bin/env python3
"""Benchmarks MyZig against MyGo and gpui-fast on the same three samples.

    python3 bench/bench.py setup          # clone MyGo and gpui-fast at the pinned commits
    python3 bench/bench.py all            # everything below, into bench/results/latest.json
    python3 bench/bench.py compile|size|memory|window|frames

It measures, on Linux:

- compile time of the counter sample: clean (no build cache, dependencies
  already downloaded) and incremental (one string of the sample changed),
  in each toolchain's debug and release modes;
- the size of each sample's release executable, stripped;
- memory: the proportional set size (PSS) of the counter idle six seconds
  after it starts, and of the quotes while they stream, as MyGo's
  internal/idlemem measures it on Linux;
- in a window (an X server, Xvfb without one): frames per second of the
  streaming quotes and the CPU time each frame costs the process;
- headless frame cost: building, laying out and painting a frame, and for
  MyZig and MyGo drawing it on the CPU (gpui-fast's harness counts the CPU
  work before the GPU draws).

Run it on an idle machine: compile times use every core.
"""

import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "bench"
CACHE = BENCH / ".cache"
MYGO_REPO, MYGO_COMMIT = "https://github.com/egoist/mygo", "abc00a4d5dd9d1edd6f44c71af019398338e6e56"
GPUI_REPO, GPUI_COMMIT = "https://github.com/longbridge/gpui-fast", "231f2cd339e96a9105d0d3b160b7c04ba358c762"
ZIG = os.environ.get("ZIG", "zig")
SAMPLES = ["counter", "quotes", "list"]
RESULTS = BENCH / "results" / "latest.json"


def sh(cmd, cwd=None, env=None, check=True, capture=True):
    e = dict(os.environ)
    if env:
        e.update(env)
    r = subprocess.run(cmd, cwd=cwd, env=e, shell=isinstance(cmd, str), text=True,
                       stdout=subprocess.PIPE if capture else None, stderr=subprocess.STDOUT if capture else None)
    if check and r.returncode != 0:
        sys.exit(f"failed: {cmd}\n{r.stdout}")
    return r.stdout or ""


def timed(cmd, **kw):
    t = time.perf_counter()
    sh(cmd, **kw)
    return time.perf_counter() - t


def load():
    return json.loads(RESULTS.read_text()) if RESULTS.exists() else {}


def save(res):
    RESULTS.parent.mkdir(parents=True, exist_ok=True)
    res["machine"] = machine()
    RESULTS.write_text(json.dumps(res, indent=2) + "\n")


def machine():
    cpu = re.search(r"model name\s*:\s*(.*)", Path("/proc/cpuinfo").read_text())
    return {
        "cpu": cpu.group(1) if cpu else "?",
        "cores": os.cpu_count(),
        "zig": sh(f"{ZIG} version").strip(),
        "go": sh("go version").strip(),
        "rustc": sh("rustc --version", cwd=CACHE / "gpui-fast", check=False).strip(),
    }


# ---- setup ----

def setup():
    CACHE.mkdir(parents=True, exist_ok=True)
    for name, repo, commit in [("mygo", MYGO_REPO, MYGO_COMMIT), ("gpui-fast", GPUI_REPO, GPUI_COMMIT)]:
        d = CACHE / name
        if not d.exists():
            sh(f"git clone --filter=blob:none {repo} {d}", capture=False)
        sh(f"git -C {d} checkout -q {commit}")
    # MyZig's samples as gpui_perf scenarios.
    perf = CACHE / "gpui-fast/crates/gpui_perf/src"
    shutil.copy(BENCH / "gpui/scenarios/myzig.rs", perf / "scenarios/myzig.rs")
    mod = perf / "scenarios/mod.rs"
    if "pub mod myzig;" not in mod.read_text():
        mod.write_text(mod.read_text() + "pub mod myzig;\n")
    lib = perf / "gpui_perf.rs"
    t = lib.read_text()
    if "scenarios::myzig" not in t:
        t = t.replace("    scenarios.extend(scenarios::chat_patterns::scenarios());",
                      "    scenarios.extend(scenarios::chat_patterns::scenarios());\n    scenarios.extend(scenarios::myzig::scenarios());")
        lib.write_text(t)
    shutil.copy(CACHE / "gpui-fast/Cargo.lock", BENCH / "gpui/Cargo.lock")
    sh("go mod download", cwd=BENCH / "go")
    sh("cargo fetch", cwd=BENCH / "gpui")
    sh("cargo fetch", cwd=CACHE / "gpui-fast")


# ---- compile time ----

def edit(path, old, new):
    t = path.read_text()
    path.write_text(t.replace(old, new, 1))


def compile_times(res):
    out = {}
    zig_cache = Path.home() / ".cache/zig"
    counter_zig = ROOT / "examples/counter.zig"
    for mode in ["Debug", "ReleaseFast"]:
        shutil.rmtree(ROOT / ".zig-cache", ignore_errors=True)
        shutil.rmtree(zig_cache, ignore_errors=True)
        clean = timed(f"{ZIG} build -Donly=counter -Doptimize={mode}", cwd=ROOT)
        inc = []
        for i in range(3):
            edit(counter_zig, '"Counter"', f'"Counter{i}"')
            inc.append(timed(f"{ZIG} build -Donly=counter -Doptimize={mode}", cwd=ROOT))
            edit(counter_zig, f'"Counter{i}"', '"Counter"')
        sh(f"{ZIG} build -Donly=counter -Doptimize={mode}", cwd=ROOT)
        out[f"myzig {mode}"] = {"clean_s": clean, "incremental_s": statistics.median(inc)}

    go_main = BENCH / "go/counter/main.go"
    for mode, flags in [("default", ""), ("release", "-trimpath -ldflags='-s -w'")]:
        cache = tempfile.mkdtemp()
        env = {"GOCACHE": cache, "CGO_ENABLED": "0"}
        clean = timed(f"go build {flags} -o /tmp/myzig-bench-go-counter ./counter", cwd=BENCH / "go", env=env)
        inc = []
        for i in range(3):
            edit(go_main, 'Title: "Counter"', f'Title: "Counter{i}"')
            inc.append(timed(f"go build {flags} -o /tmp/myzig-bench-go-counter ./counter", cwd=BENCH / "go", env=env))
            edit(go_main, f'Title: "Counter{i}"', 'Title: "Counter"')
        shutil.rmtree(cache, ignore_errors=True)
        out[f"mygo {mode}"] = {"clean_s": clean, "incremental_s": statistics.median(inc)}

    rs_main = BENCH / "gpui/src/bin/counter.rs"
    for mode, flags in [("debug", ""), ("release", "--release")]:
        target = tempfile.mkdtemp()
        env = {"CARGO_TARGET_DIR": target}
        clean = timed(f"cargo build --offline {flags} --bin counter", cwd=BENCH / "gpui", env=env)
        inc = []
        for i in range(3):
            edit(rs_main, '"↑ and ↓ count too"', f'"↑ and ↓ count too{i}"')
            inc.append(timed(f"cargo build --offline {flags} --bin counter", cwd=BENCH / "gpui", env=env))
            edit(rs_main, f'"↑ and ↓ count too{i}"', '"↑ and ↓ count too"')
        if mode == "release":
            shutil.copy(Path(target) / "release/counter", "/tmp/myzig-bench-gpui-counter")
        shutil.rmtree(target, ignore_errors=True)
        out[f"gpui-fast {mode}"] = {"clean_s": clean, "incremental_s": statistics.median(inc)}
    res["compile"] = out
    return out


# ---- executables ----

BIN = Path(tempfile.gettempdir()) / "myzig-bench-bin"


def build_all():
    """Release executables of every sample, in BIN."""
    BIN.mkdir(exist_ok=True)
    for mode, suffix in [("ReleaseSmall", "small"), ("ReleaseFast", "fast")]:
        sh(f"{ZIG} build -Doptimize={mode}", cwd=ROOT)
        for s in SAMPLES:
            shutil.copy(ROOT / "zig-out/bin" / s, BIN / f"myzig-{s}-{suffix}")
    for s in SAMPLES:
        sh(f"go build -trimpath -ldflags='-s -w' -o {BIN}/mygo-{s} ./{s}", cwd=BENCH / "go", env={"CGO_ENABLED": "0"})
    sh("cargo build --offline --release --bin counter", cwd=BENCH / "gpui")
    shutil.copy(BENCH / "gpui/target/release/counter", BIN / "gpui-counter")
    sh(f"{ZIG} build -Doptimize=ReleaseFast", cwd=ROOT)


def sizes(res):
    build_all()
    res["size"] = {p.name: p.stat().st_size for p in sorted(BIN.iterdir())}
    return res["size"]


# ---- memory and windows ----

class Display:
    """An X server: the session's, or Xvfb when there is none."""

    def __enter__(self):
        self.proc = None
        if not os.environ.get("BENCH_DISPLAY"):
            self.display = ":97"
            self.proc = subprocess.Popen(["Xvfb", self.display, "-screen", "0", "1920x1200x24", "-nolisten", "tcp"],
                                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(1)
        else:
            self.display = os.environ["BENCH_DISPLAY"]
        return self

    def __exit__(self, *a):
        if self.proc:
            self.proc.terminate()
            self.proc.wait()


def tree(pid):
    """pid and its descendants."""
    pids = [pid]
    for p in Path("/proc").iterdir():
        if p.name.isdigit():
            try:
                ppid = int(Path(p, "stat").read_text().rsplit(")", 1)[1].split()[1])
            except (OSError, IndexError):
                continue
            if ppid == pid:
                pids += tree(int(p.name))
    return pids


def pss_kb(pid):
    total = 0
    for p in tree(pid):
        try:
            m = re.search(r"^Pss:\s+(\d+) kB", Path(f"/proc/{p}/smaps_rollup").read_text(), re.M)
            total += int(m.group(1))
        except OSError:
            pass
    return total


def cpu_s(pid):
    total = 0
    for p in tree(pid):
        try:
            f = Path(f"/proc/{p}/stat").read_text().rsplit(")", 1)[1].split()
            total += int(f[11]) + int(f[12])  # utime + stime
        except OSError:
            pass
    return total / os.sysconf("SC_CLK_TCK")


def start(cmd, display, env=None):
    e = dict(os.environ, DISPLAY=display)
    e.update(env or {})
    return subprocess.Popen(cmd, env=e, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def memory(res):
    out = {}
    with Display() as d:
        for name, cmd in [("myzig counter", [BIN / "myzig-counter-fast"]), ("mygo counter", [BIN / "mygo-counter"]),
                          ("gpui-fast counter", [BIN / "gpui-counter"]),
                          ("myzig quotes", [BIN / "myzig-quotes-fast"]), ("mygo quotes", [BIN / "mygo-quotes"])]:
            runs = []
            for _ in range(3):
                p = start([str(c) for c in cmd], d.display)
                time.sleep(6)
                alive = p.poll() is None
                runs.append(pss_kb(p.pid) if alive else None)
                p.kill()
                o = p.communicate()[0]
                if not alive:
                    out[name] = {"error": o[-400:]}
                    break
            else:
                out[name] = {"pss_mb": statistics.median(runs) / 1024}
    res["memory"] = out
    return out


def window(res):
    """Streaming quotes and an auto-scrolling list in a window: frames per
    second, and the process's CPU time per frame."""
    out = {}
    secs = 10
    with Display() as d:
        cases = [
            ("myzig quotes", [BIN / "myzig-quotes-fast"], {"MYZIG_STATS": "1"}),
            ("myzig quotes uncapped", [BIN / "myzig-quotes-fast"], {"MYZIG_STATS": "1", "MYZIG_UNCAPPED": "1"}),
            ("mygo quotes", [BIN / "mygo-quotes"], {"MYGO_BENCH_STATS": "1"}),
            ("myzig list (wheel)", [BIN / "myzig-list-fast"], {"MYZIG_STATS": "1"}),
            ("mygo list (wheel)", [BIN / "mygo-list"], {"MYGO_BENCH_STATS": "1"}),
        ]
        for name, cmd, env in cases:
            p = start([str(c) for c in cmd], d.display, env)
            time.sleep(2)
            wheel = None
            if "wheel" in name:
                # 60 wheel notches a second over the window, as a fast scroll.
                wid = sh(f"xdotool search --sync --onlyvisible --name List", env={"DISPLAY": d.display}).split()[0]
                sh(f"xdotool mousemove --window {wid} 300 400", env={"DISPLAY": d.display})
                wheel = subprocess.Popen(["sh", "-c", f"for i in $(seq {secs * 60 + 120}); do xdotool click 5; sleep 0.0166; done"],
                                         env=dict(os.environ, DISPLAY=d.display))
            c0 = cpu_s(p.pid)
            t0 = time.perf_counter()
            time.sleep(secs)
            c1 = cpu_s(p.pid)
            t1 = time.perf_counter()
            if wheel:
                wheel.kill()
            p.kill()
            log = p.communicate()[0]
            fps = [float(x) for x in re.findall(r"fps=([\d.]+)", log)]
            fps = fps[2:-1] or fps  # the seconds it was measured in
            f = statistics.median(fps) if fps else 0
            out[name] = {"fps": f, "cpu_percent": (c1 - c0) / (t1 - t0) * 100,
                         "cpu_ms_per_frame": (c1 - c0) * 1000 / max(1, f * (t1 - t0))}
    res["window"] = out
    return out


# ---- headless frames ----

def frames(res):
    out = {}
    o = sh(f"{ZIG} build bench -Doptimize=ReleaseFast", cwd=ROOT)
    for line in o.splitlines():
        m = re.match(r"(.+?)\s+([\d.]+) ms/frame \(build\+layout\+paint ([\d.]+), raster ([\d.]+)", line)
        if m:
            out["myzig " + m.group(1).strip()] = {"ms": float(m.group(2)), "cpu_before_raster_ms": float(m.group(3))}
    o = sh("go test -run '^$' -bench . -benchtime 2s -count 3 ./...", cwd=BENCH / "go")
    vals = {}
    for line in o.splitlines():
        m = re.match(r"Benchmark(\w+)-\d+\s+\d+\s+([\d.]+) ns/op(?:\s+([\d.]+) ns/frame)?", line)
        if m:
            vals.setdefault(m.group(1), []).append(float(m.group(3) or m.group(2)) / 1e6)
    names = {"Unchanged": "counter unchanged", "Streaming": "quotes streaming", "Scrolling": "list scrolling"}
    for k, v in vals.items():
        out["mygo " + names[k]] = {"ms": statistics.median(v)}
    gp = CACHE / "gpui-fast"
    sh("cargo build --offline --release -p gpui_perf", cwd=gp)
    j = Path(tempfile.mktemp(suffix=".json"))
    sh(f"./target/release/gpui_perf --headless --scenario myzig --frames 300 --retention on --json {j}", cwd=gp)
    data = json.loads(j.read_text())
    names = {"myzig-counter-unchanged": "counter unchanged", "myzig-quotes-streaming": "quotes streaming",
             "myzig-list-scroll": "list scrolling"}
    for sc in data["scenarios"]:
        if sc["name"] in names:
            run = next(r for r in sc["runs"] if r["retention"])
            out["gpui-fast " + names[sc["name"]]] = {"cpu_before_raster_ms": run["frame"]["mean_ms"],
                                                      "allocations": run["allocations"]}
    res["frames"] = out
    return out


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "all"
    if cmd == "setup":
        return setup()
    res = load()
    steps = {"compile": compile_times, "size": sizes, "memory": memory, "window": window, "frames": frames}
    if cmd == "all":
        for f in steps.values():
            print(json.dumps(f(res), indent=2))
            save(res)
    else:
        if cmd in ("memory", "window") and not BIN.exists():
            build_all()
        print(json.dumps(steps[cmd](res), indent=2))
        save(res)


if __name__ == "__main__":
    main()
