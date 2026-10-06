import copy
import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import build_content_db as b
import validate_texts as v

ROOT = b.ROOT


def make_text(tid: str, level: str, genre: str, title: str) -> dict:
    words = "The quick brown fox jumps over the lazy dog near a river bank today. "
    text = (words * 8).strip()
    # pad/trim to a valid A1 count
    t = {
        "id": tid, "level": level, "genre": genre, "title_en": title, "title_ru": "Заголовок " + title,
        "description_ru": "Описание", "text": text, "word_count": v.count_words(text),
        "vocabulary": [{"word": w, "ipa_us": "x", "ipa_uk": "x", "translation_ru": "т"}
                       for w in ["quick", "brown", "fox", "jumps", "lazy", "river"]],
        "focus_sounds": [{"sound": "w", "examples": ["quick", "river"], "tip_ru": "совет"},
                         {"sound": "r", "examples": ["brown", "river"], "tip_ru": "совет"}],
        "questions": [{"question": f"Q{i}?", "answer": "A", "accepted_keywords": ["fox", "dog"]} for i in range(3)],
    }
    return t


class BuildTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = Path(self.tmp.name)
        self.texts, self.sounds, self.out = d / "texts", d / "sounds", d / "out" / "content.db"
        for tid, genre, title in [("a1-story-02", "story", "Zebra River"), ("a1-story-01", "story", "Apple Fox"),
                                  ("a1-humor-01", "humor", "Funny Dog")]:
            t = make_text(tid, "A1", genre, title)
            (self.texts / "a1").mkdir(parents=True, exist_ok=True)
            (self.texts / "a1" / f"{tid}.json").write_text(json.dumps(t, ensure_ascii=False), encoding="utf-8")
        self.sounds.mkdir()
        (self.sounds / "th-voiceless.json").write_text(
            json.dumps({"id": "th1", "name_ru": "Глухой th", "ipa": "θ", "difficulty": 3, "extra": [1]}, ensure_ascii=False))
        self.schema = ROOT / "content" / "db" / "schema.sql"

    def tearDown(self):
        self.tmp.cleanup()

    def build(self):
        return b.build(self.texts, self.sounds, self.schema, self.out)

    def test_rows_and_order(self):
        # fixture words must pass real validation (A1 70-120)
        self.assertEqual(v.check(self.texts / "a1" / "a1-story-01.json"), [])
        r = self.build()
        self.assertEqual(len(r["texts"]), 3)
        con = sqlite3.connect(self.out)
        ids = [x[0] for x in con.execute("SELECT id FROM texts ORDER BY sort_order")]
        self.assertEqual(ids, ["a1-humor-01", "a1-story-01", "a1-story-02"])
        self.assertEqual(con.execute("SELECT count(*) FROM text_vocabulary").fetchone()[0], 18)
        self.assertEqual(con.execute("SELECT count(*) FROM text_focus_sounds").fetchone()[0], 6)
        self.assertEqual(con.execute("SELECT count(*) FROM text_focus_examples").fetchone()[0], 12)
        self.assertEqual(con.execute("SELECT count(*) FROM text_questions").fetchone()[0], 9)
        self.assertEqual(con.execute("SELECT count(*) FROM text_question_keywords").fetchone()[0], 18)
        row = con.execute("SELECT id, slug, name_ru, ipa, difficulty, json FROM sounds").fetchone()
        self.assertEqual(row[:5], ("th1", "th-voiceless", "Глухой th", "θ", "3"))
        self.assertEqual(json.loads(row[5])["extra"], [1])
        meta = dict(con.execute("SELECT key, value FROM meta"))
        self.assertEqual(meta["schema_version"], "1")
        self.assertEqual(len(meta["content_hash"]), 64)
        self.assertEqual(con.execute("PRAGMA integrity_check").fetchone()[0], "ok")

    def test_fts(self):
        self.build()
        con = sqlite3.connect(self.out)
        q = "SELECT t.id FROM texts_fts f JOIN texts t ON t.rowid = f.rowid WHERE texts_fts MATCH ? ORDER BY t.sort_order"
        self.assertEqual([x[0] for x in con.execute(q, ("zebra",))], ["a1-story-02"])
        self.assertEqual([x[0] for x in con.execute(q, ("заголовок AND funny",))], ["a1-humor-01"])
        self.assertEqual(len(con.execute(q, ("fox",)).fetchall()), 3)

    def test_foreign_keys(self):
        self.build()
        con = sqlite3.connect(self.out)
        self.assertEqual(con.execute("PRAGMA foreign_key_check").fetchall(), [])
        con.execute("PRAGMA foreign_keys = ON")
        with self.assertRaises(sqlite3.IntegrityError):
            con.execute("INSERT INTO text_vocabulary VALUES ('nope',0,'a','b','c','d')")

    def test_deterministic(self):
        import os
        os.environ["SOURCE_DATE_EPOCH"] = "1700000000"
        try:
            h1 = self.build()["hash"]
            d1 = self.out.read_bytes()
            h2 = self.build()["hash"]
            self.assertEqual(h1, h2)
            self.assertEqual(d1, self.out.read_bytes())
        finally:
            del os.environ["SOURCE_DATE_EPOCH"]

    def test_invalid_text_aborts(self):
        p = self.texts / "a1" / "a1-story-01.json"
        t = json.loads(p.read_text(encoding="utf-8"))
        t["word_count"] = 5
        p.write_text(json.dumps(t, ensure_ascii=False), encoding="utf-8")
        with self.assertRaises(SystemExit) as cm:
            self.build()
        self.assertIn("a1-story-01.json", str(cm.exception))
        self.assertFalse(self.out.exists())

    def test_missing_sounds_skipped(self):
        for f in self.sounds.glob("*.json"):
            f.unlink()
        self.assertEqual(self.build()["sounds"], 0)

    def test_user_schema_and_attach(self):
        self.build()
        user = Path(self.tmp.name) / "user.db"
        con = sqlite3.connect(user)
        con.executescript((ROOT / "content" / "db" / "user_schema.sql").read_text(encoding="utf-8"))
        con.execute("PRAGMA foreign_keys = ON")
        con.execute("INSERT INTO progress(text_id, status) VALUES ('a1-story-01','started')")
        con.execute("INSERT INTO saved_words(word, due_at, created_at) VALUES ('fox', 1, 1)")
        con.commit()
        con.close()
        con = sqlite3.connect(user)
        con.execute(f"ATTACH DATABASE 'file:{self.out}?mode=ro' AS content")
        n = con.execute("SELECT count(*) FROM progress p JOIN content.texts t ON t.id = p.text_id").fetchone()[0]
        self.assertEqual(n, 1)
        with self.assertRaises(sqlite3.OperationalError):
            con.execute("DELETE FROM content.texts")
        con.close()
        # rebuilding content must not affect user data
        self.build()
        con = sqlite3.connect(user)
        self.assertEqual(con.execute("SELECT count(*) FROM saved_words").fetchone()[0], 1)


if __name__ == "__main__":
    unittest.main()
