import torch
from fastapi import FastAPI
from pydantic import BaseModel
from transformers import AutoTokenizer, AutoModelForSequenceClassification

MODEL_DIR = ".././models/distilbert-violent/final"   # the folder you passed to save_pretrained()

tok = AutoTokenizer.from_pretrained(MODEL_DIR)
model = AutoModelForSequenceClassification.from_pretrained(MODEL_DIR).eval()
cfg = model.config
id2label = {int(k): str(v) for k, v in cfg.id2label.items()}
print("Model labels:", id2label, "| problem_type:", cfg.problem_type)

# Which output index means "toxic"? Match by name, else fall back to the last index.
TOXIC_NAMES = {"toxic", "toxicity", "label_1", "1", "offensive", "hate", "abusive"}
toxic_idx = next((i for i, l in id2label.items() if l.lower() in TOXIC_NAMES), cfg.num_labels - 1)

# How much each label counts toward the final score (0 to 1).
# "toxic" is discounted because the model is overfit on it.
WEIGHTS = {
    "threat":        1.00,
    "severe_toxic":  0.80,
    "identity_hate": 0.80,
    "insult":        0.70,
    "obscene":       0.60,
    "toxic":         0.3,
}

MAX_LEN = min(tok.model_max_length, 512)
CHUNK = MAX_LEN - 2                 # leave room for special tokens
STRIDE = CHUNK // 2

app = FastAPI()

class In(BaseModel):
    text: str

def score_ids(ids: list[int]) -> list[float]:
    """Per-label probabilities for one chunk."""
    input_ids = torch.tensor([[tok.cls_token_id] + ids + [tok.sep_token_id]])
    mask = torch.ones_like(input_ids)
    with torch.no_grad():
        logits = model(input_ids=input_ids, attention_mask=mask).logits[0]
    return torch.sigmoid(logits).tolist()   # multi-label, so sigmoid per label

@app.post("/score")
def score(body: In):
    ids = tok(body.text, add_special_tokens=False)["input_ids"]
    if not ids:
        return {"score": 0.0, "labels": {}, "weighted": {}, "driver": None, "chunks": 0}
    windows = [ids[i:i + CHUNK] for i in range(0, max(len(ids) - STRIDE, 1), STRIDE)]
    per_chunk = [score_ids(w) for w in windows]

    # Raw: highest probability per label across chunks
    labels = {id2label[i]: max(c[i] for c in per_chunk) for i in range(len(id2label))}
    # Weighted: discount each label, unknown labels count fully
    weighted = {name: p * WEIGHTS.get(name, 1.0) for name, p in labels.items()}
    driver = max(weighted, key=weighted.get)

    return {
        "score": weighted[driver],
        "driver": driver,
        "labels": labels,
        "weighted": weighted,
        "chunks": len(per_chunk),
    }

@app.get("/health")
def health():
    return {"labels": id2label, "toxic_index": toxic_idx, "problem_type": cfg.problem_type}
