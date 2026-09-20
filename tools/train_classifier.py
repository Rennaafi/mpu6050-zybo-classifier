#!/usr/bin/env python3
"""
Stage 4: train a small MLP on the Stage 3 feature CSV and validate accuracy
before touching HDL.

Built as a Keras model (not scikit-learn) on purpose: Stage 5 converts this
same model with hls4ml, which has first-class support for Keras but not for
scikit-learn's MLPClassifier.

Usage:
    python train_classifier.py
    (reads ../data/training_data.csv, writes ../models/)
"""
import json
import os

import numpy as np
import pandas as pd
import tensorflow as tf
from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report, confusion_matrix
from sklearn.preprocessing import StandardScaler

HERE = os.path.dirname(__file__)
DATA_PATH = os.path.join(HERE, "..", "data", "training_data.csv")
MODEL_DIR = os.path.join(HERE, "..", "models")
CLASSES = ["stationary", "tilt", "freefall", "impact", "shake"]


def main():
    if not os.path.exists(DATA_PATH):
        raise SystemExit(
            f"No training data at {os.path.abspath(DATA_PATH)} yet — "
            "run collect_training_data.py first (Stage 3)."
        )

    df = pd.read_csv(DATA_PATH)
    missing = set(df["label"]) - set(CLASSES)
    if missing:
        raise SystemExit(f"Unexpected label(s) in CSV: {missing}")

    X = df.drop(columns=["label"]).to_numpy(dtype=np.float32)
    y = np.array([CLASSES.index(label) for label in df["label"]])

    counts = {c: int((y == i).sum()) for i, c in enumerate(CLASSES)}
    print("Samples per class:", counts)
    if min(counts.values()) < 10:
        print("Warning: some classes have very few windows — collect more "
              "before trusting this accuracy number.")

    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.2, stratify=y, random_state=0
    )

    scaler = StandardScaler().fit(X_train)
    X_train = scaler.transform(X_train)
    X_test = scaler.transform(X_test)

    model = tf.keras.Sequential([
        tf.keras.layers.Input(shape=(X.shape[1],)),
        tf.keras.layers.Dense(16, activation="relu"),
        tf.keras.layers.Dense(len(CLASSES), activation="softmax"),
    ])
    model.compile(optimizer="adam", loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    model.summary()

    n_params = model.count_params()
    print(f"Total parameters: {n_params}" + (" (over the ~1k Zynq-7010 budget!)" if n_params > 1000 else ""))

    model.fit(
        X_train, y_train,
        validation_data=(X_test, y_test),
        epochs=60, batch_size=8, verbose=2,
        callbacks=[tf.keras.callbacks.EarlyStopping(patience=10, restore_best_weights=True)],
    )

    y_pred = model.predict(X_test).argmax(axis=1)
    print("\nClassification report:")
    print(classification_report(y_test, y_pred, target_names=CLASSES, zero_division=0))
    print("Confusion matrix (rows=true, cols=predicted):")
    print(confusion_matrix(y_test, y_pred))

    os.makedirs(MODEL_DIR, exist_ok=True)
    model.save(os.path.join(MODEL_DIR, "motion_classifier.h5"))
    np.savez(os.path.join(MODEL_DIR, "scaler.npz"), mean=scaler.mean_, scale=scaler.scale_)
    with open(os.path.join(MODEL_DIR, "classes.json"), "w") as f:
        json.dump(CLASSES, f)
    print(f"\nSaved model + scaler + class list to {os.path.abspath(MODEL_DIR)}")


if __name__ == "__main__":
    main()
