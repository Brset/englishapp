# Модели

Раскладка каталога определена в `engine/include/pron/pron_engine.h`:

```
<out>/whisper/ggml-base.en.bin
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
python tools/export_phoneme_model.py --out models      # facebook/wav2vec2-lv-60-espeak-cv-ft -> int8 ONNX
```

Скрипты идемпотентны, загрузка докачивается (`.part`). Архивы голосов распаковываются, `.onnx` переименовывается в `model.onnx`.
В CI: `uses: ./.github/actions/models` (вход `out`, по умолчанию `models`; кэш по хешу манифеста и скриптов).

Файлы моделей не отслеживаются в Git (см. `.gitignore`).
