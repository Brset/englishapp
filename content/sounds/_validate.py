#!/usr/bin/env python3
"""Validate content/sounds/*.json against ../schema/sound.schema.json."""
import json
import sys
from pathlib import Path

from jsonschema import Draft202012Validator

HERE = Path(__file__).resolve().parent
SCHEMA = HERE.parent / "schema" / "sound.schema.json"
TEXT_SCHEMA = HERE.parent / "schema" / "text.schema.json"


def main() -> int:
    errors = []
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    validator = Draft202012Validator(schema)
    enum = schema["properties"]["id"]["enum"]
    text_enum = json.loads(TEXT_SCHEMA.read_text(encoding="utf-8"))["properties"]["focus_sounds"]["items"]["properties"]["sound"]["enum"]
    if enum != text_enum:
        errors.append("id enum differs from text.schema.json focus_sounds.sound enum")
    if len(enum) != 31:
        errors.append(f"expected 31 ids in enum, got {len(enum)}")

    files = sorted(p for p in HERE.glob("*.json"))
    if len(files) != 31:
        errors.append(f"expected 31 sound files, found {len(files)}")

    ids, slugs = [], []
    for p in files:
        data = json.loads(p.read_text(encoding="utf-8"))
        for e in validator.iter_errors(data):
            errors.append(f"{p.name}: {'/'.join(map(str, e.absolute_path))}: {e.message[:150]}")
        ids.append(data.get("id"))
        slugs.append(data.get("slug"))
        if data.get("slug") != p.stem:
            errors.append(f"{p.name}: slug {data.get('slug')!r} does not match filename")
        if data.get("slug") and not data["slug"].isascii():
            errors.append(f"{p.name}: slug is not ASCII")
        # no duplicate minimal pairs / example words
        mp = [(x["target"], x["contrast"]) for x in data.get("minimal_pairs", [])]
        if len(mp) != len(set(mp)):
            errors.append(f"{p.name}: duplicate minimal pair")
        for x in data.get("minimal_pairs", []):
            if x["target"] == x["contrast"]:
                errors.append(f"{p.name}: pair with identical words {x['target']}")
        ew = [x["word"] for x in data.get("example_words", [])]
        if len(ew) != len(set(ew)):
            errors.append(f"{p.name}: duplicate example word")

    if len(set(slugs)) != len(slugs):
        errors.append("slugs are not unique")
    if len(set(ids)) != len(ids):
        errors.append("ids are not unique")
    if set(ids) != set(enum):
        errors.append(f"id mismatch: missing {sorted(set(enum) - set(ids))}, extra {sorted(set(ids) - set(enum))}")

    if errors:
        print("FAIL")
        for e in errors:
            print(" -", e)
        return 1
    print(f"OK: {len(files)} files, {len(set(slugs))} unique slugs, all {len(enum)} ids match enum")
    return 0


if __name__ == "__main__":
    sys.exit(main())
