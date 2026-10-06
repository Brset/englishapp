"""Pack content/texts and content/sounds into build/content.db.

Usage: python tools/build_content_db.py [--texts DIR] [--sounds DIR] [--schema FILE] [--out FILE]
Validates every text first (validate_texts.check) and aborts listing all failures.
Deterministic: same inputs -> same rows and content_hash (built_at honours SOURCE_DATE_EPOCH).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import sqlite3
import sys
import time
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import validate_texts  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SCHEMA_VERSION = 1
LEVELS = ["A1", "A2", "B1", "B2", "C1", "C2"]


def collect_texts(texts_dir: Path) -> tuple[list[dict], list[str]]:
    texts, failures = [], []
    for p in sorted(texts_dir.rglob("*.json")):
        errs = validate_texts.check(p)
        if errs:
            failures.append(f"{p}\n" + "\n".join(f"  - {e}" for e in errs))
            continue
        texts.append(json.loads(p.read_text(encoding="utf-8")))
    texts.sort(key=lambda t: (LEVELS.index(t["level"]), t["genre"], t["id"]))
    return texts, failures


def collect_sounds(sounds_dir: Path) -> list[tuple[str, dict]]:
    files = sorted(sounds_dir.glob("*.json")) if sounds_dir.is_dir() else []
    if not files:
        print(f"WARNING: no sound cards in {sounds_dir}; skipping sounds", file=sys.stderr)
        return []
    return [(p.stem, json.loads(p.read_text(encoding="utf-8"))) for p in files]


def content_hash(texts, sounds) -> str:
    h = hashlib.sha256()
    h.update(json.dumps([texts, sounds], ensure_ascii=False, sort_keys=True).encode("utf-8"))
    return h.hexdigest()


def build(texts_dir: Path, sounds_dir: Path, schema: Path, out: Path) -> dict:
    texts, failures = collect_texts(texts_dir)
    if failures:
        raise SystemExit(f"ABORT: {len(failures)} text file(s) failed validation:\n\n" + "\n".join(failures))
    sounds = collect_sounds(sounds_dir)

    out.parent.mkdir(parents=True, exist_ok=True)
    for suffix in ("", "-journal", "-wal", "-shm"):
        Path(str(out) + suffix).unlink(missing_ok=True)
    con = sqlite3.connect(out)
    try:
        con.executescript(schema.read_text(encoding="utf-8"))
        con.execute("PRAGMA foreign_keys = ON")
        with con:
            for i, t in enumerate(texts, 1):
                con.execute("INSERT INTO texts VALUES (?,?,?,?,?,?,?,?,?)",
                            (t["id"], t["level"], t["genre"], t["title_en"], t["title_ru"],
                             t["description_ru"], t["text"], t["word_count"], i))
                for n, v in enumerate(t["vocabulary"]):
                    con.execute("INSERT INTO text_vocabulary VALUES (?,?,?,?,?,?)",
                                (t["id"], n, v["word"], v["ipa_us"], v["ipa_uk"], v["translation_ru"]))
                for n, f in enumerate(t["focus_sounds"]):
                    cur = con.execute("INSERT INTO text_focus_sounds(text_id,position,sound,tip_ru) VALUES (?,?,?,?)",
                                      (t["id"], n, f["sound"], f["tip_ru"]))
                    con.executemany("INSERT INTO text_focus_examples VALUES (?,?,?)",
                                    [(cur.lastrowid, k, ex) for k, ex in enumerate(f["examples"])])
                for n, q in enumerate(t["questions"]):
                    cur = con.execute("INSERT INTO text_questions(text_id,position,question,answer) VALUES (?,?,?,?)",
                                      (t["id"], n, q["question"], q["answer"]))
                    con.executemany("INSERT OR IGNORE INTO text_question_keywords VALUES (?,?)",
                                    [(cur.lastrowid, k.lower()) for k in q["accepted_keywords"]])
            for i, (slug, card) in enumerate(sounds, 1):
                con.execute("INSERT INTO sounds VALUES (?,?,?,?,?,?,?)",
                            (str(card.get("id", slug)), slug, card.get("name_ru"), card.get("ipa"),
                             None if card.get("difficulty") is None else str(card["difficulty"]),
                             i, json.dumps(card, ensure_ascii=False, sort_keys=True)))
            con.execute("INSERT INTO texts_fts(texts_fts) VALUES ('rebuild')")
            built = time.gmtime(int(os.environ.get("SOURCE_DATE_EPOCH", time.time())))
            chash = content_hash(texts, sounds)
            con.executemany("INSERT INTO meta VALUES (?,?)", [
                ("schema_version", str(SCHEMA_VERSION)),
                ("built_at", time.strftime("%Y-%m-%dT%H:%M:%SZ", built)),
                ("content_hash", chash),
            ])
        bad = con.execute("PRAGMA foreign_key_check").fetchall()
        if bad:
            raise SystemExit(f"ABORT: foreign key violations: {bad}")
        con.execute("PRAGMA user_version = %d" % SCHEMA_VERSION)
        con.commit()
        con.execute("VACUUM")
    finally:
        con.close()
    return {"texts": texts, "sounds": len(sounds), "hash": chash, "size": out.stat().st_size}


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--texts", type=Path, default=ROOT / "content" / "texts")
    ap.add_argument("--sounds", type=Path, default=ROOT / "content" / "sounds")
    ap.add_argument("--schema", type=Path, default=ROOT / "content" / "db" / "schema.sql")
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "content.db")
    a = ap.parse_args(argv)
    r = build(a.texts, a.sounds, a.schema, a.out)
    for lvl in LEVELS:
        c = Counter(t["genre"] for t in r["texts"] if t["level"] == lvl)
        if c:
            print(f"{lvl}: {sum(c.values()):3d}  " + ", ".join(f"{g}={n}" for g, n in sorted(c.items())))
    print(f"BUILD OK: {len(r['texts'])} texts, {r['sounds']} sounds, "
          f"{r['size'] / 1024:.1f} KiB, hash {r['hash'][:12]} -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
