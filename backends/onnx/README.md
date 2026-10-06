# pron_onnx — бэкенды на ONNX Runtime

Статическая библиотека `pron_onnx`: `pron::SileroVad` (IVad) и `pron::Wav2Vec2PhonemeModel` (IPhonemeModel).
Заголовок: `pron/onnx_backends.h`. Аудио — 16 кГц mono float (другая частота для VAD ресемплируется линейно).

## Сборка (Linux x64)
    cmake -S backends/onnx -B build-onnx && cmake --build build-onnx && ctest --test-dir build-onnx
ONNX Runtime (`ONNXRUNTIME_VERSION`, по умолчанию 1.22.0) скачивается автоматически; можно указать готовую
распаковку: `-DONNXRUNTIME_ROOT=/path/to/onnxruntime-...` (нужны `include/` и `lib/`).
Пути к заголовкам ядра: `-DPRON_CORE_INCLUDE_DIR=...` (по умолчанию `../../core/include`).
Для тестов нужны python3 с пакетами `onnx`, `numpy` (фейковая wav2vec2-модель) и доступ к GitHub (Silero).
Отключить тесты: `-DPRON_ONNX_BUILD_TESTS=OFF`.

## Windows
Скачайте `onnxruntime-win-x64-<ver>.zip` с github.com/microsoft/onnxruntime/releases, распакуйте и передайте
`-DONNXRUNTIME_ROOT=<папка>` (`include/`, `lib/onnxruntime.lib`). Рядом с exe положите `onnxruntime.dll`.
(Без `ONNXRUNTIME_ROOT` CMake сам скачает win-x64 zip.)

## Android
Скачайте `onnxruntime-android-<ver>.aar` (Maven Central), переименуйте в .zip и распакуйте. Соберите папку:
`headers/` -> `include/`, `jni/<abi>/libonnxruntime.so`. Укажите `-DONNXRUNTIME_ROOT=<папка>` с
подпапками `headers/` (или `include/`) и `jni/` и ABI через `-DANDROID_ABI=arm64-v8a` (NDK toolchain).
Библиотеку `libonnxruntime.so` нужно упаковать в APK (jniLibs).

## Модели
- Silero VAD v5/v6 (`silero_vad.onnx`): вход 512 отсчётов + 64 контекста, порог 0.5, речь >=250 мс, пауза >=100 мс, pad 30 мс.
- wav2vec2 CTC (`model.onnx` + `vocab.json`): blank = id `<pad>`, нормализация (среднее 0, дисперсия 1), log-softmax по кадрам.
