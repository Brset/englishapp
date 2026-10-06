# pron_engine

Одна общая библиотека (`libpron_engine.so` / `pron_engine.dll`): core + whisper.cpp + Silero VAD / wav2vec2 (onnxruntime) + Piper TTS (sherpa-onnx).
Экспортирует весь `pron_c.h` и `pron_engine.h`. Макет папки моделей — в комментарии `include/pron/pron_engine.h`.

## Версии (закреплены)
- whisper.cpp v1.9.5 (FetchContent, статически)
- sherpa-onnx **1.12.14** (shared; несёт собственный onnxruntime)
- onnxruntime **1.17.1** — ровно та версия, что внутри sherpa-onnx. `backends/onnx` линкуется с `libonnxruntime` из архива sherpa
  (в процессе одна копия ORT); заголовки берутся из официального релиза ORT 1.17.1.

## Prebuilt (скачиваются CMake автоматически, либо `-DSHERPA_ONNX_ROOT=`, `-DONNXRUNTIME_HEADERS_ROOT=`)
- linux-x64: https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.12.14/sherpa-onnx-v1.12.14-linux-x64-shared.tar.bz2
- win-x64: https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.12.14/sherpa-onnx-v1.12.14-win-x64-shared.tar.bz2
- android (arm64-v8a, x86_64): https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.12.14/sherpa-onnx-v1.12.14-android.tar.bz2
  (jniLibs/<abi>/libonnxruntime.so; C-заголовки берутся из linux-архива)
- заголовки ORT: https://github.com/microsoft/onnxruntime/releases/download/v1.17.1/onnxruntime-linux-x64-1.17.1.tgz (только `include/`)

Windows и Android собраны по тем же правилам, но здесь не проверялись (только Linux). На Android при `-DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake -DANDROID_ABI=...`
нужно, чтобы `libonnxruntime.so` и `libsherpa-onnx-c-api.so` (или `...-jni.so`) лежали рядом с `libpron_engine.so` в APK.

## Сборка и тесты (Linux)
    cmake -S engine -B build/engine -DCMAKE_BUILD_TYPE=Release
    cmake --build build/engine -j && ctest --test-dir build/engine
Тесты качают Silero VAD и голос lessac; whisper — tiny-модель из whisper.cpp только для проверки "не падает".
Рядом с `libpron_engine` при сборке копируются `libonnxruntime` и `libsherpa-onnx-c-api` (RPATH `$ORIGIN` / DLL рядом).
