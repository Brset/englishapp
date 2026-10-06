# Writes a tiny stand-in wav2vec2 phoneme model (models/phoneme/{model.onnx,vocab.json}) for plumbing tests:
# one conv with stride 320 -> one frame per 20 ms of 16 kHz audio, V classes. Not a real recognizer.
import json, os, sys
import numpy as np
import onnx
from onnx import TensorProto as TP, helper, numpy_helper

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
labels = ["<pad>", "<s>", "</s>", "|", "θ", "s", "ɪ", "ŋ", "k", "ə", "ð", "w", "ɛ", "r", "t", "ɔ", "n", "d"]
V = len(labels)
with open(os.path.join(out, "vocab.json"), "w", encoding="utf-8") as f:
    json.dump({l: i for i, l in enumerate(labels)}, f, ensure_ascii=True)
rng = np.random.RandomState(1)
w = (rng.randn(V, 1, 320) * 0.05).astype(np.float32)
b = (rng.randn(V) * 0.5).astype(np.float32)
nodes = [
    helper.make_node("Unsqueeze", ["input_values", "ax"], ["x3"]),
    helper.make_node("Conv", ["x3", "W", "B"], ["c"], strides=[320], kernel_shape=[320]),
    helper.make_node("Transpose", ["c"], ["logits"], perm=[0, 2, 1]),
]
g = helper.make_graph(nodes, "fake_w2v",
    [helper.make_tensor_value_info("input_values", TP.FLOAT, [1, "N"])],
    [helper.make_tensor_value_info("logits", TP.FLOAT, [1, "T", V])],
    [numpy_helper.from_array(np.array([1], np.int64), "ax"), numpy_helper.from_array(w, "W"), numpy_helper.from_array(b, "B")])
m = helper.make_model(g, opset_imports=[helper.make_opsetid("", 13)])
m.ir_version = 8
onnx.checker.check_model(m)
onnx.save(m, os.path.join(out, "model.onnx"))
