# EnglishApp для Windows (WinUI 3)
Офлайн-тренажёр английского произношения: C# / WinUI 3, .NET 8, unpackaged, x64, интерфейс на русском.

**Сборка** (Windows 10 1809+, .NET 8 SDK / Visual Studio 2022):
1. `python tools/build_content_db.py` — собирает `build/content.db`.
2. `cmake -S engine -B build/engine -A x64` и `cmake --build build/engine --config Release --target pron_engine` — получается `pron_engine.dll` (+ onnxruntime/sherpa-onnx DLL).
3. `dotnet publish windows/EnglishApp/EnglishApp.csproj -c Release -r win-x64 --self-contained -p:Platform=x64` — рядом с exe копируются `content.db`, `user_schema.sql` (`-p:ContentDb=`); `pron_engine.dll`, зависимые DLL и папку `models/` добавляет CI-воркфлоу.

CI: `.github/workflows/windows.yml` выполняет всё это и выкладывает артефакт `EnglishApp-win-x64`.

**Структура:** `Native/PronCore.cs` (P/Invoke, SafeHandle, записи JSON), `Services/` (ContentRepository: user.db + ATTACH content.db read-only; NAudio; интерфейсы ASR/фонем/TTS), `ViewModels/` + `Views/` (MVVM, CommunityToolkit.Mvvm).

**Горячие клавиши («Запись»):** Пробел — старт/стоп, R — прослушать образец.

**TODO:** ASR (whisper.cpp), фонемная модель (onnxruntime) и TTS (piper) — пока заглушки; озвучка через системные голоса Windows. Без ASR разбор показывает слова как нераспознанные.
Данные пользователя: `%LOCALAPPDATA%\EnglishApp\user.db`, записи — в `recordings\`.
