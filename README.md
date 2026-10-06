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
