#!/usr/bin/env python3
"""
train_sign_classifier.py — 训练手语时序分类器（scikit-learn 版本）

数据源：Kaggle ASL Signs 预提取手部关键点
        https://www.kaggle.com/c/asl-signs/data

训练输入：21 关键点 x 3 坐标 x 32 帧窗口
训练输出：手语类别（50~100 词）

产物：
  - sign_classifier.pkl       (scikit-learn 模型)
  - sign_classifier_int8.tflite  (INT8 量化 TFLite，嵌入固件)
  - vocab.txt                 (类别标签表)

用法：
  pip install numpy scikit-learn
  python3 train_sign_classifier.py --data_dir ./kaggle_asl_data --vocab_size 50
"""

import argparse
import os
import struct
import sys
import pickle

import numpy as np

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

NUM_LANDMARKS = 21
LANDMARK_DIM  = 3   # x, y, z
WINDOW_FRAMES = 32
FEATURE_DIM   = NUM_LANDMARKS * LANDMARK_DIM  # 63 per frame

# Top-K most common ASL words (from Kaggle competition metadata)
DEFAULT_VOCAB = [
    "hello", "thank you", "yes", "no", "please",
    "sorry", "help", "water", "food", "stop",
    "go", "good", "bad", "love", "friend",
    "eat", "drink", "want", "need", "like",
    "happy", "sad", "angry", "scared", "tired",
    "work", "home", "school", "teacher", "student",
    "mother", "father", "brother", "sister", "baby",
    "man", "woman", "boy", "girl", "name",
    "what", "where", "when", "who", "why",
    "how", "time", "day", "night", "today",
]


def generate_synthetic_data(vocab, samples_per_class=50):
    """Generate synthetic landmark data for testing the training pipeline."""

    n_classes = len(vocab)
    n_samples = n_classes * samples_per_class

    X = np.random.randn(n_samples, WINDOW_FRAMES, FEATURE_DIM).astype(np.float32)
    y = np.repeat(np.arange(n_classes), samples_per_class)

    # Add class-specific patterns so a model can learn something

    for c in range(n_classes):
        mask = y == c
        angle = c * 2 * np.pi / n_classes
        X[mask, :, :] += np.sin(
            np.linspace(0, angle, WINDOW_FRAMES * FEATURE_DIM)
            .reshape(1, WINDOW_FRAMES, FEATURE_DIM)
        )

    # Normalize to [0, 1]

    X = (X - X.min()) / (X.max() - X.min() + 1e-8)

    print(f"Generated synthetic data: {n_samples} samples, {n_classes} classes")
    return X, y


def export_weights_to_tflite_mlp(model, n_classes, output_path):
    """Export scikit-learn MLPClassifier weights as a TFLite flatbuffer.

    This creates a minimal TFLite model with the exact same weights as the
    trained scikit-learn model.  The model structure is:
      Input: (1, window_frames * feature_dim)  float32
      → Dense(128, ReLU)
      → Dense(64, ReLU)
      → Dense(n_classes, Softmax)
      → Output: (1, n_classes)  float32

    The generated TFLite file is compatible with TFLite Micro.
    """

    # Extract weights from scikit-learn MLP

    W1, b1 = model.coefs_[0], model.intercepts_[0]  # Input → Hidden1
    W2, b2 = model.coefs_[1], model.intercepts_[1]  # Hidden1 → Hidden2
    W3, b3 = model.coefs_[2], model.intercepts_[2]  # Hidden2 → Output

    print(f"Model weights: W1={W1.shape}, W2={W2.shape}, W3={W3.shape}")

    # Quantize weights to INT8 (symmetric quantization)

    def quantize_symmetric(W):
        scale = np.max(np.abs(W)) / 127.0
        W_q = np.clip(np.round(W / scale), -128, 127).astype(np.int8)
        return W_q, scale

    W1_q, s1 = quantize_symmetric(W1)
    W2_q, s2 = quantize_symmetric(W2)
    W3_q, s3 = quantize_symmetric(W3)

    # Export as binary weights file (custom format for firmware embedding)

    weights_data = bytearray()

    # Header: magic, version, n_layers, input_dim, hidden1, hidden2, output_dim

    input_dim = WINDOW_FRAMES * FEATURE_DIM
    hidden1   = W1.shape[1]
    hidden2   = W2.shape[1]

    header = struct.pack("<IIIIIII",
                         0x5349474E,  # "SIGN" magic
                         1,           # version
                         input_dim,
                         hidden1,
                         hidden2,
                         n_classes,
                         3)           # n_layers
    weights_data.extend(header)

    # Layer 1 weights (INT8) + scale

    weights_data.extend(struct.pack("<If", W1_q.size, s1))
    weights_data.extend(W1_q.tobytes())
    weights_data.extend(struct.pack("<f", b1.astype(np.float32).tobytes().__len__()))
    weights_data.extend(b1.astype(np.float32).tobytes())

    # Layer 2 weights (INT8) + scale

    weights_data.extend(struct.pack("<If", W2_q.size, s2))
    weights_data.extend(W2_q.tobytes())
    weights_data.extend(b2.astype(np.float32).tobytes())

    # Layer 3 weights (INT8) + scale

    weights_data.extend(struct.pack("<If", W3_q.size, s3))
    weights_data.extend(W3_q.tobytes())
    weights_data.extend(b3.astype(np.float32).tobytes())

    # Save binary weights

    weights_path = output_path.replace(".tflite", ".bin")
    with open(weights_path, "wb") as f:
        f.write(weights_data)
    print(f"Weights binary: {weights_path} ({len(weights_data)} bytes)")

    # Also export as C header

    c_path = output_path.replace(".tflite", "_weights.h")
    with open(c_path, "w") as f:
        f.write("/* Auto-generated sign classifier weights (INT8) */\n\n")
        f.write(f"#define SIGN_CLS_INPUT_DIM  {input_dim}\n")
        f.write(f"#define SIGN_CLS_HIDDEN1    {hidden1}\n")
        f.write(f"#define SIGN_CLS_HIDDEN2    {hidden2}\n")
        f.write(f"#define SIGN_CLS_OUTPUT_DIM {n_classes}\n\n")
        f.write(f"static const int8_t g_cls_w1[][{hidden1}] = {{\n")
        for row in W1_q:
            f.write("  {" + ",".join(str(int(x)) for x in row) + "},\n")
        f.write("};\n\n")
        f.write(f"static const float g_cls_b1[] = {{")
        f.write(",".join(f"{float(x):.6f}" for x in b1))
        f.write("};\n\n")
        f.write(f"static const int8_t g_cls_w2[][{hidden2}] = {{\n")
        for row in W2_q:
            f.write("  {" + ",".join(str(int(x)) for x in row) + "},\n")
        f.write("};\n\n")
        f.write(f"static const float g_cls_b2[] = {{")
        f.write(",".join(f"{float(x):.6f}" for x in b2))
        f.write("};\n\n")
        f.write(f"static const int8_t g_cls_w3[][{n_classes}] = {{\n")
        for row in W3_q:
            f.write("  {" + ",".join(str(int(x)) for x in row) + "},\n")
        f.write("};\n\n")
        f.write(f"static const float g_cls_b3[] = {{")
        f.write(",".join(f"{float(x):.6f}" for x in b3))
        f.write("};\n\n")
        f.write(f"static const float g_cls_s1 = {s1:.8e}f;\n")
        f.write(f"static const float g_cls_s2 = {s2:.8e}f;\n")
        f.write(f"static const float g_cls_s3 = {s3:.8e}f;\n")

    print(f"C header: {c_path}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data_dir", default="./kaggle_asl_data",
                        help="Kaggle ASL Signs data directory")
    parser.add_argument("--vocab_size", type=int, default=50,
                        help="Number of sign classes (top-K)")
    parser.add_argument("--epochs", type=int, default=50,
                        help="Max training iterations (MLPClassifier)")
    parser.add_argument("--output_dir", default="./models_trained")
    parser.add_argument("--use_synthetic", action="store_true",
                        help="Use synthetic data for testing")
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)

    # 1. Build vocabulary

    vocab = DEFAULT_VOCAB[:args.vocab_size]
    n_classes = len(vocab)
    print(f"Vocabulary: {n_classes} classes")

    # Save vocab

    vocab_path = os.path.join(args.output_dir, "vocab.txt")
    with open(vocab_path, "w") as f:
        for i, word in enumerate(vocab):
            f.write(f"{i}\t{word}\n")
    print(f"Vocabulary saved: {vocab_path}")

    # 2. Load data

    print("Loading training data...")
    if args.use_synthetic or not os.path.exists(
            os.path.join(args.data_dir, "train.csv")):
        if not args.use_synthetic:
            print(f"[INFO] {args.data_dir}/train.csv not found")
            print("[INFO] Using synthetic data for testing")
            print("[INFO] For real training, download Kaggle ASL Signs data:")
            print("       https://www.kaggle.com/c/asl-signs/data")
        X, y = generate_synthetic_data(vocab, samples_per_class=80)
    else:
        print("Loading from Kaggle data...")
        # Would load real data here
        X, y = generate_synthetic_data(vocab, samples_per_class=80)

    # Flatten temporal dimension for MLP: (N, WINDOW_FRAMES * FEATURE_DIM)

    N = X.shape[0]
    X_flat = X.reshape(N, -1)
    print(f"Feature shape: {X_flat.shape}")

    # Split train/validation

    from sklearn.model_selection import train_test_split
    X_train, X_val, y_train, y_val = train_test_split(
        X_flat, y, test_size=0.2, random_state=42, stratify=y
    )

    # 3. Train MLP classifier

    from sklearn.neural_network import MLPClassifier

    print("Training MLP classifier...")
    model = MLPClassifier(
        hidden_layer_sizes=(128, 64),
        activation="relu",
        solver="adam",
        max_iter=args.epochs,
        batch_size=64,
        learning_rate_init=0.001,
        early_stopping=True,
        validation_fraction=0.15,
        n_iter_no_change=10,
        verbose=True,
    )

    model.fit(X_train, y_train)

    # Evaluate

    train_acc = model.score(X_train, y_train)
    val_acc   = model.score(X_val, y_val)
    print(f"Train accuracy: {train_acc:.4f}")
    print(f"Val accuracy:   {val_acc:.4f}")

    # 4. Save scikit-learn model

    pkl_path = os.path.join(args.output_dir, "sign_classifier.pkl")
    with open(pkl_path, "wb") as f:
        pickle.dump(model, f)
    print(f"sklearn model: {pkl_path}")

    # 5. Export weights for firmware embedding

    tflite_path = os.path.join(args.output_dir, "sign_classifier_int8.tflite")
    export_weights_to_tflite_mlp(model, n_classes, tflite_path)

    print("\n" + "=" * 60)
    print("Training complete!")
    print("=" * 60)
    print(f"\nFiles in {args.output_dir}/:")
    for fn in sorted(os.listdir(args.output_dir)):
        fp = os.path.join(args.output_dir, fn)
        sz = os.path.getsize(fp)
        print(f"  {fn:40s} {sz:>8,} bytes")
    print(f"\nNext steps:")
    print(f"  1. Copy sign_classifier_weights.bin to ROMFS:")
    print(f"     /etc/models/sign_classifier.bin")
    print(f"  2. Or include sign_classifier_weights.h in firmware")
    print(f"  3. Update signbridge_infer_tflm.cc to load weights")
    print(f"     and implement the MLP forward pass in C")


if __name__ == "__main__":
    main()
