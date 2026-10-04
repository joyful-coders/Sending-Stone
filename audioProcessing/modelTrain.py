import numpy as np
import torch
from datasets import load_dataset
from sklearn.metrics import average_precision_score
from transformers import (AutoModelForSequenceClassification, AutoTokenizer,
                          TrainingArguments, Trainer, DataCollatorWithPadding)

LABELS = ["toxic", "severe_toxic", "obscene", "threat", "insult", "identity_hate"]
id2label = {i: l for i, l in enumerate(LABELS)}
label2id = {l: i for i, l in id2label.items()}

dataset = load_dataset("csv", data_files="./train.csv")["train"]

# Create the validation split BEFORE tokenizing
dataset = dataset.train_test_split(test_size=0.1, seed=42)

tokenizer = AutoTokenizer.from_pretrained("distilbert/distilbert-base-uncased")

def preprocess_function(examples):
    tok = tokenizer(examples["comment_text"], truncation=True, max_length=128)
    # Pack the six label columns into one float vector per row
    tok["labels"] = [
        [float(examples[l][i]) for l in LABELS]
        for i in range(len(examples["comment_text"]))
    ]
    return tok

tokenized_data = dataset.map(
    preprocess_function,
    batched=True,
    remove_columns=dataset["train"].column_names,  # drops text, id, and the raw label columns
)
data_collator = DataCollatorWithPadding(tokenizer=tokenizer)

# Class weights (softened with sqrt)
y = np.array(tokenized_data["train"]["labels"])
pos = y.sum(axis=0)
neg = len(y) - pos
pos_weight = torch.tensor(np.sqrt(neg / np.maximum(pos, 1)), dtype=torch.float32)

def compute_metrics(eval_pred):
    logits, labels = eval_pred
    probs = 1 / (1 + np.exp(-logits))
    out = {}
    for i, name in enumerate(LABELS):
        if labels[:, i].sum() > 0:
            out[f"ap_{name}"] = average_precision_score(labels[:, i], probs[:, i])
    out["pr_auc"] = float(np.mean(list(out.values())))
    return out

model = AutoModelForSequenceClassification.from_pretrained(
    "distilbert/distilbert-base-uncased",
    num_labels=len(LABELS),
    problem_type="multi_label_classification",
    id2label=id2label,
    label2id=label2id,
)

class WeightedTrainer(Trainer):
    def compute_loss(self, model, inputs, return_outputs=False, **kwargs):
        labels = inputs.pop("labels")
        outputs = model(**inputs)
        loss_fn = torch.nn.BCEWithLogitsLoss(
            pos_weight=pos_weight.to(outputs.logits.device))
        loss = loss_fn(outputs.logits, labels)
        return (loss, outputs) if return_outputs else loss

args = TrainingArguments(
    output_dir="distilbert-violent",
    learning_rate=3e-5,
    per_device_train_batch_size=32,
    per_device_eval_batch_size=64,
    num_train_epochs=2,
    weight_decay=0.01,
    warmup_steps=0.06,
    fp16=torch.cuda.is_available(),
    eval_strategy="epoch",
    save_strategy="epoch",
    load_best_model_at_end=True,
    metric_for_best_model="pr_auc",
    greater_is_better=True,
    report_to="none",
)

trainer = WeightedTrainer(
    model=model,
    args=args,
    train_dataset=tokenized_data["train"],
    eval_dataset=tokenized_data["test"],
    processing_class=tokenizer,
    data_collator=data_collator,
    compute_metrics=compute_metrics,
)

trainer.train()
trainer.save_model("distilbert-violent/final")
tokenizer.save_pretrained("distilbert-violent/final")