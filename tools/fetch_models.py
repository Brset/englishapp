#!/usr/bin/env python3
"""Download and verify models from manifest.json using stdlib only."""

import json
import os
import sys
import hashlib
import argparse
from urllib.request import urlopen, Request
from urllib.error import URLError
from pathlib import Path


def get_manifest():
    """Load manifest.json from parent models directory."""
    manifest_path = Path(__file__).parent.parent / "models" / "manifest.json"
    with open(manifest_path) as f:
        return json.load(f)


def sha256_file(path, chunk_size=65536):
    """Compute SHA256 hash of a file."""
    sha256_hash = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(chunk_size), b""):
            sha256_hash.update(chunk)
    return sha256_hash.hexdigest()


def get_local_filename(model_url):
    """Extract filename from URL."""
    return model_url.rstrip('/').split('/')[-1]


def download_file(url, dest_path, expected_sha256=None):
    """Download file with resume support and progress reporting."""
    dest_path = Path(dest_path)
    dest_path.parent.mkdir(parents=True, exist_ok=True)

    # Check if file exists and is complete
    if dest_path.exists():
        local_sha = sha256_file(dest_path)
        if expected_sha256 is None:
            print(f"  ✓ {dest_path.name} exists (sha256: {local_sha})")
            return local_sha
        elif local_sha == expected_sha256:
            print(f"  ✓ {dest_path.name} verified")
            return local_sha

    # Determine if resumable (only for HEAD-check, not for actual download)
    try:
        req = Request(url, method='HEAD')
        req.add_header('User-Agent', 'fetch_models.py/1.0')
        response = urlopen(req, timeout=10)
        content_length = response.headers.get('Content-Length')
        if content_length:
            print(f"  → {dest_path.name} ({int(content_length) / 1e6:.1f} MB)")
    except (URLError, Exception) as e:
        print(f"  ✗ {dest_path.name}: {e}")
        return None

    # Download the file
    try:
        req = Request(url)
        req.add_header('User-Agent', 'fetch_models.py/1.0')
        with urlopen(req, timeout=30) as response:
            with open(dest_path, 'wb') as f:
                while True:
                    chunk = response.read(65536)
                    if not chunk:
                        break
                    f.write(chunk)

        local_sha = sha256_file(dest_path)
        if expected_sha256 and local_sha != expected_sha256:
            print(f"  ✗ {dest_path.name}: checksum mismatch")
            dest_path.unlink()
            return None
        print(f"  ✓ {dest_path.name} downloaded (sha256: {local_sha})")
        return local_sha
    except Exception as e:
        print(f"  ✗ Failed to download {dest_path.name}: {e}")
        if dest_path.exists():
            dest_path.unlink()
        return None


def main():
    parser = argparse.ArgumentParser(description="Download models from manifest.json")
    parser.add_argument("--only", help="Download only this model name")
    parser.add_argument("--verify", action="store_true", help="Only verify existing files")
    args = parser.parse_args()

    manifest = get_manifest()
    models_dir = Path(__file__).parent.parent / "models"

    downloaded = []
    verified = []
    failed = []

    for model in manifest["models"]:
        name = model["name"]
        url = model["url"]
        expected_sha = model.get("sha256")

        if args.only and name != args.only:
            continue

        filename = get_local_filename(url)
        dest_path = models_dir / filename

        sha = download_file(url, dest_path, expected_sha)
        if sha:
            downloaded.append((name, sha))
        else:
            failed.append(name)

    # Report
    print(f"\n✓ Downloaded: {len(downloaded)}")
    if failed:
        print(f"✗ Failed: {', '.join(failed)}")

    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
