#!/usr/bin/env python3
"""Export facebook/wav2vec2-lv-60-espeak-cv-ft (CTC, espeak IPA phoneme vocab) to ONNX + int8.

  python tools/export_phoneme_model.py --out models

Writes <out>/phoneme/model.onnx (dynamic int8, input `input_values` [1, N] float32 16 kHz
normalised waveform, output `logits` [1, T, V]) and <out>/phoneme/vocab.json.

Pinned dependencies (tested set; CPU only):
  pip install --index-url https://download.pytorch.org/whl/cpu "torch==2.5.1"
  pip install "transformers==4.46.3" "onnx==1.17.0" "onnxruntime==1.20.1" "numpy<2.1"
"""
import argparse
import json
import sys
import tempfile
from pathlib import Path

MODEL_ID = "facebook/wav2vec2-lv-60-espeak-cv-ft"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="models")
    ap.add_argument("--model", default=MODEL_ID)
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()

    out = Path(args.out) / "phoneme"
    target, vocab_path = out / "model.onnx", out / "vocab.json"
    if target.is_file() and vocab_path.is_file() and not args.force:
        print(f"{target} exists, skipping (use --force to rebuild)")
        return 0
    out.mkdir(parents=True, exist_ok=True)

    import numpy as np
    import onnxruntime as ort
    import torch
    from huggingface_hub import hf_hub_download
    from onnxruntime.quantization import QuantType, quantize_dynamic
    from transformers import Wav2Vec2ForCTC

    model = Wav2Vec2ForCTC.from_pretrained(args.model).eval()
    model.config.return_dict = True

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
        # Only MatMul: ORT's CPU provider has no kernel for the ConvInteger nodes that
        # quantizing the conv feature extractor would produce.
        quantize_dynamic(str(fp32), str(target), weight_type=QuantType.QInt8,
                         op_types_to_quantize=["MatMul"],
                         use_external_data_format=False)
    print(f"int8 model: {target.stat().st_size / 1e6:.0f} MB")

    # vocab.json: token -> id (espeak IPA labels incl. <pad>/<s>/</s>/<unk>)
    vp = hf_hub_download(args.model, "vocab.json")
    vocab = json.loads(Path(vp).read_text(encoding="utf-8"))
    vocab_path.write_text(json.dumps(vocab, ensure_ascii=False, indent=0), encoding="utf-8")
    print(f"vocab: {len(vocab)} tokens -> {vocab_path}")

    # verify: ORT on 1 s of noise
    sess = ort.InferenceSession(str(target), providers=["CPUExecutionProvider"])
    x = np.random.randn(1, 16000).astype(np.float32)
    logits = sess.run(["logits"], {"input_values": x})[0]
    print(f"verify: logits shape {logits.shape}")
    if logits.ndim != 3 or logits.shape[0] != 1 or logits.shape[2] < len(vocab) - 8:
        print("verify FAILED: unexpected output shape")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
