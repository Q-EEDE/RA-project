#!/usr/bin/env python3
"""Evaluate the current board-side fruit rules plus the active TFLite model.

This mirrors the firmware path:
JPEG -> 96x96 RGB -> ROI row-span extraction -> rule classifier -> CNN -> safe-A filter.
"""

from __future__ import annotations

import argparse
import os
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np
import tensorflow as tf


IMG_SIZE = 96
CLASS_NAMES = ("A", "B", "C", "UNKNOWN")

APP_RULE_MIN_FRUIT_PIXELS = 800
APP_RULE_CENTER_IGNORE_RADIUS_PIXELS = 14
APP_RULE_CENTER_DEFECT_COMPONENT_PIXELS = 24
APP_RULE_CENTER_DEFECT_TOTAL_PIXELS = 50
APP_RULE_CENTER_DEFECT_MAX_COMPONENT_PIXELS = 220
APP_RULE_UNKNOWN_COLOR_RATIO_PERMILLE = 80
APP_RULE_A_ORANGE_RATIO_PERMILLE = 780
APP_RULE_A_STRONG_ORANGE_RATIO_PERMILLE = 880
APP_RULE_A_GREEN_SHADOW_ALLOW_PERMILLE = 45
APP_RULE_A_SHADOW_DEFECT_COMPONENT_MAX = 320
APP_RULE_A_SHADOW_DEFECT_TOTAL_MAX = 320
APP_RULE_B_GREEN_RATIO_PERMILLE = 30
APP_RULE_B_STRONG_GREEN_RATIO_PERMILLE = 18
APP_RULE_B_DEFECT_RATIO_PERMILLE = 55
APP_RULE_B_DEFECT_COMPONENT_PIXELS = 150
APP_RULE_B_LARGE_SPOT_PIXELS = 260
APP_RULE_B_SPECKLE_TOTAL_PIXELS = 35
APP_RULE_B_SPECKLE_COMPONENT_COUNT = 10
APP_RULE_C_DEFECT_RATIO_PERMILLE = 80
APP_RULE_C_DEFECT_COMPONENT_PIXELS = 220
APP_RULE_C_TOTAL_DEFECT_PIXELS = 360
APP_RULE_C_LARGE_DEFECT_COMPONENT_PIXELS = 520
APP_RULE_C_SPECKLE_COMPONENT_MIN_PIXELS = 1
APP_RULE_C_SPECKLE_TOTAL_PIXELS = 52
APP_RULE_C_SPECKLE_COMPONENT_COUNT = 16
APP_RULE_C_SPECKLE_SPREAD_X_PIXELS = 25
APP_RULE_C_SPECKLE_SPREAD_Y_PIXELS = 40
APP_RULE_IGNORE_SMALL_SPOT_PIXELS = 50
APP_RULE_CLASSIFIER_ENABLED = True
APP_INFERENCE_UNKNOWN_OUTPUT_ENABLED = False

APP_RULE_FRUIT_BRIGHTNESS_MIN = 60
APP_RULE_FRUIT_BLUE_ALLOWANCE = 20
APP_RULE_FRUIT_COLOR_DELTA = 5
APP_RULE_ORANGE_R_MIN = 80
APP_RULE_ORANGE_G_MIN = 45
APP_RULE_ORANGE_R_OVER_G_MIN = 15
APP_RULE_ORANGE_G_OVER_B_MIN = 8
APP_RULE_GREEN_G_MIN = 50
APP_RULE_GREEN_G_OVER_B_MIN = 8
APP_RULE_GREEN_R_OVER_G_ALLOWANCE = 38
APP_RULE_STRONG_GREEN_G_MIN = 60
APP_RULE_STRONG_GREEN_G_OVER_B_MIN = 12
APP_RULE_STRONG_GREEN_R_OVER_G_ALLOWANCE = 20
APP_RULE_BLACK_BRIGHTNESS_MAX = 190
APP_RULE_BLACK_RGB_MAX = 95
APP_RULE_BROWN_BRIGHTNESS_MAX = 260
APP_RULE_BROWN_R_MAX = 130
APP_RULE_BROWN_G_MAX = 110
APP_RULE_BROWN_B_MAX = 90
APP_RULE_BROWN_R_UNDER_G_ALLOWANCE = 10
APP_RULE_CENTER_DEFECT_BRIGHTNESS_MAX = 360
APP_RULE_CENTER_DEFECT_R_MAX = 170
APP_RULE_CENTER_DEFECT_G_MAX = 150
APP_RULE_CENTER_DEFECT_B_MAX = 130
APP_RULE_CENTER_DEFECT_R_OVER_G_MIN = 30
APP_RULE_CENTER_DEFECT_R_OVER_B_MIN = 10
APP_RULE_CENTER_DEFECT_G_OVER_B_MIN = 5
APP_RULE_SPECKLE_EDGE_MARGIN_PIXELS = 8
APP_RULE_SPECKLE_LOCAL_RADIUS_PIXELS = 2
APP_RULE_SPECKLE_LOCAL_MIN_NEIGHBORS = 12
APP_RULE_SPECKLE_LOCAL_DELTA_GRAY = 12
APP_RULE_SPECKLE_GRAY_MAX = 170

APP_FRUIT_REGION_MIN_SEED_PIXELS = 120
APP_FRUIT_REGION_MARGIN_PIXELS = 5
APP_FRUIT_REGION_CENTER_WINDOW_PIXELS = 32
APP_FRUIT_REGION_BACKGROUND_VALUE = 0

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parents[2]
DEFAULT_MODEL = PROJECT_ROOT / "trained_models" / "fruit_real_rgb_roi_rebalanced_20260707_cnn_unknown_light_uint8.tflite"
DEFAULT_DATASETS = (PROJECT_ROOT / "fruit_dataset_balanced", PROJECT_ROOT / "fruit_dataset")


@dataclass
class Features:
    fruit_area: int = 0
    orange_count: int = 0
    greenish_count: int = 0
    strong_greenish_count: int = 0
    defect_count: int = 0
    max_defect_component: int = 0
    center_defect_count: int = 0
    max_center_defect_component: int = 0
    speckle_defect_count: int = 0
    speckle_defect_components: int = 0
    speckle_min_x: int = IMG_SIZE
    speckle_min_y: int = IMG_SIZE
    speckle_max_x: int = 0
    speckle_max_y: int = 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, default=DEFAULT_MODEL)
    parser.add_argument("--dataset", type=Path, action="append", default=[])
    parser.add_argument("--show-wrong", type=int, default=40)
    return parser.parse_args()


def normalize_rgb(r: int, g: int, b: int) -> tuple[int, int, int] | None:
    max_channel = max(r, g, b)
    if max_channel == 0:
        return None
    return (
        ((r * 255) + (max_channel // 2)) // max_channel,
        ((g * 255) + (max_channel // 2)) // max_channel,
        ((b * 255) + (max_channel // 2)) // max_channel,
    )


def pixel_is_fruit(r: int, g: int, b: int) -> bool:
    if (r + g + b) <= APP_RULE_FRUIT_BRIGHTNESS_MIN:
        return False
    normalized = normalize_rgb(r, g, b)
    if normalized is None:
        return False
    rn, gn, bn = normalized
    return (bn <= (((rn + gn) // 2) + APP_RULE_FRUIT_BLUE_ALLOWANCE)) and (
        (rn > (bn + APP_RULE_FRUIT_COLOR_DELTA)) or (gn > (bn + APP_RULE_FRUIT_COLOR_DELTA))
    )


def pixel_is_orange(r: int, g: int, b: int) -> bool:
    normalized = normalize_rgb(r, g, b)
    if normalized is None:
        return False
    rn, gn, bn = normalized
    return (
        rn >= APP_RULE_ORANGE_R_MIN
        and gn >= APP_RULE_ORANGE_G_MIN
        and rn >= (gn + APP_RULE_ORANGE_R_OVER_G_MIN)
        and gn >= (bn + APP_RULE_ORANGE_G_OVER_B_MIN)
    )


def pixel_is_greenish(r: int, g: int, b: int) -> bool:
    normalized = normalize_rgb(r, g, b)
    if normalized is None:
        return False
    rn, gn, bn = normalized
    return (
        gn >= APP_RULE_GREEN_G_MIN
        and gn >= (bn + APP_RULE_GREEN_G_OVER_B_MIN)
        and rn <= (gn + APP_RULE_GREEN_R_OVER_G_ALLOWANCE)
    )


def pixel_is_strong_greenish(r: int, g: int, b: int) -> bool:
    normalized = normalize_rgb(r, g, b)
    if normalized is None:
        return False
    rn, gn, bn = normalized
    return (
        gn >= APP_RULE_STRONG_GREEN_G_MIN
        and gn >= (bn + APP_RULE_STRONG_GREEN_G_OVER_B_MIN)
        and rn <= (gn + APP_RULE_STRONG_GREEN_R_OVER_G_ALLOWANCE)
    )


def pixel_gray(image: np.ndarray, x: int, y: int) -> int:
    r, g, b = (int(v) for v in image[y, x])
    return ((77 * r) + (150 * g) + (29 * b)) >> 8


def center_ignored(x: int, y: int) -> bool:
    center = IMG_SIZE // 2
    dx = abs(x - center)
    dy = abs(y - center)
    radius = APP_RULE_CENTER_IGNORE_RADIUS_PIXELS
    return ((dx * dx) + (dy * dy)) <= (radius * radius)


def pixel_is_defect(image: np.ndarray, x: int, y: int) -> bool:
    if center_ignored(x, y):
        return False
    r, g, b = (int(v) for v in image[y, x])
    brightness = r + g + b
    if not pixel_is_fruit(r, g, b):
        return False
    if brightness >= APP_RULE_BLACK_BRIGHTNESS_MAX and pixel_is_orange(r, g, b):
        return False
    if brightness < APP_RULE_BLACK_BRIGHTNESS_MAX and r < APP_RULE_BLACK_RGB_MAX and g < APP_RULE_BLACK_RGB_MAX and b < APP_RULE_BLACK_RGB_MAX:
        return True
    return (
        brightness < APP_RULE_BROWN_BRIGHTNESS_MAX
        and r < APP_RULE_BROWN_R_MAX
        and g < APP_RULE_BROWN_G_MAX
        and b < APP_RULE_BROWN_B_MAX
        and (r + APP_RULE_BROWN_R_UNDER_G_ALLOWANCE) >= g
    )


def pixel_is_center_defect(image: np.ndarray, x: int, y: int) -> bool:
    if not center_ignored(x, y):
        return False
    r, g, b = (int(v) for v in image[y, x])
    brightness = r + g + b
    if not pixel_is_fruit(r, g, b):
        return False
    return (
        brightness < APP_RULE_CENTER_DEFECT_BRIGHTNESS_MAX
        and r < APP_RULE_CENTER_DEFECT_R_MAX
        and g < APP_RULE_CENTER_DEFECT_G_MAX
        and b < APP_RULE_CENTER_DEFECT_B_MAX
        and r >= (g + APP_RULE_CENTER_DEFECT_R_OVER_G_MIN)
        and r >= (b + APP_RULE_CENTER_DEFECT_R_OVER_B_MIN)
        and g >= (b + APP_RULE_CENTER_DEFECT_G_OVER_B_MIN)
    )


def pixel_is_speckle(image: np.ndarray, x: int, y: int, row_min: np.ndarray, row_max: np.ndarray) -> bool:
    edge_margin = APP_RULE_SPECKLE_EDGE_MARGIN_PIXELS
    if center_ignored(x, y):
        return False
    if row_min[y] >= IMG_SIZE or x <= (int(row_min[y]) + edge_margin) or (x + edge_margin) >= int(row_max[y]):
        return False
    r, g, b = (int(v) for v in image[y, x])
    if not pixel_is_fruit(r, g, b):
        return False
    gray = pixel_gray(image, x, y)
    if gray > APP_RULE_SPECKLE_GRAY_MAX:
        return False

    radius = APP_RULE_SPECKLE_LOCAL_RADIUS_PIXELS
    local_sum = 0
    local_count = 0
    for ly in range(max(0, y - radius), min(IMG_SIZE - 1, y + radius) + 1):
        for lx in range(max(0, x - radius), min(IMG_SIZE - 1, x + radius) + 1):
            lr, lg, lb = (int(v) for v in image[ly, lx])
            if pixel_is_fruit(lr, lg, lb):
                local_sum += pixel_gray(image, lx, ly)
                local_count += 1
    if local_count < APP_RULE_SPECKLE_LOCAL_MIN_NEIGHBORS:
        return False
    return (local_sum // local_count) >= (gray + APP_RULE_SPECKLE_LOCAL_DELTA_GRAY)


def connected_components(mask: np.ndarray) -> list[tuple[int, int, int, int, int]]:
    seen = np.zeros(mask.shape, dtype=bool)
    comps: list[tuple[int, int, int, int, int]] = []
    for sy in range(IMG_SIZE):
        for sx in range(IMG_SIZE):
            if seen[sy, sx] or not mask[sy, sx]:
                continue
            queue = [(sx, sy)]
            seen[sy, sx] = True
            head = 0
            area = 0
            min_x = max_x = sx
            min_y = max_y = sy
            while head < len(queue):
                x, y = queue[head]
                head += 1
                area += 1
                min_x = min(min_x, x)
                max_x = max(max_x, x)
                min_y = min(min_y, y)
                max_y = max(max_y, y)
                for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                    if 0 <= nx < IMG_SIZE and 0 <= ny < IMG_SIZE and (not seen[ny, nx]) and mask[ny, nx]:
                        seen[ny, nx] = True
                        queue.append((nx, ny))
            comps.append((area, min_x, min_y, max_x, max_y))
    return comps


def region_seed_mask(image: np.ndarray) -> np.ndarray:
    mask = np.zeros((IMG_SIZE, IMG_SIZE), dtype=bool)
    for y in range(IMG_SIZE):
        for x in range(IMG_SIZE):
            r, g, b = (int(v) for v in image[y, x])
            brightness = r + g + b
            if brightness < 90:
                continue
            normalized = normalize_rgb(r, g, b)
            if normalized is None:
                continue
            rn, gn, bn = normalized
            rg_sum = rn + gn
            mask[y, x] = (bn <= (rg_sum // 2)) and ((rn > (bn + 10)) or (gn > (bn + 10))) and (rg_sum > ((2 * bn) + 20))
    return mask


def extract_roi(image: np.ndarray) -> tuple[np.ndarray, bool]:
    mask = region_seed_mask(image)
    comps = connected_components(mask)
    best: tuple[int, int, int, int, int] | None = None

    def overlaps_center(comp: tuple[int, int, int, int, int]) -> bool:
        _, min_x, min_y, max_x, max_y = comp
        cmin = (IMG_SIZE - APP_FRUIT_REGION_CENTER_WINDOW_PIXELS) // 2
        cmax = cmin + APP_FRUIT_REGION_CENTER_WINDOW_PIXELS - 1
        return not (max_x < cmin or min_x > cmax or max_y < cmin or min_y > cmax)

    def center_distance(comp: tuple[int, int, int, int, int]) -> int:
        _, min_x, min_y, max_x, max_y = comp
        return abs(((min_x + max_x) // 2) - (IMG_SIZE // 2)) + abs(((min_y + max_y) // 2) - (IMG_SIZE // 2))

    for comp in comps:
        area = comp[0]
        if area < APP_FRUIT_REGION_MIN_SEED_PIXELS:
            continue
        if best is None:
            best = comp
            continue
        if overlaps_center(comp) != overlaps_center(best):
            if overlaps_center(comp):
                best = comp
            continue
        if area != best[0]:
            if area > best[0]:
                best = comp
            continue
        if center_distance(comp) < center_distance(best):
            best = comp

    if best is None:
        return image, False

    best_area, _, _, _, _ = best
    # Rebuild the best label by taking the matching connected component pixels.
    # This is easier and deterministic because connected_components returns in scan order.
    target_seen = np.zeros(mask.shape, dtype=bool)
    selected = np.zeros(mask.shape, dtype=bool)
    for sy in range(IMG_SIZE):
        for sx in range(IMG_SIZE):
            if target_seen[sy, sx] or not mask[sy, sx]:
                continue
            queue = [(sx, sy)]
            pixels: list[tuple[int, int]] = []
            target_seen[sy, sx] = True
            head = 0
            while head < len(queue):
                x, y = queue[head]
                head += 1
                pixels.append((x, y))
                for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                    if 0 <= nx < IMG_SIZE and 0 <= ny < IMG_SIZE and (not target_seen[ny, nx]) and mask[ny, nx]:
                        target_seen[ny, nx] = True
                        queue.append((nx, ny))
            if len(pixels) == best_area:
                min_x = min(x for x, _ in pixels)
                min_y = min(y for _, y in pixels)
                max_x = max(x for x, _ in pixels)
                max_y = max(y for _, y in pixels)
                if (best_area, min_x, min_y, max_x, max_y) == best:
                    for x, y in pixels:
                        selected[y, x] = True
                    break

    output = np.full_like(image, APP_FRUIT_REGION_BACKGROUND_VALUE)
    for y in range(IMG_SIZE):
        xs = np.nonzero(selected[y])[0]
        if len(xs) == 0:
            continue
        start_x = max(0, int(xs.min()) - APP_FRUIT_REGION_MARGIN_PIXELS)
        end_x = min(IMG_SIZE - 1, int(xs.max()) + APP_FRUIT_REGION_MARGIN_PIXELS)
        output[y, start_x : end_x + 1, :] = image[y, start_x : end_x + 1, :]
    return output, True


def extract_features(image: np.ndarray) -> Features:
    features = Features()
    row_min = np.full(IMG_SIZE, IMG_SIZE, dtype=np.int32)
    row_max = np.zeros(IMG_SIZE, dtype=np.int32)

    for y in range(IMG_SIZE):
        for x in range(IMG_SIZE):
            r, g, b = (int(v) for v in image[y, x])
            if not pixel_is_fruit(r, g, b):
                continue
            features.fruit_area += 1
            row_min[y] = min(int(row_min[y]), x)
            row_max[y] = max(int(row_max[y]), x)
            is_orange = pixel_is_orange(r, g, b)
            if is_orange:
                features.orange_count += 1
            if not is_orange and pixel_is_greenish(r, g, b):
                features.greenish_count += 1
            if not is_orange and pixel_is_strong_greenish(r, g, b):
                features.strong_greenish_count += 1

    defect_mask = np.zeros((IMG_SIZE, IMG_SIZE), dtype=bool)
    speckle_mask = np.zeros((IMG_SIZE, IMG_SIZE), dtype=bool)
    center_mask = np.zeros((IMG_SIZE, IMG_SIZE), dtype=bool)
    for y in range(IMG_SIZE):
        for x in range(IMG_SIZE):
            defect_mask[y, x] = pixel_is_defect(image, x, y)
            speckle_mask[y, x] = pixel_is_speckle(image, x, y, row_min, row_max)
            center_mask[y, x] = pixel_is_center_defect(image, x, y)

    for area, _, _, _, _ in connected_components(defect_mask):
        if area >= APP_RULE_IGNORE_SMALL_SPOT_PIXELS:
            features.defect_count += area
        features.max_defect_component = max(features.max_defect_component, area)

    for area, min_x, min_y, max_x, max_y in connected_components(speckle_mask):
        if area >= APP_RULE_C_SPECKLE_COMPONENT_MIN_PIXELS:
            features.speckle_defect_count += area
            features.speckle_defect_components += 1
            features.speckle_min_x = min(features.speckle_min_x, min_x)
            features.speckle_min_y = min(features.speckle_min_y, min_y)
            features.speckle_max_x = max(features.speckle_max_x, max_x)
            features.speckle_max_y = max(features.speckle_max_y, max_y)

    for area, _, _, _, _ in connected_components(center_mask):
        features.center_defect_count += area
        features.max_center_defect_component = max(features.max_center_defect_component, area)

    return features


def permille(count: int, total: int) -> int:
    if total == 0:
        return 0
    return (count * 1000) // total


def rule_classifier(image: np.ndarray) -> tuple[str | None, str]:
    features = extract_features(image)
    if features.fruit_area < APP_RULE_MIN_FRUIT_PIXELS:
        return "UNKNOWN", "RULE_UNKNOWN_NO_FRUIT"

    orange_p = permille(features.orange_count, features.fruit_area)
    green_p = permille(features.greenish_count, features.fruit_area)
    strong_green_p = permille(features.strong_greenish_count, features.fruit_area)
    defect_p = permille(features.defect_count, features.fruit_area)
    fruit_color_count = min(features.fruit_area, features.orange_count + features.greenish_count)
    fruit_color_p = permille(fruit_color_count, features.fruit_area)

    speckle_spread_x = 0
    speckle_spread_y = 0
    if features.speckle_min_x <= features.speckle_max_x and features.speckle_min_y <= features.speckle_max_y:
        speckle_spread_x = features.speckle_max_x - features.speckle_min_x + 1
        speckle_spread_y = features.speckle_max_y - features.speckle_min_y + 1

    large_defect = (
        defect_p >= APP_RULE_C_DEFECT_RATIO_PERMILLE
        and features.defect_count >= APP_RULE_C_TOTAL_DEFECT_PIXELS
        and features.max_defect_component >= APP_RULE_C_DEFECT_COMPONENT_PIXELS
    ) or (features.max_defect_component >= APP_RULE_C_LARGE_DEFECT_COMPONENT_PIXELS)
    center_defect = (
        features.center_defect_count >= APP_RULE_CENTER_DEFECT_TOTAL_PIXELS
        and features.max_center_defect_component >= APP_RULE_CENTER_DEFECT_COMPONENT_PIXELS
        and features.max_center_defect_component <= APP_RULE_CENTER_DEFECT_MAX_COMPONENT_PIXELS
    )
    speckle_defect = (
        features.speckle_defect_count >= APP_RULE_C_SPECKLE_TOTAL_PIXELS
        and features.speckle_defect_components >= APP_RULE_C_SPECKLE_COMPONENT_COUNT
        and speckle_spread_x >= APP_RULE_C_SPECKLE_SPREAD_X_PIXELS
        and speckle_spread_y >= APP_RULE_C_SPECKLE_SPREAD_Y_PIXELS
        and (
            features.defect_count >= APP_RULE_C_SPECKLE_TOTAL_PIXELS
            or features.max_defect_component >= APP_RULE_IGNORE_SMALL_SPOT_PIXELS
            or (
                green_p < APP_RULE_B_GREEN_RATIO_PERMILLE
                and strong_green_p < APP_RULE_B_STRONG_GREEN_RATIO_PERMILLE
            )
        )
    )
    if large_defect or center_defect or speckle_defect:
        return "C", "RULE_C_SPECKLE" if speckle_defect else "RULE_C_DEFECT"

    if (
        orange_p >= APP_RULE_A_STRONG_ORANGE_RATIO_PERMILLE
        and green_p < APP_RULE_A_GREEN_SHADOW_ALLOW_PERMILLE
        and strong_green_p < APP_RULE_A_GREEN_SHADOW_ALLOW_PERMILLE
        and features.max_defect_component < APP_RULE_A_SHADOW_DEFECT_COMPONENT_MAX
        and features.defect_count < APP_RULE_A_SHADOW_DEFECT_TOTAL_MAX
        and features.speckle_defect_count < APP_RULE_C_SPECKLE_TOTAL_PIXELS
    ):
        return "A", "RULE_A_ORANGE"

    if green_p >= APP_RULE_B_GREEN_RATIO_PERMILLE or strong_green_p >= APP_RULE_B_STRONG_GREEN_RATIO_PERMILLE:
        return "B", "RULE_B_GREEN"

    if (
        (defect_p >= APP_RULE_B_DEFECT_RATIO_PERMILLE and features.max_defect_component >= APP_RULE_B_DEFECT_COMPONENT_PIXELS)
        or features.max_defect_component >= APP_RULE_B_LARGE_SPOT_PIXELS
        or (
            features.defect_count >= APP_RULE_B_SPECKLE_TOTAL_PIXELS
            and features.speckle_defect_components >= APP_RULE_B_SPECKLE_COMPONENT_COUNT
        )
    ):
        return "B", "RULE_B_SPOT"

    if orange_p >= APP_RULE_A_ORANGE_RATIO_PERMILLE:
        return "A", "RULE_A_ORANGE"

    return "A", "RULE_A_DEFAULT"


def safe_a_filter(image: np.ndarray, pred: str, status: str) -> tuple[str, str]:
    if pred != "A":
        return pred, status
    features = extract_features(image)
    if features.fruit_area < APP_RULE_MIN_FRUIT_PIXELS:
        return "UNKNOWN", "SAFE_A_TO_UNKNOWN"
    orange_p = permille(features.orange_count, features.fruit_area)
    green_p = permille(features.greenish_count, features.fruit_area)
    strong_green_p = permille(features.strong_greenish_count, features.fruit_area)
    defect_p = permille(features.defect_count, features.fruit_area)
    fruit_color_p = permille(min(features.fruit_area, features.orange_count + features.greenish_count), features.fruit_area)

    if fruit_color_p < APP_RULE_UNKNOWN_COLOR_RATIO_PERMILLE:
        return "UNKNOWN", "SAFE_A_TO_UNKNOWN"

    large_defect = (
        defect_p >= APP_RULE_C_DEFECT_RATIO_PERMILLE
        and features.defect_count >= APP_RULE_C_TOTAL_DEFECT_PIXELS
        and features.max_defect_component >= APP_RULE_C_DEFECT_COMPONENT_PIXELS
    ) or (features.max_defect_component >= APP_RULE_C_LARGE_DEFECT_COMPONENT_PIXELS)
    center_defect = (
        features.center_defect_count >= APP_RULE_CENTER_DEFECT_TOTAL_PIXELS
        and features.max_center_defect_component >= APP_RULE_CENTER_DEFECT_COMPONENT_PIXELS
        and features.max_center_defect_component <= APP_RULE_CENTER_DEFECT_MAX_COMPONENT_PIXELS
    )
    if large_defect or center_defect:
        return "C", "SAFE_A_TO_C"

    green_risk = (
        strong_green_p >= APP_RULE_B_STRONG_GREEN_RATIO_PERMILLE
        or (green_p >= APP_RULE_B_GREEN_RATIO_PERMILLE and orange_p < APP_RULE_A_ORANGE_RATIO_PERMILLE)
    )
    spot_risk = (
        features.max_defect_component >= APP_RULE_B_LARGE_SPOT_PIXELS
        or (defect_p >= APP_RULE_B_DEFECT_RATIO_PERMILLE and features.max_defect_component >= APP_RULE_B_DEFECT_COMPONENT_PIXELS)
    )
    if green_risk or spot_risk:
        return "B", "SAFE_A_TO_B"
    return pred, status


def apply_unknown_output_policy(image: np.ndarray, pred: str, status: str) -> tuple[str, str]:
    if APP_INFERENCE_UNKNOWN_OUTPUT_ENABLED or pred != "UNKNOWN":
        return pred, status
    features = extract_features(image)
    if features.fruit_area < APP_RULE_MIN_FRUIT_PIXELS:
        return "C", "UNKNOWN_NO_FRUIT_TO_C"

    orange_p = permille(features.orange_count, features.fruit_area)
    green_p = permille(features.greenish_count, features.fruit_area)
    strong_green_p = permille(features.strong_greenish_count, features.fruit_area)
    defect_p = permille(features.defect_count, features.fruit_area)
    fruit_color_p = permille(min(features.fruit_area, features.orange_count + features.greenish_count), features.fruit_area)

    speckle_spread_x = 0
    speckle_spread_y = 0
    if features.speckle_min_x <= features.speckle_max_x and features.speckle_min_y <= features.speckle_max_y:
        speckle_spread_x = features.speckle_max_x - features.speckle_min_x + 1
        speckle_spread_y = features.speckle_max_y - features.speckle_min_y + 1

    large_defect = (
        defect_p >= APP_RULE_C_DEFECT_RATIO_PERMILLE
        and features.defect_count >= APP_RULE_C_TOTAL_DEFECT_PIXELS
        and features.max_defect_component >= APP_RULE_C_DEFECT_COMPONENT_PIXELS
    ) or (features.max_defect_component >= APP_RULE_C_LARGE_DEFECT_COMPONENT_PIXELS)
    center_defect = (
        features.center_defect_count >= APP_RULE_CENTER_DEFECT_TOTAL_PIXELS
        and features.max_center_defect_component >= APP_RULE_CENTER_DEFECT_COMPONENT_PIXELS
        and features.max_center_defect_component <= APP_RULE_CENTER_DEFECT_MAX_COMPONENT_PIXELS
    )
    speckle_defect = (
        features.speckle_defect_count >= APP_RULE_C_SPECKLE_TOTAL_PIXELS
        and features.speckle_defect_components >= APP_RULE_C_SPECKLE_COMPONENT_COUNT
        and speckle_spread_x >= APP_RULE_C_SPECKLE_SPREAD_X_PIXELS
        and speckle_spread_y >= APP_RULE_C_SPECKLE_SPREAD_Y_PIXELS
        and (
            features.defect_count >= APP_RULE_C_SPECKLE_TOTAL_PIXELS
            or features.max_defect_component >= APP_RULE_IGNORE_SMALL_SPOT_PIXELS
            or (
                green_p < APP_RULE_B_GREEN_RATIO_PERMILLE
                and strong_green_p < APP_RULE_B_STRONG_GREEN_RATIO_PERMILLE
            )
        )
    )
    if large_defect or center_defect or speckle_defect:
        return "C", "UNKNOWN_RULE_C_SPECKLE" if speckle_defect else "UNKNOWN_RULE_C_DEFECT"

    if strong_green_p >= APP_RULE_B_STRONG_GREEN_RATIO_PERMILLE or green_p >= APP_RULE_B_GREEN_RATIO_PERMILLE:
        return "B", "UNKNOWN_RULE_B_GREEN"

    spot_risk = (
        features.max_defect_component >= APP_RULE_B_LARGE_SPOT_PIXELS
        or (defect_p >= APP_RULE_B_DEFECT_RATIO_PERMILLE and features.max_defect_component >= APP_RULE_B_DEFECT_COMPONENT_PIXELS)
        or (
            features.defect_count >= APP_RULE_B_SPECKLE_TOTAL_PIXELS
            and features.speckle_defect_components >= APP_RULE_B_SPECKLE_COMPONENT_COUNT
        )
    )
    if spot_risk:
        return "B", "UNKNOWN_RULE_B_SPOT"

    if orange_p >= 500 and fruit_color_p >= 300:
        return "A", "UNKNOWN_RULE_A_ORANGE"

    return "A", "UNKNOWN_RULE_A_DEFAULT"


def load_board_image(path: Path) -> np.ndarray:
    data = tf.io.read_file(str(path))
    image = tf.io.decode_jpeg(data, channels=3)
    image = tf.cast(image, tf.float32)
    scaled = tf.image.resize(image, (120, 160), method="nearest")
    resized = tf.image.resize(scaled, (IMG_SIZE, IMG_SIZE), method="nearest").numpy()
    return np.clip(resized, 0, 255).astype(np.uint8)


class TfliteModel:
    def __init__(self, path: Path) -> None:
        self.interpreter = tf.lite.Interpreter(model_path=str(path))
        self.interpreter.allocate_tensors()
        self.input_detail = self.interpreter.get_input_details()[0]
        self.output_detail = self.interpreter.get_output_details()[0]

    def predict(self, image: np.ndarray) -> str:
        tensor = image[np.newaxis, ...]
        if self.input_detail["dtype"] != np.uint8:
            scale, zero_point = self.input_detail["quantization"]
            tensor = np.round((tensor.astype(np.float32) / 255.0 / scale) + zero_point)
        tensor = np.clip(tensor, 0, 255).astype(self.input_detail["dtype"])
        self.interpreter.set_tensor(self.input_detail["index"], tensor)
        self.interpreter.invoke()
        output = self.interpreter.get_tensor(self.output_detail["index"])[0]
        return CLASS_NAMES[int(np.argmax(output))]


def predict_final(model: TfliteModel, raw_image: np.ndarray) -> tuple[str, str, bool]:
    model_image, roi_applied = extract_roi(raw_image)
    if APP_RULE_CLASSIFIER_ENABLED:
        pred, status = rule_classifier(model_image)
    else:
        pred, status = None, "RULE_DISABLED"

    if pred is None:
        pred = model.predict(model_image)
        status = "EAI_OK_ROI" if roi_applied else "EAI_OK_RAW"
        pred, status = safe_a_filter(model_image, pred, status)
    pred, status = apply_unknown_output_policy(model_image, pred, status)
    return pred, status, roi_applied


def evaluate_dataset(dataset_dir: Path, model: TfliteModel, show_wrong: int) -> str:
    samples: list[tuple[Path, str]] = []
    for label in CLASS_NAMES[:3]:
        samples.extend((path, label) for path in sorted((dataset_dir / label).glob("*.jpg")))
    if not samples:
        return f"Dataset: {dataset_dir}\nNo A/B/C .jpg samples found.\n"

    confusion = {true: Counter() for true in CLASS_NAMES[:3]}
    status_counter: Counter[str] = Counter()
    wrong: list[tuple[str, str, str, str]] = []
    roi_count = 0

    for path, true in samples:
        raw_image = load_board_image(path)
        pred, status, roi_applied = predict_final(model, raw_image)
        confusion[true][pred] += 1
        status_counter[status] += 1
        roi_count += int(roi_applied)
        if pred != true:
            wrong.append((path.name, true, pred, status))

    total = len(samples)
    correct = total - len(wrong)
    c_total = sum(confusion["C"].values())
    c_to_a = confusion["C"]["A"]
    c_ok = confusion["C"]["C"]

    lines = [
        f"Dataset: {dataset_dir}",
        f"Samples: {total}  A={sum(confusion['A'].values())} B={sum(confusion['B'].values())} C={sum(confusion['C'].values())}",
        f"Final firmware-style accuracy: {correct / total:.4f} ({correct}/{total})",
        f"C safety: C->A = {c_to_a}/{c_total}; C recall = {c_ok / max(1, c_total):.4f} ({c_ok}/{c_total})",
        f"ROI applied: {roi_count}/{total}",
        "Confusion matrix rows=true A/B/C, cols=pred A/B/C/UNKNOWN:",
        "      A   B   C   U",
    ]
    for true in CLASS_NAMES[:3]:
        row = confusion[true]
        lines.append(f"{true}: {row['A']:3d} {row['B']:3d} {row['C']:3d} {row['UNKNOWN']:3d}")
    lines.append("Status counts:")
    for status, count in status_counter.most_common():
        lines.append(f"  {status}: {count}")
    lines.append(f"Wrong count: {len(wrong)}")
    for name, true, pred, status in wrong[:show_wrong]:
        lines.append(f"  {name}: true={true} pred={pred} status={status}")
    if len(wrong) > show_wrong:
        lines.append(f"  ... {len(wrong) - show_wrong} more")
    return "\n".join(lines) + "\n"


def main() -> None:
    args = parse_args()
    datasets = args.dataset or list(DEFAULT_DATASETS)
    model = TfliteModel(args.model)
    print(f"Model: {args.model}")
    for dataset in datasets:
        print()
        print(evaluate_dataset(dataset, model, args.show_wrong))


if __name__ == "__main__":
    main()
