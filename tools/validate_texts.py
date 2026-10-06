"""Validate content/texts/**/*.json against the schema plus level-specific rules.

Usage: python tools/validate_texts.py [path ...]   (default: all texts)
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEXTS = ROOT / "content" / "texts"
SCHEMA = json.loads((ROOT / "content" / "schema" / "text.schema.json").read_text(encoding="utf-8"))

WORD_RANGES = {"A1": (70, 120), "A2": (120, 180), "B1": (180, 280), "B2": (280, 400), "C1": (400, 550), "C2": (500, 700)}
WORD_RE = re.compile(r"[A-Za-z]+(?:['’][A-Za-z]+)*")


def count_words(text: str) -> int:
    # Dialogue speaker labels ("Anna:") are not read aloud.
    body = re.sub(r"^[A-Z][\w .]{0,20}:\s", "", text, flags=re.MULTILINE)
    return len(WORD_RE.findall(body))


def check(path: Path) -> list[str]:
    errors: list[str] = []
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        return [f"invalid JSON: {exc}"]
    try:
        import jsonschema
        for err in jsonschema.Draft202012Validator(SCHEMA).iter_errors(data):
            errors.append(f"schema: {'/'.join(map(str, err.path))}: {err.message}")
    except ImportError:
        missing = [k for k in SCHEMA["required"] if k not in data]
        if missing:
            errors.append(f"missing keys: {missing}")
    if errors:
        return errors

    if path.stem != data["id"]:
        errors.append(f"file name {path.stem} != id {data['id']}")
    level_prefix, genre = data["id"].split("-")[:2]
    if level_prefix.upper() != data["level"] or genre != data["genre"]:
        errors.append("id does not match level/genre")
    if path.parent.name != level_prefix:
        errors.append(f"file must be in texts/{level_prefix}/")
    actual = count_words(data["text"])
    if actual != data["word_count"]:
        errors.append(f"word_count {data['word_count']} but counted {actual}")
    lo, hi = WORD_RANGES[data["level"]]
    if not lo <= actual <= hi:
        errors.append(f"{actual} words, {data['level']} needs {lo}-{hi}")
    lower = data["text"].lower()
    for v in data["vocabulary"]:
        if v["word"].lower().split()[0] not in lower:
            errors.append(f"vocabulary word not in text: {v['word']}")
    for s in data["focus_sounds"]:
        for ex in s["examples"]:
            if ex.lower() not in lower:
                errors.append(f"focus example not in text: {ex}")
    return errors


def main(argv: list[str]) -> int:
    paths = [Path(p) for p in argv] or sorted(TEXTS.rglob("*.json"))
    bad = 0
    for p in paths:
        errs = check(p)
        if errs:
            bad += 1
            print(f"FAIL {p.relative_to(ROOT) if p.is_absolute() else p}")
            for e in errs:
                print(f"  - {e}")
    print(f"{len(paths) - bad}/{len(paths)} ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
