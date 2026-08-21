#!/usr/bin/env python3
"""
train_sign_classifier_tf.py — TensorFlow Keras 训练 + INT8 量化

产物：
  - sign_classifier_keras.h5        (Keras 模型)
  - sign_classifier_int8.tflite     (INT8 量化，嵌入固件)
  - sign_classifier_int8.bin        (二进制权重，自定义格式)
  - sign_classifier_weights.h       (C 头文件)
  - vocab.txt                       (类别标签)

用法：
  python3 tools/train_sign_classifier_tf.py --vocab_size 50 --epochs 50
"""

import argparse
import os
import struct
import sys

import numpy as np
import tensorflow as tf

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

NUM_LANDMARKS = 21
LANDMARK_DIM  = 3
WINDOW_FRAMES = 32
FEATURE_DIM   = NUM_LANDMARKS * LANDMARK_DIM  # 63 per frame
INPUT_DIM     = WINDOW_FRAMES * FEATURE_DIM    # 2016

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


def generate_synthetic_data(vocab, samples_per_class=100):
    """Generate synthetic landmark data with class-specific patterns."""

    n_classes = len(vocab)
    n_samples = n_classes * samples_per_class

    X = np.random.randn(n_samples, WINDOW_FRAMES, FEATURE_DIM).astype(np.float32)
    y = np.repeat(np.arange(n_classes), samples_per_class)

    for c in range(n_classes):
        mask = y == c
        angle = c * 2 * np.pi / n_classes
        pattern = np.sin(
            np.linspace(0, angle, INPUT_DIM).reshape(1, WINDOW_FRAMES, FEATURE_DIM)
        )
        X[mask] += pattern * 2.0

    # Normalize to [0, 1]

    X = (X - X.min()) / (X.max() - X.min() + 1e-8)
    return X, y


def build_keras_model(n_classes):
    """Build a lightweight Keras MLP classifier.

    Architecture optimized for ESP32-P4 INT8 inference:
      Input(2016) → Dense(128, ReLU) → Dense(64, ReLU) → Dense(n_classes, Softmax)
    """

    model = tf.keras.Sequential([
        tf.keras.layers.Input(shape=(INPUT_DIM,)),
        tf.keras.layers.Dense(128, activation="relu", name="hidden1"),
        tf.keras.layers.Dropout(0.3),
        tf.keras.layers.Dense(64, activation="relu", name="hidden2"),
        tf.keras.layers.Dropout(0.2),
        tf.keras.layers.Dense(n_classes, activation="softmax", name="output"),
    ])

    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=0.001),
        loss="sparse_categorical_crossentropy",
        metrics=["accuracy"],
    )

    return model


def quantize_model_to_int8(model, X_cal, output_path):
    """Convert Keras model to INT8 quantized TFLite."""

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]

    def representative_dataset():
        indices = np.random.choice(len(X_cal), min(200, len(X_cal)), replace=False)
        for i in indices:
            yield [X_cal[i:i + 1].astype(np.float32)]

    converter.representative_dataset = representative_dataset
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type  = tf.int8
    converter.inference_output_type = tf.int8

    tflite_model = converter.convert()

    with open(output_path, "wb") as f:
        f.write(tflite_model)

    print(f"INT8 TFLite: {output_path} ({len(tflite_model):,} bytes)")
    return tflite_model


def export_c_header(model, output_path):
    """Export model weights as a C header for firmware embedding."""

    weights = {}
    for layer in model.layers:
        w = layer.get_weights()
        if w:
            weights[layer.name] = w

    with open(output_path, "w") as f:
        f.write("/* Auto-generated sign classifier weights */\n\n")
        f.write(f"#define SIGN_CLS_INPUT_DIM  {INPUT_DIM}\n")
        f.write(f"#define SIGN_CLS_HIDDEN1    128\n")
        f.write(f"#define SIGN_CLS_HIDDEN2    64\n")
        f.write(f"#define SIGN_CLS_OUTPUT_DIM {len(model.layers[-1].get_weights()[1])}\n\n")

        # Layer 1: Dense(128)
        W1, b1 = weights["hidden1"]
        f.write("/* Layer 1: input → hidden1 (128, ReLU) */\n")
        f.write(f"static const float g_cls_w1[{INPUT_DIM}][128] = {{\n")
        for row in W1:
            f.write("  {" + ",".join(f"{v:.6f}" for v in row) + "},\n")
        f.write("};\n\n")
        f.write(f"static const float g_cls_b1[128] = {{")
        f.write(",".join(f"{v:.6f}" for v in b1))
        f.write("};\n\n")

        # Layer 2: Dense(64)
        W2, b2 = weights["hidden2"]
        f.write("/* Layer 2: hidden1 → hidden2 (64, ReLU) */\n")
        f.write(f"static const float g_cls_w2[128][64] = {{\n")
        for row in W2:
            f.write("  {" + ",".join(f"{v:.6f}" for v in row) + "},\n")
        f.write("};\n\n")
        f.write(f"static const float g_cls_b2[64] = {{")
        f.write(",".join(f"{v:.6f}" for v in b2))
        f.write("};\n\n")

        # Layer 3: Dense(n_classes)
        W3, b3 = weights["output"]
        n_out = len(b3)
        f.write(f"/* Layer 3: hidden2 → output ({n_out}, Softmax) */\n")
        f.write(f"static const float g_cls_w3[64][{n_out}] = {{\n")
        for row in W3:
            f.write("  {" + ",".join(f"{v:.6f}" for v in row) + "},\n")
        f.write("};\n\n")
        f.write(f"static const float g_cls_b3[{n_out}] = {{")
        f.write(",".join(f"{v:.6f}" for v in b3))
        f.write("};\n")

    print(f"C header: {output_path}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--vocab_size", type=int, default=50)
    parser.add_argument("--epochs", type=int, default=50)
    parser.add_argument("--output_dir", default="./models_trained")
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)
    vocab = DEFAULT_VOCAB[:args.vocab_size]
    n_classes = len(vocab)

    print(f"=== Sign Language Classifier Training ===")
    print(f"Vocabulary: {n_classes} classes")
    print(f"Input dim:  {INPUT_DIM} ({WINDOW_FRAMES} frames x {FEATURE_DIM} features)")

    # Save vocab

    vocab_path = os.path.join(args.output_dir, "vocab.txt")
    with open(vocab_path, "w") as f:
        for i, word in enumerate(vocab):
            f.write(f"{i}\t{word}\n")

    # Generate training data

    print("\nGenerating training data...")
    X, y = generate_synthetic_data(vocab, samples_per_class=100)
    X_flat = X.reshape(-1, INPUT_DIM)

    from sklearn.model_selection import train_test_split
    X_train, X_val, y_train, y_val = train_test_split(
        X_flat, y, test_size=0.2, random_state=42, stratify=y
    )
    print(f"Train: {X_train.shape[0]}, Val: {X_val.shape[0]}")

    # Build and train model

    print("\nBuilding Keras model...")
    model = build_keras_model(n_classes)
    model.summary()

    print("\nTraining...")
    model.fit(
        X_train, y_train,
        validation_data=(X_val, y_val),
        epochs=args.epochs,
        batch_size=64,
        callbacks=[
            tf.keras.callbacks.EarlyStopping(
                patience=10, restore_best_weights=True
            ),
            tf.keras.callbacks.ReduceLROnPlateau(
                factor=0.5, patience=5
            ),
        ],
    )

    val_loss, val_acc = model.evaluate(X_val, y_val, verbose=0)
    print(f"\nFinal val accuracy: {val_acc:.4f}")

    # Save Keras model

    h5_path = os.path.join(args.output_dir, "sign_classifier_keras.h5")
    model.save(h5_path)
    print(f"Keras model: {h5_path}")

    # INT8 quantization

    print("\nQuantizing to INT8...")
    int8_path = os.path.join(args.output_dir, "sign_classifier_int8.tflite")
    tflite_data = quantize_model_to_int8(model, X_train, int8_path)

    # Export C header

    c_path = os.path.join(args.output_dir, "sign_classifier_weights.h")
    export_c_header(model, c_path)

    # Export binary weights (custom format)

    bin_path = os.path.join(args.output_dir, "sign_classifier_int8.bin")

    # Symmetric INT8 quantization of weights

    def quantize_symmetric(W):
        scale = np.max(np.abs(W)) / 127.0
        W_q = np.clip(np.round(W / scale), -128, 127).astype(np.int8)
        return W_q, float(scale)

    W1, b1 = model.get_layer("hidden1").get_weights()
    W2, b2 = model.get_layer("hidden2").get_weights()
    W3, b3 = model.get_layer("output").get_weights()

    W1_q, s1 = quantize_symmetric(W1)
    W2_q, s2 = quantize_symmetric(W2)
    W3_q, s3 = quantize_symmetric(W3)

    with open(bin_path, "wb") as f:
        # Header
        f.write(struct.pack("<IIIIIII",
                            0x5349474E,  # "SIGN" magic
                            2,           # version
                            INPUT_DIM, 128, 64, n_classes, 3))
        # Layer 1
        f.write(struct.pack("<If", W1_q.size, s1))
        f.write(W1_q.tobytes())
        f.write(b1.astype(np.float32).tobytes())
        # Layer 2
        f.write(struct.pack("<If", W2_q.size, s2))
        f.write(W2_q.tobytes())
        f.write(b2.astype(np.float32).tobytes())
        # Layer 3
        f.write(struct.pack("<If", W3_q.size, s3))
        f.write(W3_q.tobytes())
        f.write(b3.astype(np.float32).tobytes())

    bin_size = os.path.getsize(bin_path)
    print(f"Binary weights: {bin_path} ({bin_size:,} bytes)")

    # Summary

    print("\n" + "=" * 60)
    print("Training complete!")
    print("=" * 60)
    print(f"\nGenerated files in {args.output_dir}/:")
    for fn in sorted(os.listdir(args.output_dir)):
        fp = os.path.join(args.output_dir, fn)
        sz = os.path.getsize(fp)
        print(f"  {fn:45s} {sz:>10,} bytes")
    print(f"\nNext steps:")
    print(f"  1. Copy sign_classifier_int8.bin to ROMFS /etc/models/")
    print(f"  2. Update signbridge_cls_mlp.c to load the binary weights")
    print(f"  3. Or embed sign_classifier_weights.h directly in firmware")


if __name__ == "__main__":
    main()
