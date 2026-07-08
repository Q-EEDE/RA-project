# 基于RA6M5与AI视觉的水果智能分级与包装一体化系统

## 项目简介

本项目基于瑞萨 RA6M5 开发板，完成水果图像采集、AI 视觉分级、分拣控制和包装流程联动。系统采用双板协同结构：AI 推理板负责摄像头采集、图像预处理和水果等级判断；控制板负责电机、舵机、光电开关和分拣状态机控制。

## 系统结构

```text
水果进入检测区域
    -> 摄像头采集图像
    -> JPEG 解码与 ROI 提取
    -> RA6M5 板端 AI 推理
    -> UART 发送分类结果
    -> 控制板解析 A/B/C
    -> 舵机分流与包装动作
```

## 主要工程

| 目录 | 作用 |
| --- | --- |
| `AI_Inference_board` | AI 推理板工程，负责图像采集、预处理、e-AI 推理和分类结果发送 |
| `control_board` | 控制板工程，负责电机、舵机、光电开关、UART 接收和分拣状态机 |

## 核心功能

1. 摄像头采集：驱动 Arducam OV2640，完成 SPI/I2C 通信、FIFO 读取和 JPEG 图像获取。
2. 图像处理：将摄像头图像解码并转换为模型输入格式，支持 ROI 区域提取。
3. AI 推理：电脑端训练模型，经 e-AI Translator 转换为 C 代码后部署到 RA6M5 板端运行。
4. 分类输出：推理板通过 UART 发送类似 `$CLS,A,85` 的分类帧。
5. 分拣控制：控制板解析 A/B/C 分类结果，并驱动电机、舵机和光电传感器完成分流。
6. 调试工具：提供串口抓图、模型训练和规则评估脚本，便于采集数据和分析误分类样本。

## 重点代码位置

| 文件或目录 | 说明 |
| --- | --- |
| `AI_Inference_board/src/app_main.c` | 推理板主流程 |
| `AI_Inference_board/src/app_camera_arducam.c` | OV2640 摄像头采集与 FIFO 读取 |
| `AI_Inference_board/src/app_jpeg_decode.c` | JPEG 解码与图像输入转换 |
| `AI_Inference_board/src/app_inference.c` | ROI 提取、AI 推理、规则修正 |
| `AI_Inference_board/src/app_protocol.c` | UART 分类结果发送协议 |
| `AI_Inference_board/src/Translator` | e-AI Translator 生成的模型 C 代码 |
| `control_board/src/hal_entry.c` | 控制板状态机、电机/舵机/光电/UART 解析 |
| `AI_Inference_board/tools/model_training` | 模型训练与评估脚本 |
| `AI_Inference_board/tools/jpeg_uart_dump` | 串口图像抓取与 JPEG 恢复工具 |

## 开发环境

| 项目 | 配置 |
| --- | --- |
| MCU | Renesas RA6M5 |
| IDE | e2 studio |
| FSP | Renesas Flexible Software Package |
| 摄像头 | Arducam OV2640 |
| AI 部署 | TensorFlow Lite + Renesas e-AI Translator |
| 板间通信 | UART |

## 说明

仓库中保留核心工程代码、FSP 配置、e-AI 模型转换代码和调试脚本。大量数据集图片、临时日志、备份文件和比赛提交压缩包未作为主要源码内容上传。

