import torch
import torch.nn as nn
import torch.optim as optim
import spacy
from torch.nn.utils.rnn import pad_sequence
from torch.utils.data import TensorDataset, DataLoader, random_split
from torchtext import data
from torchtext.datasets import IMDB

device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
MAX_LEN = 250

# ---------- Tokenizer (must be set BEFORE IMDB.splits) ----------
nlp = spacy.blank("en")  # tokenizer only, no model download needed

def tokenize(text):
    return [t.text.lower() for t in nlp.tokenizer(text)]

TEXT = data.Field(tokenize=tokenize, include_lengths=True)
LABEL = data.LabelField(dtype=torch.float)

raw_train, raw_test = IMDB.splits(TEXT, LABEL)

TEXT.build_vocab(raw_train, max_size=10000)
LABEL.build_vocab(raw_train)

PAD_IDX = TEXT.vocab.stoi[TEXT.pad_token]
vocab_size = len(TEXT.vocab)

# ---------- Encode + pad ----------
def encode(examples):
    seqs = [
        torch.tensor([TEXT.vocab.stoi[w] for w in ex.text[:MAX_LEN]], dtype=torch.long)
        for ex in examples
    ]
    x = pad_sequence(seqs, batch_first=True, padding_value=PAD_IDX)   # [N, <=500]
    y = torch.tensor([1.0 if ex.label == 'pos' else 0.0 for ex in examples])
    assert x.size(0) == y.size(0)
    return TensorDataset(x, y)

full_train = encode(raw_train.examples)
test_set = encode(raw_test.examples)
print("Train tensor:", full_train.tensors[0].shape, "Labels:", full_train.tensors[1].shape)

train_size = int(0.8 * len(full_train))
train_set, valid_set = random_split(full_train, [train_size, len(full_train) - train_size])

# ---------- DataLoaders ----------
def collate_batch(batch):
    texts, labels = zip(*batch)
    texts = torch.stack(texts)
    labels = torch.stack(labels)
    lengths = (texts != PAD_IDX).sum(dim=1).clamp(min=1)
    return texts, lengths, labels

train_iterator = DataLoader(train_set, batch_size=64, shuffle=True, collate_fn=collate_batch)
valid_iterator = DataLoader(valid_set, batch_size=64, collate_fn=collate_batch)
test_iterator = DataLoader(test_set, batch_size=64, collate_fn=collate_batch)

# ---------- Model ----------
class SentimentModel(nn.Module):
    def __init__(self, vocab_size, embedding_dim, hidden_dim, output_dim, pad_idx):
        super().__init__()
        self.embedding = nn.Embedding(vocab_size, embedding_dim, padding_idx=pad_idx)
        self.rnn = nn.LSTM(embedding_dim, hidden_dim, batch_first=True)
        self.fc = nn.Linear(hidden_dim, output_dim)

    def forward(self, text, text_lengths):
        embedded = self.embedding(text)
        packed = nn.utils.rnn.pack_padded_sequence(
            embedded, text_lengths.to('cpu'), batch_first=True, enforce_sorted=False
        )
        _, (hidden, _) = self.rnn(packed)
        return self.fc(hidden[-1])

model = SentimentModel(vocab_size, 80, 128, 1, PAD_IDX).to(device)
optimizer = optim.Adam(model.parameters())
criterion = nn.BCEWithLogitsLoss()

# ---------- Train / evaluate ----------
def train_model(model, iterator, optimizer, criterion, epochs=3):
    for epoch in range(epochs):
        model.train()
        epoch_loss = 0
        for text, lengths, labels in iterator:
            text, labels = text.to(device), labels.to(device)
            optimizer.zero_grad()
            preds = model(text, lengths).squeeze(1)
            loss = criterion(preds, labels)
            loss.backward()
            optimizer.step()
            epoch_loss += loss.item()
        val_acc = evaluate_model(model, valid_iterator)
        print(f"Epoch {epoch+1}/{epochs}  Loss: {epoch_loss/len(iterator):.4f}  Val Acc: {val_acc*100:.2f}%")

def evaluate_model(model, iterator):
    model.eval()
    correct = total = 0
    with torch.no_grad():
        for text, lengths, labels in iterator:
            text, labels = text.to(device), labels.to(device)
            preds = torch.round(torch.sigmoid(model(text, lengths).squeeze(1)))
            correct += (preds == labels).sum().item()
            total += labels.size(0)
    return correct / total

train_model(model, train_iterator, optimizer, criterion)
print(f"Test Accuracy: {evaluate_model(model, test_iterator)*100:.2f}%")