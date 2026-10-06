# English pronunciation trainer

Офлайн-тренажёр произношения английского для русскоязычных: читаешь текст вслух, приложение разбирает ошибки до отдельного звука. Два нативных приложения (Windows — C# / WinUI 3, Android — Kotlin / Jetpack Compose) на общем ядре C++ (whisper.cpp, ONNX Runtime, Piper, SQLite). Всё работает на устройстве, без интернета.

| Папка | Что внутри |
|---|---|
| `core/` | Общее ядро на C++: выравнивание, оценка звуков (GOP), беглость, советы, C API |
| `windows/` | Приложение для Windows |
| `android/` | Приложение для Android |
| `content/texts/` | 240 текстов A1–C2 (JSON, схема в `content/schema/text.schema.json`) |
| `content/sounds/` | Карточки звуков: артикуляция, минимальные пары, скороговорки |
| `tools/` | Проверка контента и сборка базы `content.db` |

Проверить тексты: `python tools/validate_texts.py`

## Как продолжить работу локально на ПК (Windows)

Нужно: Visual Studio 2022 (workloads «.NET desktop», «C++ desktop», Windows App SDK), .NET 8 SDK, CMake, Python 3.11+, Android Studio (SDK 34, NDK, CMake).

1. `python tools/validate_texts.py` и `python content/sounds/_validate.py` — проверка контента
2. `python tools/build_content_db.py` — собирает `build/content.db`
3. `python tools/fetch_models.py` — скачивает модели в `models/` (~600 МБ; ссылки Hugging Face ещё не проверены)
4. Ядро: `cmake -S core -B build/core && cmake --build build/core --config Release && ctest --test-dir build/core -C Release`
5. Windows: см. `windows/README.md`; Android: см. `android/README.md`

Состояние: тексты (240) и карточки звуков (31) готовы; ядро — логика оценки с тестами, без подключённых моделей; приложения — первая версия, ещё ни разу не собиралась на Windows/Android. Следующий шаг — собрать оба приложения, исправить ошибки сборки, затем подключить whisper.cpp / ONNX Runtime / Piper к интерфейсам в `core/include/pron/backends.h`.
