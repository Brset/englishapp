#!/usr/bin/env python3
"""Verify a built APK: native libs, dependency closure, model assets, manifest.

Usage: check_apk.py <apk> [--readelf PATH] [--aapt2 PATH]
Env fallbacks: ANDROID_NDK_ROOT/ANDROID_NDK_HOME/ANDROID_HOME, ANDROID_SDK_ROOT.
"""
import argparse, glob, os, re, shutil, subprocess, sys, tempfile, zipfile

ABI = "lib/arm64-v8a/"
REQUIRED_LIBS = ["libpron_jni.so", "libpron_engine.so", "libonnxruntime.so",
                 "libsherpa-onnx-c-api.so", "libc++_shared.so"]
SYSTEM_LIBS = {"libc.so", "libm.so", "libdl.so", "liblog.so", "libandroid.so", "libz.so",
               "libOpenSLES.so", "libmediandk.so", "libEGL.so", "libGLESv2.so",
               "libvulkan.so", "libjnigraphics.so", "libnativewindow.so", "libsync.so"}
M = "assets/models/"
REQUIRED_ASSETS = ["whisper/ggml-base.en.bin", "vad/silero_vad.onnx", "phoneme/model.onnx",
                   "phoneme/vocab.json", "cmudict/cmudict.dict", "tts/us/model.onnx",
                   "tts/us/tokens.txt", "tts/us/espeak-ng-data/", "tts/gb/"]
OTHER_ASSETS = ["assets/content.db", "assets/user_schema.sql"]

failures = []
def ok(msg): print(f"PASS: {msg}")
def fail(msg): print(f"FAIL: {msg}"); failures.append(msg)

def find_tool(explicit, name, patterns):
    if explicit: return explicit
    w = shutil.which(name)
    if w: return w
    for p in patterns:
        hits = sorted(glob.glob(os.path.expandvars(p)))
        if hits: return hits[-1]
    return None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("apk"); ap.add_argument("--readelf"); ap.add_argument("--aapt2")
    a = ap.parse_args()
    sdk = os.environ.get("ANDROID_SDK_ROOT") or os.environ.get("ANDROID_HOME") or ""
    ndk = os.environ.get("ANDROID_NDK_ROOT") or os.environ.get("ANDROID_NDK_HOME") or f"{sdk}/ndk/27.2.12479018"
    readelf = find_tool(a.readelf, "llvm-readelf", [f"{ndk}/toolchains/llvm/prebuilt/*/bin/llvm-readelf"]) \
        or shutil.which("readelf")
    aapt2 = find_tool(a.aapt2, "aapt2", [f"{sdk}/build-tools/35.0.0/aapt2"])

    zf = zipfile.ZipFile(a.apk)
    infos = {i.filename: i for i in zf.infolist()}
    names = set(infos)

    # 1. required libs
    print("== native libs ==")
    present = {n[len(ABI):] for n in names if n.startswith(ABI) and n.endswith(".so") and "/" not in n[len(ABI):]}
    for l in REQUIRED_LIBS:
        (ok if l in present else fail)(f"{ABI}{l} " + ("present" if l in present else "MISSING"))

    # 2. dependency closure
    print("== dependency closure ==")
    if not readelf: fail("readelf/llvm-readelf not found")
    else:
        missing = {}
        with tempfile.TemporaryDirectory() as td:
            for so in sorted(present):
                p = zf.extract(ABI + so, td)
                out = subprocess.run([readelf, "-d", p], capture_output=True, text=True).stdout
                needed = re.findall(r"\(NEEDED\)\s+Shared library: \[(.+?)\]", out)
                print(f"  {so}: {', '.join(needed) or '(none)'}")
                for n in needed:
                    if n not in present and n not in SYSTEM_LIBS:
                        missing.setdefault(n, []).append(so)
        if missing:
            for n, by in sorted(missing.items()): fail(f"unresolved DT_NEEDED {n} (needed by {', '.join(by)})")
        else: ok("all DT_NEEDED resolved within lib/arm64-v8a or Android system libs")

    # 3. assets
    print("== assets ==")
    rows = []
    for r in REQUIRED_ASSETS:
        if r.endswith("/"):
            hit = [n for n in names if n.startswith(M + r) and not n.endswith("/")]
            (ok if hit else fail)(f"{M}{r} " + (f"({len(hit)} files)" if hit else "MISSING or empty"))
            rows += [(n, infos[n]) for n in sorted(hit)[:0]]
        else:
            n = M + r
            (ok if n in names else fail)(f"{n} " + ("present" if n in names else "MISSING"))
    for n in OTHER_ASSETS:
        (ok if n in names else fail)(f"{n} " + ("present" if n in names else "MISSING"))
    for n, i in sorted(infos.items()):
        if n.startswith(M) and n.endswith((".onnx", ".bin")):
            if i.compress_type != zipfile.ZIP_STORED:
                fail(f"{n} is compressed (must be ZIP_STORED)")
        if n.startswith(M) or n in OTHER_ASSETS or n.startswith(ABI):
            if not n.endswith("/"): rows.append((n, i))
    print(f"\n{'size':>12} {'packed':>12}  method  entry")
    for n, i in rows:
        print(f"{i.file_size:>12,} {i.compress_size:>12,}  {'STORED' if i.compress_type == 0 else 'DEFL  '}  {n}")
    print()
    if not any(f.endswith("(must be ZIP_STORED)") for f in failures):
        ok(".onnx/.bin assets are ZIP_STORED")

    # 4. manifest
    print("== manifest ==")
    if not aapt2: fail("aapt2 not found (build-tools 35.0.0)")
    else:
        out = subprocess.run([aapt2, "dump", "badging", a.apk], capture_output=True, text=True)
        txt = out.stdout
        if out.returncode: fail("aapt2 dump badging failed: " + out.stderr.strip())
        else:
            for l in txt.splitlines():
                if l.startswith(("package:", "uses-permission", "launchable-activity", "sdkVersion", "native-code")): print("  " + l)
            (ok if "uses-permission: name='android.permission.RECORD_AUDIO'" in txt else fail)("uses-permission RECORD_AUDIO")
            (ok if re.search(r"launchable-activity: name='[^']+'", txt) else fail)("launchable activity")

    print()
    if failures:
        print(f"RESULT: FAIL ({len(failures)} problem(s))"); sys.exit(1)
    print("RESULT: PASS")

main()
