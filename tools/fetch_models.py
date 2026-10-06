#!/usr/bin/env python3
"""Fetch models into the layout required by engine/include/pron/pron_engine.h.

  python tools/fetch_models.py --out models [--only NAME ...]

Stdlib only. Resumable (.part + Range), sha256-verified where the manifest has a hash
(computed hashes are printed otherwise), idempotent (finished targets are skipped).
The "phoneme" entry is produced by tools/export_phoneme_model.py, not downloaded.
"""
import argparse
import bz2  # noqa: F401  (tarfile needs it; fail early if missing)
import hashlib
import json
import shutil
import sys
import tarfile
import tempfile
import time
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parent.parent
UA = "fetch_models.py/2.0"


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(url, dest, expected=None, retries=4):
    """Download url to dest (resumable). Returns sha256 or raises."""
    dest = Path(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        got = sha256_file(dest)
        if expected is None or got == expected:
            return got
        print(f"  hash mismatch on existing {dest.name}, refetching")
        dest.unlink()
    part = dest.with_name(dest.name + ".part")
    for attempt in range(1, retries + 1):
        have = part.stat().st_size if part.exists() else 0
        req = Request(url, headers={"User-Agent": UA})
        if have:
            req.add_header("Range", f"bytes={have}-")
        try:
            with urlopen(req, timeout=60) as r:
                status = getattr(r, "status", 200)
                mode = "ab"
                if have and status != 206:  # server ignored Range
                    mode, have = "wb", 0
                total = r.headers.get("Content-Length")
                total = int(total) + have if total else None
                done, last = have, time.time()
                with open(part, mode) as f:
                    while True:
                        chunk = r.read(1 << 20)
                        if not chunk:
                            break
                        f.write(chunk)
                        done += len(chunk)
                        if time.time() - last > 2:
                            last = time.time()
                            pct = f" {100 * done // total}%" if total else ""
                            print(f"  {dest.name}: {done / 1e6:.1f} MB{pct}", flush=True)
                if total is not None and done != total:
                    raise IOError(f"short read {done}/{total}")
            break
        except HTTPError as e:
            if e.code == 416 and part.exists():  # part is already complete
                break
            if attempt == retries or e.code in (401, 403, 404):
                raise
            print(f"  retry {attempt}: {e}")
        except (URLError, IOError, TimeoutError) as e:
            if attempt == retries:
                raise
            print(f"  retry {attempt}: {e}")
            time.sleep(2 * attempt)
    got = sha256_file(part)
    if expected and got != expected:
        part.unlink()
        raise ValueError(f"sha256 mismatch for {url}: got {got}, expected {expected}")
    part.replace(dest)
    return got


def safe_extract(tar_path, target):
    target = Path(target).resolve()
    with tarfile.open(tar_path, "r:bz2") as tf:
        for m in tf.getmembers():
            p = (target / m.name).resolve()
            if target != p and target not in p.parents:
                raise ValueError(f"unsafe path in archive: {m.name}")
            if m.issym() or m.islnk() or m.isdev():
                raise ValueError(f"unsupported member in archive: {m.name}")
        if hasattr(tarfile, "data_filter"):
            tf.extractall(target, filter="data")
        else:
            tf.extractall(target)


def voice_ready(d):
    return ((d / "model.onnx").is_file() and (d / "tokens.txt").is_file()
            and (d / "espeak-ng-data").is_dir())


def prune_siblings(m, out, dest):
    """Remove stale files next to dest matching m["exclusive_glob"] (e.g. old ggml-*.bin)."""
    pat = m.get("exclusive_glob")
    if not pat:
        return
    for p in dest.parent.glob(pat):
        if p.name != dest.name and p.is_file():
            print(f"  removing stale {p.relative_to(out)}")
            p.unlink()


def fetch_file(m, out, cache):
    dest = out / m["dest"]
    exp = m.get("sha256")
    if dest.exists():
        got = sha256_file(dest)
        if exp is None or got == exp:
            prune_siblings(m, out, dest)
            return got, "present"
        dest.unlink()
    sha = download(m["url"], dest, exp)
    prune_siblings(m, out, dest)
    return sha, "downloaded"


def fetch_voice(m, out, cache):
    dest = out / m["dest"]
    if voice_ready(dest):
        return None, "present"
    archive = cache / m["url"].rsplit("/", 1)[-1]
    sha = download(m["url"], archive, m.get("sha256"))
    with tempfile.TemporaryDirectory(dir=cache) as tmp:
        safe_extract(archive, tmp)
        roots = [p for p in Path(tmp).iterdir() if p.is_dir()]
        src = roots[0] if len(roots) == 1 else Path(tmp)
        onnx = sorted(p for p in src.glob("*.onnx") if "int8" not in p.name) or sorted(src.glob("*.onnx"))
        if not onnx or not (src / "tokens.txt").is_file() or not (src / "espeak-ng-data").is_dir():
            raise ValueError(f"unexpected archive contents in {archive.name}")
        stage = Path(tmp) / "_stage"
        stage.mkdir()
        shutil.copy2(onnx[0], stage / "model.onnx")
        shutil.copy2(src / "tokens.txt", stage / "tokens.txt")
        shutil.copytree(src / "espeak-ng-data", stage / "espeak-ng-data")
        if dest.exists():
            shutil.rmtree(dest)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(stage), str(dest))
    archive.unlink()
    return sha, "downloaded"


def asr_ready(m, d):
    return all((d / name).is_file() for name in m["pick"])


def fetch_asr(m, out, cache):
    dest = out / m["dest"]
    if asr_ready(m, dest):
        return None, "present"
    archive = cache / m["url"].rsplit("/", 1)[-1]
    sha = download(m["url"], archive, m.get("sha256"))
    with tempfile.TemporaryDirectory(dir=cache) as tmp:
        safe_extract(archive, tmp)
        roots = [p for p in Path(tmp).iterdir() if p.is_dir()]
        src = roots[0] if len(roots) == 1 else Path(tmp)
        stage = Path(tmp) / "_stage"
        stage.mkdir()
        for name, alts in m["pick"].items():
            found = None
            for pat in alts.split("|"):
                hits = sorted(src.glob(pat))
                if hits:
                    found = hits[0]
                    break
            if found is None:
                raise ValueError(f"{name}: nothing matching '{alts}' in {archive.name}")
            shutil.copy2(found, stage / name)
        if dest.exists():
            shutil.rmtree(dest)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(stage), str(dest))
    archive.unlink()
    return sha, "downloaded"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="models", help="output models directory (default: models)")
    ap.add_argument("--only", nargs="+", metavar="NAME", help="only these manifest entries")
    ap.add_argument("--manifest", default=str(ROOT / "models" / "manifest.json"))
    args = ap.parse_args()

    manifest = json.loads(Path(args.manifest).read_text(encoding="utf-8"))
    names = {m["name"] for m in manifest["models"]}
    if args.only and (bad := set(args.only) - names):
        print(f"unknown names: {', '.join(sorted(bad))}; known: {', '.join(sorted(names))}")
        return 2
    out = Path(args.out)
    cache = out / ".cache"
    cache.mkdir(parents=True, exist_ok=True)

    failed = []
    for m in manifest["models"]:
        name = m["name"]
        if args.only and name not in args.only:
            continue
        kind = m["kind"]
        if kind == "export":
            print(f"[{name}] built by {m['script']} --out {out}")
            continue
        print(f"[{name}]")
        try:
            sha, state = {"file": fetch_file, "tar_voice": fetch_voice, "tar_asr": fetch_asr}[kind](m, out, cache)
            note = f" sha256={sha}" if sha and not m.get("sha256") else ""
            print(f"  ok: {m['dest']} ({state}){note}")
        except Exception as e:  # noqa: BLE001
            print(f"  FAILED: {e}")
            failed.append(name)
    try:
        cache.rmdir()
    except OSError:
        pass
    if failed:
        print("failed: " + ", ".join(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
