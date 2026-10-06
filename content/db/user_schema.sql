-- User data schema. Lives in user.db, which survives content updates.
-- The app opens user.db and ATTACHes content.db read-only:
--   ATTACH DATABASE 'file:content.db?mode=ro' AS content;   (open with SQLITE_OPEN_URI)
-- Text/sound ids below refer to content.texts.id / content.sounds.id by value only (no
-- cross-database foreign keys in SQLite), so the app must tolerate ids missing after an update.
PRAGMA foreign_keys = ON;

CREATE TABLE user_meta (
    key TEXT PRIMARY KEY, value TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE progress (
    text_id        TEXT PRIMARY KEY,
    status         TEXT NOT NULL DEFAULT 'new' CHECK (status IN ('new','started','done')),
    best_score     REAL,
    attempts       INTEGER NOT NULL DEFAULT 0,
    last_opened_at INTEGER,          -- unix seconds
    completed_at   INTEGER
) WITHOUT ROWID;

CREATE TABLE recordings (
    id          INTEGER PRIMARY KEY,
    text_id     TEXT NOT NULL,
    kind        TEXT NOT NULL DEFAULT 'reading',   -- reading | question | sound
    file_path   TEXT NOT NULL,
    duration_ms INTEGER,
    score       REAL,
    scores_json TEXT,                              -- per-word / per-phoneme scores
    created_at  INTEGER NOT NULL
);
CREATE INDEX idx_recordings_text ON recordings(text_id, created_at);

CREATE TABLE phoneme_stats (
    phoneme    TEXT PRIMARY KEY,                   -- IPA symbol
    attempts   INTEGER NOT NULL DEFAULT 0,
    errors     INTEGER NOT NULL DEFAULT 0,
    avg_score  REAL,
    updated_at INTEGER
) WITHOUT ROWID;

CREATE TABLE saved_words (
    id            INTEGER PRIMARY KEY,
    word          TEXT NOT NULL UNIQUE,
    ipa           TEXT,
    translation_ru TEXT,
    source_text_id TEXT,
    ease          REAL NOT NULL DEFAULT 2.5,
    interval_days REAL NOT NULL DEFAULT 0,
    due_at        INTEGER NOT NULL,                -- unix seconds
    reps          INTEGER NOT NULL DEFAULT 0,
    lapses        INTEGER NOT NULL DEFAULT 0,
    created_at    INTEGER NOT NULL
);
CREATE INDEX idx_saved_words_due ON saved_words(due_at);

CREATE TABLE daily_streak (
    day            TEXT PRIMARY KEY,               -- local date YYYY-MM-DD
    minutes        REAL NOT NULL DEFAULT 0,
    texts_done     INTEGER NOT NULL DEFAULT 0,
    goal_met       INTEGER NOT NULL DEFAULT 0
) WITHOUT ROWID;

CREATE TABLE settings (
    key TEXT PRIMARY KEY, value TEXT NOT NULL
) WITHOUT ROWID;
