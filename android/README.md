# Android-приложение: тренажёр английского произношения

Kotlin + Jetpack Compose (Material 3), minSdk 28, ABI: arm64-v8a и x86_64. Работает офлайн.

- `app/src/main/cpp` — JNI-обёртка `pron_jni` над `core/` (assess, tokenize, lookup, loadCmudict, setVocab; всё возвращает JSON).
- `PronCore.kt` — Kotlin-фасад к JNI; `engine/Engines.kt` — интерфейсы `AsrEngine`/`PhonemeEngine` и заглушки (whisper / onnx — позже).
- `data/Database.kt` — обычный `android.database.sqlite`: `content.db` копируется из assets, `user.db` + `ATTACH content`, схема из `user_schema.sql`.
- `audio/AudioIO.kt` — запись 16 кГц mono PCM16, AudioTrack, системный TTS (временно, потом Piper).
- `ui/` — экраны на Compose, ViewModel + StateFlow, светлая/тёмная тема.

Сборка (нужны JDK 17, Android SDK, NDK 27.2.12479018, CMake 3.22.1):
1. В корне репозитория: `python tools/build_content_db.py` (создаёт `build/content.db`).
2. `cd android && ./gradlew assembleDebug` — задача `copyContentAssets` сама кладёт `content.db` и `user_schema.sql` в assets.
3. APK: `android/app/build/outputs/apk/debug/`.

Опционально (в `build/` до сборки): `cmudict.dict` и `phoneme_vocab.json` (`{"labels":[...],"blank":0}`) попадут в assets и загрузятся при старте.
Пока вместо ASR стоит заглушка «прочитано идеально», поэтому оценки демонстрационные. CI: `.github/workflows/android.yml`.
