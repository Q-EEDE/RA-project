# JPEG UART Dump Tool

这个文件夹用于把 RA6M5 推理板通过 SCI9 调试串口导出的 JPEG 数据保存成图片。

## 文件用途

- `auto_capture_dataset.py`：实时监听串口，自动保存图片到数据集文件夹。
- `restore_jpeg_from_uart.py`：把已经保存好的串口日志还原成 JPEG。
- `logs/`：保存原始串口日志。
- `output/`：保存单张测试输出图片。

## 推荐采集方式

先安装串口库：

```powershell
python -m pip install pyserial
```

查看电脑当前串口：

```powershell
cd "S:\about RA\ra project\AI_Inference_board"
python tools\jpeg_uart_dump\auto_capture_dataset.py --list-ports
```

### 方式 1：电脑自动触发拍照

如果电脑同时接了两个串口：

- SCI9：推理板调试串口，输出 `$DBG`、`$JPG`、`$CLS`
- SCI2：推理板主控通信串口，电脑发送 `$TRIG`

采集 A 类 20 张：

```powershell
python tools\jpeg_uart_dump\auto_capture_dataset.py --debug-port COM6 --host-port COM7 --class A --count 20 --echo
```

采集 B 类 20 张：

```powershell
python tools\jpeg_uart_dump\auto_capture_dataset.py --debug-port COM6 --host-port COM7 --class B --count 20 --echo
```

采集 C 类 20 张：

```powershell
python tools\jpeg_uart_dump\auto_capture_dataset.py --debug-port COM6 --host-port COM7 --class C --count 20 --echo
```

采集 unknown 类 20 张：

```powershell
python tools\jpeg_uart_dump\auto_capture_dataset.py --debug-port COM6 --host-port COM7 --class unknown --count 20 --echo
```

保存位置：

```text
S:\about RA\ra project\fruit_dataset\A
S:\about RA\ra project\fruit_dataset\B
S:\about RA\ra project\fruit_dataset\C
S:\about RA\ra project\fruit_dataset\unknown
```

## 方式 2：手动按键触发拍照

如果电脑只接了 SCI9 调试串口，不接 SCI2，就不要写 `--host-port`。

例如采集 B 类 10 张：

```powershell
python tools\jpeg_uart_dump\auto_capture_dataset.py --debug-port COM6 --class B --count 10 --echo
```

脚本提示 `Passive mode: press the board capture button once.` 时，按一次推理板拍照按键。

## 文件命名

脚本会自动从当前文件夹里找下一个编号：

```text
A001.jpg
A002.jpg
B001.jpg
unknown001.jpg
```

如果想指定起始编号：

```powershell
python tools\jpeg_uart_dump\auto_capture_dataset.py --debug-port COM6 --class A --count 20 --start-index 31
```

## 保存原始日志

如果想同时保存 `$JPG` 原始文本日志，方便排查问题：

```powershell
python tools\jpeg_uart_dump\auto_capture_dataset.py --debug-port COM6 --host-port COM7 --class A --count 5 --save-log --echo
```

## 常见问题

- `Missing Python package: pyserial`：先运行 `python -m pip install pyserial`。
- 一直等不到图片：检查 `--debug-port` 是否接的是 SCI9，而不是 SCI2。
- 自动触发无效：检查 `--host-port` 是否接的是 SCI2，并确认推理板程序处于按键/主控触发模式。
- 图片保存失败或提示 JPEG 不完整：说明串口日志中 `$JPG,DATA` 可能丢行，建议降低干扰、确认波特率是 `115200`，并不要同时开串口助手占用同一个 COM 口。
