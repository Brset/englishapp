#!/usr/bin/env python3
"""Verify the Windows publish directory.

Usage: check_win_bundle.py <publish dir> [--engine-dir build/engine/Release] [--dumpbin PATH]
dumpbin must be on PATH (ilammy/msvc-dev-cmd) or passed with --dumpbin.
"""
import argparse, glob, os, re, shutil, subprocess, sys

REQUIRED = ["EnglishApp.exe", "pron_engine.dll", "onnxruntime.dll", "sherpa-onnx-c-api.dll",
            "resources.pri", "content.db", "user_schema.sql"]
MODELS = ["whisper/ggml-base.en.bin", "vad/silero_vad.onnx", "phoneme/model.onnx", "phoneme/vocab.json",
          "cmudict/cmudict.dict", "tts/us/model.onnx", "tts/us/tokens.txt", "tts/us/espeak-ng-data/", "tts/gb/"]
SYS_EXACT = {"kernel32", "user32", "advapi32", "ole32", "oleaut32", "shell32", "ws2_32", "ucrtbase",
             "gdi32", "shlwapi", "bcrypt", "crypt32", "ntdll", "combase", "rpcrt4", "winmm", "version",
             "setupapi", "imm32", "comdlg32", "mfplat", "mfreadwrite", "mf", "mmdevapi", "avrt", "dbghelp",
             "psapi", "iphlpapi", "secur32", "userenv", "shcore", "ncrypt", "dxgi", "d3d11", "d2d1"}
VC_RE = re.compile(r"^(vcruntime140(_1)?|msvcp140(_\d+)?|concrt140|vcomp140)$")
failures = []
def ok(m): print(f"PASS: {m}")
def fail(m): print(f"FAIL: {m}"); failures.append(m)
def warn(m): print(f"WARN: {m}")

def fsize(p): return os.path.getsize(p)

def ort_version(p):
    data = open(p, "rb").read()
    pv = re.search(rb"P\x00r\x00o\x00d\x00u\x00c\x00t\x00V\x00e\x00r\x00s\x00i\x00o\x00n\x00\x00\x00((?:[0-9.]\x00)+)", data)
    return pv.group(1).decode("utf-16-le") if pv else None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pub"); ap.add_argument("--engine-dir", default="build/engine/Release")
    ap.add_argument("--dumpbin")
    a = ap.parse_args()
    pub = a.pub
    present = {f.lower() for f in os.listdir(pub)}

    print("== required files ==")
    for f in REQUIRED:
        (ok if os.path.exists(os.path.join(pub, f)) else fail)(f"{f} " + ("present" if os.path.exists(os.path.join(pub, f)) else "MISSING"))
    boot = "microsoft.windowsappruntime.bootstrap.dll" in present
    xaml = "microsoft.ui.xaml.dll" in present
    (ok if (boot or xaml) else fail)(f"WinAppSDK runtime ({'Bootstrap.dll ' if boot else ''}{'Microsoft.ui.xaml.dll' if xaml else ''}".strip() + ")" if (boot or xaml) else "WinAppSDK runtime DLLs MISSING (Bootstrap or Microsoft.ui.xaml.dll)")
    xbf = glob.glob(os.path.join(pub, "**", "*.xbf"), recursive=True)
    (ok if xbf else fail)(f"compiled XAML (*.xbf): {len(xbf)} files" if xbf else "no *.xbf compiled XAML found")

    print("== sherpa onnxruntime check ==")
    src = os.path.join(a.engine_dir, "onnxruntime.dll")
    dst = os.path.join(pub, "onnxruntime.dll")
    if os.path.exists(src) and os.path.exists(dst):
        sv, dv = ort_version(src), ort_version(dst)
        print(f"  engine copy: {fsize(src):,} bytes, version {sv}\n  bundle copy: {fsize(dst):,} bytes, version {dv}")
        if fsize(src) == fsize(dst) and sv == dv: ok("bundled onnxruntime.dll matches build/engine/Release (sherpa) copy")
        else: fail("bundled onnxruntime.dll differs from the sherpa copy in " + a.engine_dir)
    else: fail(f"cannot compare onnxruntime.dll ({src} / {dst})")

    print("== models ==")
    for r in MODELS:
        p = os.path.join(pub, "models", r)
        if r.endswith("/"):
            n = sum(len(fs) for _, _, fs in os.walk(p)) if os.path.isdir(p) else 0
            (ok if n else fail)(f"models/{r} " + (f"({n} files)" if n else "MISSING or empty"))
        else:
            (ok if os.path.isfile(p) else fail)(f"models/{r} " + (f"({fsize(p):,} bytes)" if os.path.isfile(p) else "MISSING"))

    print("== DLL import closure ==")
    dumpbin = a.dumpbin or shutil.which("dumpbin")
    if not dumpbin: fail("dumpbin not found on PATH (need ilammy/msvc-dev-cmd)")
    else:
        vc_needed = set()
        for dll in ("pron_engine.dll", "sherpa-onnx-c-api.dll"):
            p = os.path.join(pub, dll)
            if not os.path.exists(p): continue
            out = subprocess.run([dumpbin, "/dependents", p], capture_output=True, text=True).stdout
            sec = out.split("image has the following dependencies:")[-1].split("Summary")[0]
            deps = [l.strip() for l in sec.splitlines() if l.strip().lower().endswith(".dll")]
            print(f"  {dll}: {', '.join(deps)}")
            for d in deps:
                base = d.lower()[:-4]
                if d.lower() in present: continue
                if VC_RE.match(base): vc_needed.add(d); continue
                if base.startswith("api-ms-win-") or base.startswith("ext-ms-win-") or base in SYS_EXACT: continue
                if os.path.exists(os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32", d)):
                    warn(f"{dll} imports {d}: not in bundle, assumed system DLL"); continue
                fail(f"{dll} imports {d}: not in bundle and not a known system DLL")
        for d in sorted(vc_needed):
            if d.lower() in present: ok(f"VC runtime {d} bundled")
            else: fail(f"VC runtime {d} imported but NOT in bundle (VC++ redistributable required; copy from VS redist)")
        if not vc_needed: ok("no VC runtime imports unresolved")

    print()
    if failures:
        print(f"RESULT: FAIL ({len(failures)} problem(s))"); sys.exit(1)
    print("RESULT: PASS")
main()
