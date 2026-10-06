# TTS-бэкенд (Piper через sherpa-onnx)

`pron::PiperTts : ITts` — оффлайн-синтез голосами Piper (VITS ONNX) через C API sherpa-onnx
(v1.12.14, espeak-ng встроен). `speed` -> `length_scale = 1/speed`. `EspeakG2P` пока TODO
(C API sherpa-onnx фонемизацию не отдаёт; нужен libespeak-ng напрямую).

Голоса (формат sherpa-onnx: `*.onnx`, `tokens.txt`, `espeak-ng-data/`):
- https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-piper-en_US-lessac-medium.tar.bz2
- https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-piper-en_GB-alan-medium.tar.bz2

Сборка (Linux x64; библиотеки скачиваются сами): `cmake -S . -B build && cmake --build build && ctest --test-dir build`.
Тест сам качет голос lessac в `build/voice`, пишет WAV в `build/`; без сети — SKIP.

Windows x64: скачать `sherpa-onnx-v1.12.14-win-x64-shared.tar.bz2` (релиз v1.12.14), распаковать,
`-DSHERPA_ONNX_ROOT=<папка>`; положить `sherpa-onnx-c-api.dll` и `onnxruntime.dll` рядом с exe.

Android: скачать `sherpa-onnx-v1.12.14-android.tar.bz2` (jniLibs/<abi>/libsherpa-onnx-jni.so,
libonnxruntime.so; заголовки `c-api.h` взять из linux-архива). Собирать pron_tts с NDK и
`-DSHERPA_ONNX_ROOT=<папка с include/ и lib/ для нужной ABI>` (линковка с libsherpa-onnx-c-api.so
из этого каталога); голос и espeak-ng-data распаковать из assets в файловую систему приложения.

Параметры: `-DPRON_CORE_INCLUDE_DIR` (по умолчанию `../../core/include`), `-DSHERPA_ONNX_ROOT`.
