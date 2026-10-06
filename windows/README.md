# EnglishApp для Windows (WinUI 3)
Офлайн-тренажёр английского произношения: C# / WinUI 3, .NET 8, unpackaged, x64, интерфейс на русском.

**Сборка** (Windows 10 1809+, .NET 8 SDK / Visual Studio 2022):
1. `python tools/build_content_db.py` — собирает `build/content.db`.
2. `cmake -S core -B build/native -A x64 -DPRON_BUILD_SHARED=ON -DPRON_BUILD_TESTS=OFF` и `cmake --build build/native --config Release --target pron_c` — получается `pron.dll`.
3. `dotnet publish windows/EnglishApp/EnglishApp.csproj -c Release -r win-x64 --self-contained -p:Platform=x64` — рядом с exe копируются `pron.dll`, `content.db`, `user_schema.sql` (пути: `-p:NativeDll=`, `-p:ContentDb=`).

CI: `.github/workflows/windows.yml` выполняет всё это и выкладывает артефакт `EnglishApp-win-x64`.

**Структура:** `Native/PronCore.cs` (P/Invoke, SafeHandle, записи JSON), `Services/` (ContentRepository: user.db + ATTACH content.db read-only; NAudio; интерфейсы ASR/фонем/TTS), `ViewModels/` + `Views/` (MVVM, CommunityToolkit.Mvvm).

**Горячие клавиши («Запись»):** Пробел — старт/стоп, R — прослушать образец.

**TODO:** ASR (whisper.cpp), фонемная модель (onnxruntime) и TTS (piper) — пока заглушки; озвучка через системные голоса Windows. Без ASR разбор показывает слова как нераспознанные.
Данные пользователя: `%LOCALAPPDATA%\EnglishApp\user.db`, записи — в `recordings\`.
