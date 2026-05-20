# JPEG UART Dump Tool

这个文件夹用于保存 RA6M5 推理板通过 UART 导出的 JPEG 十六进制日志，并还原成图片。

## 文件夹用途

- `logs/`：放串口助手保存的 `.txt` 日志文件。
- `output/`：脚本生成的 `.jpg` 图片输出位置。
- `restore_jpeg_from_uart.py`：从 `$JPG,DATA` 日志还原 JPEG 的 Python 脚本。

## 串口日志格式

脚本会读取如下格式：

```text
$JPG,BEGIN,4801
$JPG,DATA,0,FFD8FFE000104A464946...
$JPG,DATA,32,...
$JPG,END,4801
```

`$DBG`、`$CLS` 等其他行会被自动忽略。

## 使用方法

1. 用串口助手保存完整日志，例如：

```text
tools/jpeg_uart_dump/logs/capture_log.txt
```

2. 在工程根目录运行：

```powershell
python tools/jpeg_uart_dump/restore_jpeg_from_uart.py tools/jpeg_uart_dump/logs/capture_log.txt
```

如果当前电脑没有把 `python` 加到 PATH，也可以用 Codex 内置 Python：

```powershell
& 'C:\Users\EEDE1\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' tools/jpeg_uart_dump/restore_jpeg_from_uart.py tools/jpeg_uart_dump/logs/capture_log.txt
```

3. 生成图片：

```text
tools/jpeg_uart_dump/output/capture.jpg
```

也可以指定输出文件：

```powershell
python tools/jpeg_uart_dump/restore_jpeg_from_uart.py tools/jpeg_uart_dump/logs/capture_log.txt tools/jpeg_uart_dump/output/test1.jpg
```

## 注意事项

- 请确保日志里包含 `$JPG,BEGIN`、若干 `$JPG,DATA` 和 `$JPG,END`。
- 如果日志开头有乱码，脚本会忽略非 `$JPG` 行。
- 如果缺少某个 offset，脚本会报错，说明串口日志可能保存不完整。
