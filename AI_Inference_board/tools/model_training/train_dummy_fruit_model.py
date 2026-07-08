#!/usr/bin/env python3
"""Train a tiny dummy fruit classifier for e-AI integration testing.

This script intentionally uses synthetic image patterns instead of real fruit
photos. Its purpose is to produce a small uint8 TFLite model that keeps the
same input/output shape as the later real fruit classifier.
"""

from __future__ import annotations

from pathlib import Path

import numpy as np
import tensorflow as tf


IMG_SIZE = 96
NUM_CLASSES = 4
SAMPLES_PER_CLASS = 80
EPOCHS = 8

OUTPUT_DIR = Path(__file__).resolve().parent / "models"
H5_MODEL = OUTPUT_DIR / "fruit_dummy_cnn.h5"
TFLITE_MODEL = OUTPUT_DIR / "fruit_dummy_uint8.tflite"


def build_synthetic_dataset() -> tuple[np.ndarray, np.ndarray]:
    """Build four simple grayscale classes: bright, middle, dark, and blank."""

    rng = np.random.default_rng(seed=1)
    images: list[np.ndarray] = []
    labels: list[int] = []

    for label in range(NUM_CLASSES):
        for _ in range(SAMPLES_PER_CLASS):
            if label == 0:
                base = make_center_blob(rng, center_value=220, background_value=30)
            elif label == 1:
                base = make_vertical_band(rng, band_value=170, background_value=35)
            elif label == 2:
                base = make_dark_blob(rng, center_value=75, background_value=20)
            else:
                base = rng.normal(loc=18, scale=4, size=(IMG_SIZE, IMG_SIZE))

            image = np.clip(base, 0, 255).astype(np.uint8)
            images.append(image[..., np.newaxis])
            labels.append(label)

    x = np.stack(images).astype(np.float32) / 255.0
    y = np.asarray(labels, dtype=np.int64)

    order = rng.permutation(len(y))
    return x[order], y[order]


def make_center_blob(
    rng: np.random.Generator,
    center_value: float,
    background_value: float,
) -> np.ndarray:
    y, x = np.ogrid[:IMG_SIZE, :IMG_SIZE]
    cx = IMG_SIZE / 2 + rng.integers(-8, 9)
    cy = IMG_SIZE / 2 + rng.integers(-8, 9)
    radius = rng.integers(18, 30)
    mask = (x - cx) ** 2 + (y - cy) ** 2 <= radius**2
    image = rng.normal(loc=background_value, scale=6, size=(IMG_SIZE, IMG_SIZE))
    image[mask] = rng.normal(loc=center_value, scale=10, size=np.count_nonzero(mask))
    return image


def make_vertical_band(
    rng: np.random.Generator,
    band_value: float,
    background_value: float,
) -> np.ndarray:
    image = rng.normal(loc=background_value, scale=6, size=(IMG_SIZE, IMG_SIZE))
    center = IMG_SIZE // 2 + rng.integers(-12, 13)
    width = rng.integers(14, 24)
    start = max(0, center - width)
    end = min(IMG_SIZE, center + width)
    image[:, start:end] = rng.normal(loc=band_value, scale=10, size=(IMG_SIZE, end - start))
    return image


def make_dark_blob(
    rng: np.random.Generator,
    center_value: float,
    background_value: float,
) -> np.ndarray:
    image = make_center_blob(rng, center_value=center_value, background_value=background_value)
    image += rng.normal(loc=0, scale=5, size=(IMG_SIZE, IMG_SIZE))
    return image


def build_model() -> tf.keras.Model:
    model = tf.keras.Sequential(
        [
            tf.keras.layers.Input(shape=(IMG_SIZE, IMG_SIZE, 1)),
            tf.keras.layers.Flatten(),
            tf.keras.layers.Dense(NUM_CLASSES, activation="softmax"),
        ]
    )

    model.compile(
        optimizer="adam",
        loss="sparse_categorical_crossentropy",
        metrics=["accuracy"],
    )
    return model


def representative_dataset():
    x, _ = build_synthetic_dataset()
    for i in range(min(100, len(x))):
        yield [x[i : i + 1].astype(np.float32)]


def convert_to_uint8_tflite(model: tf.keras.Model) -> bytes:
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.uint8
    converter.inference_output_type = tf.uint8
    return converter.convert()


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    x, y = build_synthetic_dataset()
    split = int(len(y) * 0.8)
    x_train, x_val = x[:split], x[split:]
    y_train, y_val = y[:split], y[split:]

    model = build_model()
    model.fit(
        x_train,
        y_train,
        validation_data=(x_val, y_val),
        epochs=EPOCHS,
        batch_size=16,
        verbose=2,
    )

    model.save(H5_MODEL)
    tflite_bytes = convert_to_uint8_tflite(model)
    TFLITE_MODEL.write_bytes(tflite_bytes)

    print(f"OK: wrote {H5_MODEL}")
    print(f"OK: wrote {TFLITE_MODEL}")
    print("Class order: 0=A, 1=B, 2=C, 3=UNKNOWN")


if __name__ == "__main__":
    main()
