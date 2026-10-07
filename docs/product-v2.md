# Product v2 — features and design (Android + Windows implement the same spec)

Texts are now long (700–1300 words, 6–12 paragraphs). The app must make long reading comfortable,
motivating and measurable. UI language: Russian. Keep everything offline.

## 1. Reading coverage % (must-have)
- While reading (live tracker), show a big live percentage "Прочитано 37%" = words in state `read`
  / total words × 100, rounded; plus a thin progress bar at the top of the text.
- After reading, store per attempt: `coverage_pct`, `skipped_count`, duration, words per minute.
- Library card shows best coverage % as a ring (0–100) and the latest pronunciation score.
- Text is "пройден" when coverage ≥ 90 %; 50–89 % = "начат"; shown as a badge.

## 2. Long-text reading flow
- Paragraph mode (default for texts > 300 words): one paragraph highlighted at a time, others dimmed;
  "Дальше" / auto-advance when the paragraph is fully read. Whole-text mode stays available.
- Resume: remember last read word per text; "Продолжить с места" button.
- Each paragraph is a separate processing job (smaller, faster heavy assessment); the text result is
  aggregated across paragraphs. Heavy queue shows "Абзац 3/8".
- Teleprompter auto-scroll (already there) + adjustable font size (S/M/L/XL) and line spacing.

## 3. Home screen (dashboard)
- Greeting + streak flame (days in a row) + daily goal ring (minutes read today vs goal, default 10).
- "Продолжить чтение" card (last text, % done, paragraph N/M).
- "Текст дня" card (level-appropriate, not read yet).
- "Слабые звуки" chips (top 3 from phoneme stats) → opens drill.
- Weekly bar chart (minutes per day, last 7 days).

## 4. Practice tools
- Word drill (spaced repetition): words with low scores are auto-added; card = word + IPA +
  translation; "Послушать" (TTS) → "Сказать" (live engine on the single word) → instant score
  (green/yellow/red) → SRS interval update (again / hard / good / easy).
- Sound drill: pick a sound card → minimal pairs; the app says one word of the pair, user says it;
  score shown instantly. Shows articulation tips from the sound card.
- Shadowing: per sentence — play TTS, then user repeats; per-sentence score and coverage.

## 5. Gamification (light)
- XP: 1 XP per word read, +bonus for score ≥ 80; levels with names (Новичок → Мастер).
- Achievements (badges): first text, 7-day streak, 10 000 words, all A1 texts, perfect sound "θ", etc.
- Celebration overlay after a text (coverage %, score, XP gained, new badges).

## 6. Progress screen
- Accuracy over time (line), minutes per week (bars), top weak sounds with trend, texts per level
  (rings A1–C2), total words read.

## 7. Onboarding (first launch)
- 3 short screens: what the app does, microphone permission, choose level (or "Определить
  уровень": read a short passage, suggest level by score) + daily goal + accent US/UK.

## 8. Design system
- Android: Material 3 with dynamic color (Android 12+), fallback brand palette (deep indigo +
  warm amber accent), rounded cards (16 dp), large readable reading typography (serif option),
  dark theme. Smooth animations for state changes (word highlight fade, progress rings).
- Windows: Fluent / Mica backdrop, NavigationView left, same card layout and palette, dark theme.
- Score colours everywhere: ≥ 80 green, 60–79 amber, < 60 red; read = soft green background,
  current = accent, skipped = orange, pending = muted.
- Empty states and loading skeletons; no blank screens.

## Data (user.db, add via CREATE TABLE IF NOT EXISTS / guarded ALTER in app code)
- attempts: add coverage_pct, skipped_count, duration_sec, wpm, paragraph_index (nullable).
- reading_position(text_id PK, word_index, paragraph_index, updated_at).
- xp_log(id, amount, reason, created_at); achievements(id PK, unlocked_at).
- daily_activity(date PK, minutes, words, xp).
