import json, os, sys
import numpy as np
import onnx
from onnx import TensorProto as TP, helper, numpy_helper

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
T, V = 5, 6
vocab = {"<pad>": 0, "<s>": 1, "</s>": 2, "|": 3, "ə": 4, "θ": 5}
with open(os.path.join(out, "fake_vocab.json"), "w", encoding="utf-8") as f:
    f.write(json.dumps(vocab, ensure_ascii=False if False else True))
logits = np.arange(T * V, dtype=np.float32).reshape(1, T, V) * 0.3
nodes = [
    helper.make_node("ReduceMean", ["input_values"], ["m"], axes=[1], keepdims=1),
    helper.make_node("Mul", ["m", "zero"], ["z"]),
    helper.make_node("Add", ["z", "C"], ["logits"]),
]
g = helper.make_graph(nodes, "fake",
    [helper.make_tensor_value_info("input_values", TP.FLOAT, [1, "N"])],
    [helper.make_tensor_value_info("logits", TP.FLOAT, [1, T, V])],
    [numpy_helper.from_array(logits, "C"), numpy_helper.from_array(np.zeros((1, 1), np.float32), "zero")])
m = helper.make_model(g, opset_imports=[helper.make_opsetid("", 13)])
m.ir_version = 8
onnx.checker.check_model(m)
onnx.save(m, os.path.join(out, "fake_w2v.onnx"))
