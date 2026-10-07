#!/usr/bin/env python3
"""Export an English wav2vec2 CTC phoneme model to ONNX + int8 (MatMul only).

  python tools/export_phoneme_model.py --out models [--model ID|gruut|espeak]

The model can also be chosen with the env var PRON_PHONEME_MODEL (CI switch; --model wins).
Aliases: "gruut" = bookbot/wav2vec2-ljspeech-gruut (default), "espeak" = facebook/wav2vec2-lv-60-espeak-cv-ft.
When a model is chosen explicitly there is NO silent fallback to the other one.

Default: bookbot/wav2vec2-ljspeech-gruut (BASE size, ~95M params, IPA vocab -> ~95-110 MB int8).
Documented fallback: --model facebook/wav2vec2-lv-60-espeak-cv-ft (LARGE, ~300 MB int8).
If the default model fails to download/convert, the fallback is used automatically.

Writes <out>/phoneme/model.onnx (input `input_values` [1, N] float32 16 kHz, output
`logits` [1, T, V]) and <out>/phoneme/vocab.json ({token: id}).

Pinned dependencies (tested set; CPU only):
  pip install --index-url https://download.pytorch.org/whl/cpu "torch==2.5.1"
  pip install "transformers==4.46.3" "onnx==1.17.0" "onnxruntime==1.20.1" "numpy<2.1"
"""
import argparse
import json
import os
import sys
import tempfile
import traceback
from pathlib import Path

DEFAULT_MODEL = "bookbot/wav2vec2-ljspeech-gruut"
FALLBACK_MODEL = "facebook/wav2vec2-lv-60-espeak-cv-ft"


def load_vocab(model_id):
    """Return flat {token: id}; handles flat and nested {"en": {...}} vocab.json."""
    vocab = None
    try:
        from transformers import AutoProcessor
        proc = AutoProcessor.from_pretrained(model_id)
        tok = getattr(proc, "tokenizer", proc)
        vocab = tok.get_vocab()
    except Exception as e:  # noqa: BLE001
        print(f"  processor vocab unavailable ({e!r}); reading vocab.json")
    if not vocab:
        from huggingface_hub import hf_hub_download
        vocab = json.loads(Path(hf_hub_download(model_id, "vocab.json")).read_text(encoding="utf-8"))
    if vocab and all(isinstance(v, dict) for v in vocab.values()):  # nested {"en": {...}}
        vocab = vocab.get("en") or next(iter(vocab.values()))
    return {str(k): int(v) for k, v in vocab.items()}


def export(model_id, out):
    import numpy as np
    import onnxruntime as ort
    import torch
    from onnxruntime.quantization import QuantType, quantize_dynamic
    from transformers import Wav2Vec2ForCTC

    target, vocab_path = out / "model.onnx", out / "vocab.json"
    model = Wav2Vec2ForCTC.from_pretrained(model_id).eval()
    model.config.return_dict = True
    print(f"model {model_id}: {sum(p.numel() for p in model.parameters()) / 1e6:.0f}M params")

    class Wrap(torch.nn.Module):
        def __init__(self, m):
            super().__init__()
            self.m = m

        def forward(self, input_values):
            return self.m(input_values).logits

    with tempfile.TemporaryDirectory() as tmp:
        fp32 = Path(tmp) / "model_fp32.onnx"
        dummy = torch.randn(1, 16000)
        with torch.no_grad():
            torch.onnx.export(
                Wrap(model), (dummy,), str(fp32),
                input_names=["input_values"], output_names=["logits"],
                dynamic_axes={"input_values": {1: "samples"}, "logits": {1: "frames"}},
                opset_version=17, do_constant_folding=True,
                dynamo=False,  # legacy TorchScript exporter: single-file, stable on torch 2.5
            )
        print(f"fp32 export: {fp32.stat().st_size / 1e6:.0f} MB")
        # MatMul only: ORT's CPU provider has no kernel for ConvInteger (conv feature extractor).
        quantize_dynamic(str(fp32), str(target), weight_type=QuantType.QInt8,
                         op_types_to_quantize=["MatMul"], use_external_data_format=False)
    print(f"int8 model: {target.stat().st_size / 1e6:.0f} MB")

    vocab = load_vocab(model_id)
    vocab_path.write_text(json.dumps(vocab, ensure_ascii=False, indent=0), encoding="utf-8")
    print(f"vocab size: {len(vocab)} -> {vocab_path}")
    print("vocab tokens: " + json.dumps([t for t, _ in sorted(vocab.items(), key=lambda kv: kv[1])],
                                        ensure_ascii=False))

    sess = ort.InferenceSession(str(target), providers=["CPUExecutionProvider"])
    x = np.random.randn(1, 16000).astype(np.float32)
    logits = sess.run(["logits"], {"input_values": x})[0]
    print(f"verify: logits shape {logits.shape}")
    if logits.ndim != 3 or logits.shape[0] != 1 or logits.shape[2] < len(vocab) - 8:
        raise RuntimeError(f"unexpected logits shape {logits.shape} for vocab {len(vocab)}")



def _utf8_stdio() -> None:
    # Windows consoles default to cp1252; the vocab printout contains IPA (ɪ, ə, ...).
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


def main():
    _utf8_stdio()
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="models")
    ap.add_argument("--model", default=None,
                    help=f"HF model id or alias gruut|espeak (default: $PRON_PHONEME_MODEL, else {DEFAULT_MODEL}; "
                         f"fallback {FALLBACK_MODEL} only for the default)")
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()

    chosen = args.model or os.environ.get("PRON_PHONEME_MODEL", "").strip() or None
    explicit = chosen is not None
    aliases = {"gruut": DEFAULT_MODEL, "espeak": FALLBACK_MODEL, "large": FALLBACK_MODEL, "small": DEFAULT_MODEL}
    args.model = aliases.get(chosen.lower(), chosen) if chosen else DEFAULT_MODEL
    print(f"phoneme model: {args.model} ({'explicit' if explicit else 'default'})")

    out = Path(args.out) / "phoneme"
    if (out / "model.onnx").is_file() and (out / "vocab.json").is_file() and not args.force:
        print(f"{out / 'model.onnx'} exists, skipping (use --force to rebuild)")
        return 0
    out.mkdir(parents=True, exist_ok=True)

    try:
        export(args.model, out)
        return 0
    except Exception:  # noqa: BLE001
        traceback.print_exc()
        if args.model == FALLBACK_MODEL or explicit:
            return 1
        print(f"\n*** WARNING: {args.model} failed; falling back to {FALLBACK_MODEL} "
              f"(much larger, ~300 MB int8 -> APK grows) ***\n", flush=True)
    try:
        export(FALLBACK_MODEL, out)
        return 0
    except Exception:  # noqa: BLE001
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
