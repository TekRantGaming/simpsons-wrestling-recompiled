"""Train the profile-guided build: python tools/pgo_train.py --disc <cue> [--seconds N] [--runs N]

Runs the instrumented game from build-pgo-gen/ (build-pgo.bat builds it) on the
attract demo, closes its window so the profile is written, and merges the runs
into pgo/windows.profdata, which build.bat compiles with. Windows only."""
import argparse, ctypes, ctypes.wintypes as wt, glob, os, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build-pgo-gen")
EXE = os.path.join(BUILD, "SimpsonsWrestling_Recompiled.exe")
OUT = os.path.join(ROOT, "pgo", "windows.profdata")
SETTINGS = """[video]
fullscreen = 0
window_width = 1280
vsync = "on"
internal_resolution = "4k"
antialiasing = true
fxaa = true
geometry_correction = true
perspective_texturing = true
"""

user32 = ctypes.WinDLL("user32")
EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def close_windows(pid):
    """Post WM_CLOSE to the game's windows; returns how many it found."""
    found = []
    def visit(hwnd, _):
        owner = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True
    user32.EnumWindows(EnumProc(visit), 0)
    for hwnd in found:
        user32.PostMessageW(hwnd, 0x0010, 0, 0)
    return len(found)


def find_profdata():
    tool = shutil.which("llvm-profdata")
    if tool:
        return tool
    vs = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    for path in glob.glob(os.path.join(vs, "Microsoft Visual Studio", "*", "*", "VC", "Tools", "Llvm",
                                       "x64", "bin", "llvm-profdata.exe")):
        return path
    sys.exit("llvm-profdata not found (install the VS 2022 'C++ Clang tools' component)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--disc", required=True, help="the game's .cue")
    ap.add_argument("--seconds", type=int, default=240, help="length of each run")
    ap.add_argument("--runs", type=int, default=1)
    args = ap.parse_args()
    if not os.path.isfile(EXE):
        sys.exit("build the instrumented game first: build-pgo.bat")
    raw_dir = os.path.join(BUILD, "pgo")
    shutil.rmtree(raw_dir, ignore_errors=True)
    os.makedirs(raw_dir)
    with open(os.path.join(BUILD, "settings.toml"), "w") as f:
        f.write(SETTINGS)
    env = dict(os.environ, LLVM_PROFILE_FILE=os.path.join(raw_dir, "run-%p.profraw"))
    for run in range(args.runs):
        print(f"training run {run + 1}/{args.runs}: {args.seconds} s of the attract demo")
        game = subprocess.Popen([EXE, "--no-launcher", "--disc", os.path.abspath(args.disc)], cwd=BUILD, env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            time.sleep(args.seconds)
            close_windows(game.pid)
            game.wait(timeout=30)    # a clean exit writes the .profraw
        except subprocess.TimeoutExpired:
            print("  the game did not close; this run's profile is lost")
        finally:
            if game.poll() is None:
                game.kill()
    raws = glob.glob(os.path.join(raw_dir, "*.profraw"))
    if not raws:
        sys.exit("no profile was written")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    subprocess.run([find_profdata(), "merge", "-sparse", "-output=" + OUT, *raws], check=True)
    print(f"wrote {OUT} from {len(raws)} run(s)")


if __name__ == "__main__":
    main()
