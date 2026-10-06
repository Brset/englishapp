-- Content schema (read-only at runtime). Built by tools/build_content_db.py into build/content.db.
-- The app opens user.db (see user_schema.sql) and ATTACHes content.db READ-ONLY, so content
-- updates replace the file without touching user data. Tables here must never be written by the app.
PRAGMA foreign_keys = ON;

CREATE TABLE meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
) WITHOUT ROWID;   -- schema_version, built_at, content_hash

CREATE TABLE texts (
    id             TEXT PRIMARY KEY,           -- e.g. a1-story-01
    level          TEXT NOT NULL CHECK (level IN ('A1','A2','B1','B2','C1','C2')),
    genre          TEXT NOT NULL,
    title_en       TEXT NOT NULL,
    title_ru       TEXT NOT NULL,
    description_ru TEXT NOT NULL,
    body           TEXT NOT NULL,              -- paragraphs separated by \n\n
    word_count     INTEGER NOT NULL,
    sort_order     INTEGER NOT NULL UNIQUE     -- stable library order (level, genre, id)
);
CREATE INDEX idx_texts_level_genre ON texts(level, genre, sort_order);

CREATE TABLE text_vocabulary (
    text_id        TEXT NOT NULL REFERENCES texts(id) ON DELETE CASCADE,
    position       INTEGER NOT NULL,
    word           TEXT NOT NULL,
    ipa_us         TEXT NOT NULL,
    ipa_uk         TEXT NOT NULL,
    translation_ru TEXT NOT NULL,
    PRIMARY KEY (text_id, position)
) WITHOUT ROWID;

CREATE TABLE text_focus_sounds (
    id       INTEGER PRIMARY KEY,
    text_id  TEXT NOT NULL REFERENCES texts(id) ON DELETE CASCADE,
    position INTEGER NOT NULL,
    sound    TEXT NOT NULL,
    tip_ru   TEXT NOT NULL,
    UNIQUE (text_id, position)
);
CREATE INDEX idx_focus_sound ON text_focus_sounds(sound);

CREATE TABLE text_focus_examples (
    focus_id INTEGER NOT NULL REFERENCES text_focus_sounds(id) ON DELETE CASCADE,
    position INTEGER NOT NULL,
    example  TEXT NOT NULL,
    PRIMARY KEY (focus_id, position)
) WITHOUT ROWID;

CREATE TABLE text_questions (
    id       INTEGER PRIMARY KEY,
    text_id  TEXT NOT NULL REFERENCES texts(id) ON DELETE CASCADE,
    position INTEGER NOT NULL,
    question TEXT NOT NULL,
    answer   TEXT NOT NULL,
    UNIQUE (text_id, position)
);

CREATE TABLE text_question_keywords (
    question_id INTEGER NOT NULL REFERENCES text_questions(id) ON DELETE CASCADE,
    keyword     TEXT NOT NULL,
    PRIMARY KEY (question_id, keyword)
) WITHOUT ROWID;

CREATE TABLE sounds (
    id         TEXT PRIMARY KEY,
    slug       TEXT NOT NULL UNIQUE,
    name_ru    TEXT,
    ipa        TEXT,
    difficulty TEXT,
    sort_order INTEGER NOT NULL,
    json       TEXT NOT NULL                   -- full card JSON, source of truth
);

-- Full-text search for the library (external content: indexes texts, no duplicate storage).
CREATE VIRTUAL TABLE texts_fts USING fts5(
    title_en, title_ru, body,
    content='texts', content_rowid='rowid',
    tokenize='unicode61 remove_diacritics 2'
);
