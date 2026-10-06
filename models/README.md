# Модели

Раскладка каталога определена в `engine/include/pron/pron_engine.h`:

```
<out>/whisper/ggml-base.en-q5_1.bin          # единственный ggml-*.bin (движок грузит любой ggml-*.bin)
<out>/vad/silero_vad.onnx
<out>/phoneme/model.onnx + vocab.json
<out>/cmudict/cmudict.dict
<out>/tts/us/{model.onnx,tokens.txt,espeak-ng-data/}   # sherpa-onnx Piper en_US lessac
<out>/tts/gb/{model.onnx,tokens.txt,espeak-ng-data/}   # sherpa-onnx Piper en_GB alan
```

`manifest.json` — источники (URL, sha256 где известен; иначе скрипт печатает вычисленный хеш, его можно вписать в манифест). cmudict берётся из `master` (коммит не закреплён).

## Использование

```bash
python tools/fetch_models.py --out models              # whisper, vad, cmudict, tts us/gb
python tools/fetch_models.py --out models --only vad tts-us
pip install --index-url https://download.pytorch.org/whl/cpu "torch==2.5.1"
pip install "transformers==4.46.3" "onnx==1.17.0" "onnxruntime==1.20.1" "numpy<2.1"
python tools/export_phoneme_model.py --out models      # bookbot/wav2vec2-ljspeech-gruut -> int8 ONNX
python tools/export_phoneme_model.py --out models --model facebook/wav2vec2-lv-60-espeak-cv-ft  # запасной вариант (~300 MB)
```

## Размеры (APK ~720 -> ~320 MB)

| Модель | Было | Стало |
|---|---|---|
| whisper | ggml-base.en 148 MB | ggml-base.en-q5_1 ~57 MB |
| phoneme | wav2vec2-lv-60 (~315M параметров), int8 ~300 MB | wav2vec2-base (~95M), int8 ~95-110 MB |

Если основная phoneme-модель не скачалась/не конвертировалась, экспорт сам откатывается на espeak-модель с предупреждением. Старый `ggml-*.bin` в `whisper/` удаляется при загрузке.

Скрипты идемпотентны, загрузка докачивается (`.part`). Архивы голосов распаковываются, `.onnx` переименовывается в `model.onnx`.
В CI: `uses: ./.github/actions/models` (вход `out`, по умолчанию `models`; кэш по хешу манифеста и скриптов).

Файлы моделей не отслеживаются в Git (см. `.gitignore`).
