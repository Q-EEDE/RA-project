#!/usr/bin/env python3
"""Train a real fruit classifier from captured JPEG datasets.

The generated model keeps the board-side contract:
input  : 96 x 96 x 3 RGB image
output : 4 uint8 scores, class order A, B, C, UNKNOWN
"""

from __future__ import annotations

import argparse
import random
from pathlib import Path

import numpy as np
import tensorflow as tf


IMG_SIZE = 96
MODEL_INPUT_SHAPE = (IMG_SIZE, IMG_SIZE, 3)
CLASS_NAMES = ("A", "B", "C", "UNKNOWN")
ROI_EXTRACT_ENABLED = True
ROI_MIN_SEED_PIXELS = 120
ROI_MARGIN_PIXELS = 5
ROI_CENTER_WINDOW_PIXELS = 32
ROI_BACKGROUND_VALUE = 0.0
MODEL_ARCHITECTURE = "small_cnn"

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parents[2]
DEFAULT_DATASET_DIR = PROJECT_ROOT / "fruit_dataset_balanced"
DEFAULT_OUTPUT_DIR = PROJECT_ROOT / "trained_models"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Train the real fruit classifier.")
    parser.add_argument("--dataset", type=Path, default=DEFAULT_DATASET_DIR)
    parser.add_argument("--epochs", type=int, default=80)
    parser.add_argument("--batch-size", type=int, default=16)
    parser.add_argument("--val-ratio", type=float, default=0.2)
    parser.add_argument("--seed", type=int, default=20260704)
    parser.add_argument("--prefix", default="fruit_real")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument(
        "--size-weight-strength",
        type=float,
        default=0.0,
        help="0 disables size weighting. Try 0.3-0.5 for mild fruit-area weighting.",
    )
    parser.add_argument(
        "--unknown-ratio",
        type=float,
        default=1.0,
        help="UNKNOWN sample count relative to the largest A/B/C class.",
    )
    parser.add_argument(
        "--unknown-sample-weight",
        type=float,
        default=1.0,
        help="Training weight for synthetic UNKNOWN samples.",
    )
    return parser.parse_args()


def find_images(dataset_dir: Path) -> tuple[list[Path], list[int]]:
    image_paths: list[Path] = []
    labels: list[int] = []

    for label, class_name in enumerate(CLASS_NAMES[:3]):
        class_dir = dataset_dir / class_name
        files = sorted(class_dir.glob("*.jpg"))
        if not files:
            raise FileNotFoundError(f"No .jpg files found in {class_dir}")

        image_paths.extend(files)
        labels.extend([label] * len(files))

    return image_paths, labels


def estimate_fruit_area_ratio(image: tf.Tensor) -> float:
    """Estimate apparent fruit size from orange/yellow pixels in the image."""

    small = tf.image.resize(image, (IMG_SIZE, IMG_SIZE), method="nearest").numpy()
    r = small[..., 0]
    g = small[..., 1]
    b = small[..., 2]

    mask = (
        (r > 55.0)
        & (g > 40.0)
        & (r > b * 1.25)
        & (g > b * 1.05)
        & (r > g * 0.70)
        & (r < g * 2.20)
    )

    return float(np.count_nonzero(mask) / mask.size)


def apply_fruit_region_extract(image: np.ndarray) -> tuple[np.ndarray, bool]:
    """Suppress background while keeping apparent fruit size unchanged.

    This matches the board-side ROI path:
    color seed -> 4-neighbor connected component -> row spans with margin.
    """

    if not ROI_EXTRACT_ENABLED:
        return image, False

    r = image[..., 0]
    g = image[..., 1]
    b = image[..., 2]
    rg_sum = r + g
    brightness = rg_sum + b

    mask = (
        (brightness >= 90.0)
        & (b <= ((r + g) / 2.0))
        & ((r > (b + 10.0)) | (g > (b + 10.0)))
        & (rg_sum > ((2.0 * b) + 20.0))
    )

    labels = np.zeros((IMG_SIZE, IMG_SIZE), dtype=np.uint16)
    next_label = 1
    best_label = 0
    best_area = 0
    best_bbox = (IMG_SIZE, IMG_SIZE, 0, 0)

    def overlaps_center(bbox: tuple[int, int, int, int]) -> bool:
        min_x, min_y, max_x, max_y = bbox
        center_min_x = max(0, (IMG_SIZE - ROI_CENTER_WINDOW_PIXELS) // 2)
        center_min_y = max(0, (IMG_SIZE - ROI_CENTER_WINDOW_PIXELS) // 2)
        center_max_x = min(IMG_SIZE - 1, center_min_x + ROI_CENTER_WINDOW_PIXELS - 1)
        center_max_y = min(IMG_SIZE - 1, center_min_y + ROI_CENTER_WINDOW_PIXELS - 1)
        return not (
            (max_x < center_min_x)
            or (min_x > center_max_x)
            or (max_y < center_min_y)
            or (min_y > center_max_y)
        )

    def center_distance(bbox: tuple[int, int, int, int]) -> int:
        min_x, min_y, max_x, max_y = bbox
        center_x = (min_x + max_x) // 2
        center_y = (min_y + max_y) // 2
        return abs(center_x - (IMG_SIZE // 2)) + abs(center_y - (IMG_SIZE // 2))

    def is_better(area: int, bbox: tuple[int, int, int, int]) -> bool:
        if area < ROI_MIN_SEED_PIXELS:
            return False
        if best_label == 0:
            return True

        candidate_center = overlaps_center(bbox)
        best_center = overlaps_center(best_bbox)

        if candidate_center != best_center:
            return candidate_center
        if area != best_area:
            return area > best_area
        return center_distance(bbox) < center_distance(best_bbox)

    for start_y in range(IMG_SIZE):
        for start_x in range(IMG_SIZE):
            if labels[start_y, start_x] != 0 or not mask[start_y, start_x]:
                continue

            queue: list[tuple[int, int]] = [(start_x, start_y)]
            labels[start_y, start_x] = next_label
            head = 0
            area = 0
            min_x = max_x = start_x
            min_y = max_y = start_y

            while head < len(queue):
                x, y = queue[head]
                head += 1
                area += 1
                min_x = min(min_x, x)
                max_x = max(max_x, x)
                min_y = min(min_y, y)
                max_y = max(max_y, y)

                for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                    if (
                        0 <= nx < IMG_SIZE
                        and 0 <= ny < IMG_SIZE
                        and labels[ny, nx] == 0
                        and mask[ny, nx]
                    ):
                        labels[ny, nx] = next_label
                        queue.append((nx, ny))

            bbox = (min_x, min_y, max_x, max_y)
            if is_better(area, bbox):
                best_label = next_label
                best_area = area
                best_bbox = bbox

            next_label += 1

    if best_label == 0:
        return image, False

    output = np.full_like(image, ROI_BACKGROUND_VALUE)

    for y in range(IMG_SIZE):
        xs = np.nonzero(labels[y] == best_label)[0]
        if len(xs) == 0:
            continue

        start_x = max(0, int(xs.min()) - ROI_MARGIN_PIXELS)
        end_x = min(IMG_SIZE - 1, int(xs.max()) + ROI_MARGIN_PIXELS)
        output[y, start_x : end_x + 1, :] = image[y, start_x : end_x + 1, :]

    return output, True


def load_and_preprocess_jpeg(path: Path) -> tuple[np.ndarray, float]:
    """Match the board pipeline closely enough for training.

    Board flow for 320x240 JPEG:
    JPEG -> about 160x120 through tjpgd scale -> RGB -> map to 96x96.
    """

    data = tf.io.read_file(str(path))
    image = tf.io.decode_jpeg(data, channels=3)
    image = tf.cast(image, tf.float32)
    fruit_area_ratio = estimate_fruit_area_ratio(image)

    scaled = tf.image.resize(image, (120, 160), method="nearest")
    resized = tf.image.resize(scaled, (IMG_SIZE, IMG_SIZE), method="nearest")
    roi_image, _ = apply_fruit_region_extract(resized.numpy().astype(np.float32))
    normalized = roi_image / 255.0
    return normalized.astype(np.float32), fruit_area_ratio


def make_unknown_images(count: int, seed: int) -> np.ndarray:
    """Create simple non-fruit samples so output index 3 remains valid."""

    rng = np.random.default_rng(seed)
    images: list[np.ndarray] = []

    yy, xx = np.mgrid[0:IMG_SIZE, 0:IMG_SIZE]
    xx = xx.astype(np.float32) / max(1, IMG_SIZE - 1)
    yy = yy.astype(np.float32) / max(1, IMG_SIZE - 1)

    for _ in range(count):
        mode = int(rng.integers(0, 4))

        if mode == 0:
            base = rng.uniform(0.02, 0.18)
            gray = np.full((IMG_SIZE, IMG_SIZE), base, dtype=np.float32)
        elif mode == 1:
            base = rng.uniform(0.20, 0.55)
            gray = np.full((IMG_SIZE, IMG_SIZE), base, dtype=np.float32)
        elif mode == 2:
            start = rng.uniform(0.05, 0.45)
            end = rng.uniform(0.05, 0.45)
            gray = (start * xx) + (end * yy)
        else:
            gray = rng.uniform(0.0, 0.35, size=(IMG_SIZE, IMG_SIZE)).astype(np.float32)

        noise = rng.normal(0.0, rng.uniform(0.005, 0.025), size=(IMG_SIZE, IMG_SIZE))
        gray = np.clip(gray + noise, 0.0, 1.0)
        tint = rng.uniform(0.85, 1.15, size=(1, 1, 3)).astype(np.float32)
        image = np.clip(gray[..., np.newaxis] * tint, 0.0, 1.0)
        image, _ = apply_fruit_region_extract((image * 255.0).astype(np.float32))
        image = np.clip(image / 255.0, 0.0, 1.0)
        images.append(image.astype(np.float32))

    return np.stack(images, axis=0)


def build_dataset(dataset_dir: Path, seed: int, unknown_ratio: float) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    paths, labels = find_images(dataset_dir)
    loaded = [load_and_preprocess_jpeg(path) for path in paths]
    images = [image for image, _ in loaded]
    fruit_area_ratios = [area for _, area in loaded]

    class_counts = [labels.count(i) for i in range(3)]
    unknown_ratio = float(np.clip(unknown_ratio, 0.0, 1.0))
    unknown_count = max(1, int(round(max(class_counts) * unknown_ratio)))
    unknown_images = make_unknown_images(unknown_count, seed + 99)

    x = np.concatenate([np.stack(images, axis=0), unknown_images], axis=0)
    y = np.asarray(labels + ([3] * unknown_count), dtype=np.int64)
    size_ratios = np.asarray(fruit_area_ratios + ([0.0] * unknown_count), dtype=np.float32)
    return x, y, size_ratios


def make_size_sample_weights(
    y: np.ndarray,
    size_ratios: np.ndarray,
    strength: float,
    unknown_sample_weight: float,
) -> np.ndarray:
    """Build sample weights from per-class relative fruit area."""

    weights = np.ones_like(size_ratios, dtype=np.float32)
    unknown_sample_weight = float(np.clip(unknown_sample_weight, 0.05, 1.0))

    if strength > 0.0:
        strength = float(np.clip(strength, 0.0, 1.0))

        for label in range(3):
            indices = np.where(y == label)[0]
            if len(indices) == 0:
                continue

            class_sizes = size_ratios[indices]
            median_size = float(np.median(class_sizes))

            if median_size <= 0.001:
                continue

            relative_size = np.clip(class_sizes / median_size, 0.60, 1.40)
            weights[indices] = 1.0 + strength * (relative_size - 1.0)

    weights[y == 3] = unknown_sample_weight
    return weights.astype(np.float32)


def stratified_split(
    x: np.ndarray,
    y: np.ndarray,
    sample_weights: np.ndarray,
    val_ratio: float,
    seed: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    rng = random.Random(seed)
    train_indices: list[int] = []
    val_indices: list[int] = []

    for label in range(len(CLASS_NAMES)):
        indices = [i for i, value in enumerate(y) if int(value) == label]
        rng.shuffle(indices)
        val_count = max(1, int(round(len(indices) * val_ratio)))
        val_indices.extend(indices[:val_count])
        train_indices.extend(indices[val_count:])

    rng.shuffle(train_indices)
    rng.shuffle(val_indices)

    return (
        x[train_indices],
        y[train_indices],
        sample_weights[train_indices],
        x[val_indices],
        y[val_indices],
        sample_weights[val_indices],
    )


def build_model() -> tf.keras.Model:
    model = tf.keras.Sequential(
        [
            tf.keras.layers.Input(shape=MODEL_INPUT_SHAPE),
            tf.keras.layers.Conv2D(4, (3, 3), padding="same", activation="relu"),
            tf.keras.layers.MaxPooling2D((2, 2)),
            tf.keras.layers.Conv2D(8, (3, 3), padding="same", activation="relu"),
            tf.keras.layers.MaxPooling2D((2, 2)),
            tf.keras.layers.Flatten(),
            tf.keras.layers.Dense(12, activation="relu"),
            tf.keras.layers.Dense(len(CLASS_NAMES), activation="softmax"),
        ]
    )

    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=0.001),
        loss="sparse_categorical_crossentropy",
        metrics=["accuracy"],
    )
    return model


def convert_to_uint8_tflite(model: tf.keras.Model, representative_x: np.ndarray) -> bytes:
    def representative_dataset():
        for i in range(min(100, len(representative_x))):
            yield [representative_x[i : i + 1].astype(np.float32)]

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.uint8
    converter.inference_output_type = tf.uint8
    return converter.convert()


def evaluate_tflite(tflite_bytes: bytes, x: np.ndarray, y: np.ndarray) -> float:
    interpreter = tf.lite.Interpreter(model_content=tflite_bytes)
    interpreter.allocate_tensors()
    input_detail = interpreter.get_input_details()[0]
    output_detail = interpreter.get_output_details()[0]
    input_scale, input_zero_point = input_detail["quantization"]

    correct = 0
    for image, label in zip(x, y):
        if input_scale == 0:
            quantized = image[np.newaxis, ...].astype(np.float32)
        else:
            quantized = np.round((image[np.newaxis, ...] / input_scale) + input_zero_point)
            quantized = np.clip(quantized, 0, 255).astype(input_detail["dtype"])

        interpreter.set_tensor(input_detail["index"], quantized)
        interpreter.invoke()
        output = interpreter.get_tensor(output_detail["index"])[0]
        prediction = int(np.argmax(output))
        correct += int(prediction == int(label))

    return correct / max(1, len(y))


def write_labels(path: Path) -> None:
    lines = [f"{index}: {name}" for index, name in enumerate(CLASS_NAMES)]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    args = parse_args()
    random.seed(args.seed)
    np.random.seed(args.seed)
    tf.random.set_seed(args.seed)

    args.output_dir.mkdir(parents=True, exist_ok=True)

    x, y, size_ratios = build_dataset(args.dataset, args.seed, args.unknown_ratio)
    sample_weights = make_size_sample_weights(
        y,
        size_ratios,
        args.size_weight_strength,
        args.unknown_sample_weight,
    )
    x_train, y_train, w_train, x_val, y_val, w_val = stratified_split(
        x,
        y,
        sample_weights,
        args.val_ratio,
        args.seed,
    )

    model = build_model()
    callbacks = [
        tf.keras.callbacks.EarlyStopping(
            monitor="val_accuracy",
            patience=12,
            restore_best_weights=True,
        )
    ]

    history = model.fit(
        x_train,
        y_train,
        validation_data=(x_val, y_val),
        epochs=args.epochs,
        batch_size=args.batch_size,
        sample_weight=w_train,
        callbacks=callbacks,
        verbose=2,
    )

    h5_path = args.output_dir / f"{args.prefix}_{MODEL_ARCHITECTURE}.h5"
    tflite_path = args.output_dir / f"{args.prefix}_uint8.tflite"
    labels_path = args.output_dir / f"{args.prefix}_labels.txt"
    report_path = args.output_dir / f"{args.prefix}_report.txt"

    model.save(h5_path)
    tflite_bytes = convert_to_uint8_tflite(model, x_train)
    tflite_path.write_bytes(tflite_bytes)
    write_labels(labels_path)

    train_accuracy = float(model.evaluate(x_train, y_train, verbose=0)[1])
    val_accuracy = float(model.evaluate(x_val, y_val, verbose=0)[1])
    best_history_val_accuracy = float(max(history.history["val_accuracy"]))
    tflite_val_accuracy = evaluate_tflite(tflite_bytes, x_val, y_val)

    report = (
        f"Dataset: {args.dataset}\n"
        f"Model architecture: {MODEL_ARCHITECTURE}\n"
        f"Model parameters: {model.count_params()}\n"
        f"Class order: 0=A, 1=B, 2=C, 3=UNKNOWN\n"
        f"Input: 96x96x3 RGB, uint8 after TFLite quantization\n"
        f"ROI extraction: {ROI_EXTRACT_ENABLED}, row_span=True, min_seed={ROI_MIN_SEED_PIXELS}, "
        f"margin={ROI_MARGIN_PIXELS}, center_window={ROI_CENTER_WINDOW_PIXELS}\n"
        f"Samples: train={len(y_train)}, val={len(y_val)}, total={len(y)}\n"
        f"Class sample counts: A={int(np.count_nonzero(y == 0))}, B={int(np.count_nonzero(y == 1))}, "
        f"C={int(np.count_nonzero(y == 2))}, UNKNOWN={int(np.count_nonzero(y == 3))}\n"
        f"Size weight strength: {args.size_weight_strength:.3f}\n"
        f"Unknown ratio: {args.unknown_ratio:.3f}\n"
        f"Unknown sample weight: {args.unknown_sample_weight:.3f}\n"
        f"Train sample weight range: {float(w_train.min()):.3f}-{float(w_train.max()):.3f}\n"
        f"Keras train accuracy: {train_accuracy:.4f}\n"
        f"Keras val accuracy: {val_accuracy:.4f}\n"
        f"Best history val accuracy: {best_history_val_accuracy:.4f}\n"
        f"Uint8 TFLite val accuracy: {tflite_val_accuracy:.4f}\n"
    )
    report_path.write_text(report, encoding="utf-8")

    print(report)
    print(f"OK: wrote {h5_path}")
    print(f"OK: wrote {tflite_path}")
    print(f"OK: wrote {labels_path}")
    print(f"OK: wrote {report_path}")


if __name__ == "__main__":
    main()
