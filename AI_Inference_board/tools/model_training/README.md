# AI 水果分级模型训练说明

当前真实模型使用彩色输入，板端和训练端保持一致：

```text
input  : 96 x 96 x 3 RGB uint8
output : 4 类，顺序为 A / B / C / UNKNOWN
preprocess : 橘子区域提取，背景压制为黑色
```

为什么改成 RGB：

```text
灰度图只能看到明暗，不能可靠区分青色和橙色。
RGB 图可以让模型学习颜色、大小、果梗暗区、腐烂斑点等外观特征。
橘子区域提取可以减少背景影响，但不会把橘子裁剪放大，所以大小信息仍然保留。
```

## 训练命令

在工程根目录运行：

```powershell
python AI_Inference_board\tools\model_training\train_real_fruit_model.py `
  --dataset fruit_dataset_balanced `
  --epochs 80 `
  --batch-size 16 `
  --size-weight-strength 0.4 `
  --prefix fruit_real_rgb_roi_size_weighted
```

成功后会生成：

```text
trained_models/fruit_real_rgb_roi_size_weighted_uint8.tflite
trained_models/fruit_real_rgb_roi_size_weighted_dense.h5
trained_models/fruit_real_rgb_roi_size_weighted_labels.txt
trained_models/fruit_real_rgb_roi_size_weighted_report.txt
```

## 分类标准建议

建议后续采图时按下面标准放入 A/B/C 文件夹：

```text
A：大，橙色明显，无明显腐烂，果梗影响小
B：中等，青橙混合或轻微青色，有果梗但无明显腐烂
C：偏小，明显青色，或者有腐烂、黑斑、坏点
```

大小判断要求摄像头距离固定，否则同一个橘子靠近镜头会被看成更大。

## e-AI Translator

训练完成后，需要把 `.tflite` 转换为 C 代码，并替换：

```text
AI_Inference_board/src/Translator
```

当前 RGB 模型转换后，`layer_shapes_fruit.h` 中应能看到：

```text
27648 = 96 * 96 * 3
```

如果还是 `9216`，说明转换的仍然是旧灰度模型，不能和当前 RGB 板端代码配套。
