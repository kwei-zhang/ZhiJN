# 纸巾 (ZhiJN) —— ESP32 桌面 AI 机器人

「纸巾」是一个从零开始逐步演进的桌面 AI 机器人项目。目标是从手头一块裸 ESP32-WROOM 开发板出发，一步步加上屏幕表情、语音对话、舵机动作、摄像头视觉、工具调用和 3D 打印外壳。

---

## 整体路线图

| 阶段 | 内容 | 状态 |
|---|---|---|
| **阶段 1** | **ESP32 联网 + 串口打字与「纸巾」多轮对话（OpenAI Responses API）** | **当前阶段** |
| 阶段 2 | 接 SPI 彩屏：显示 AI 回复 + 会眨眼的眼睛表情 | 待开始 |
| 阶段 3 | 语音对话：唤醒词 + 说话 + AI 语音回复（切换到 `xiaozhi-esp32` + 自建 OpenAI 后端，或迁移至 RPi） | 待开始 |
| 阶段 4 | 双舵机点头/摇头，让 AI 自主决定动作 | 待开始 |
| 阶段 5 | 摄像头视觉识别（“你看到了什么？”） | 待开始 |
| 阶段 6 | 天气 / 智能家居等工具能力（MCP） | 待开始 |
| 阶段 7 | 3D 打印外壳与整机整合 | 待开始 |

后续阶段的免焊接硬件采购参考见 [docs/hardware-shopping-list.md](docs/hardware-shopping-list.md)。

---

## 阶段 1：快速上手指南（macOS + ESP32-S3-WROOM-1 N16R8）

### 目录说明

```text
ZhiJN/
├── platformio.ini              # PlatformIO 项目配置（默认板型 esp32-s3，UART 口）
├── include/
│   ├── secrets.example.h       # WiFi & OpenAI Key 配置模板
│   ├── secrets.h               # 你的真实配置（自行复制创建，git 已忽略）
│   └── openai_root_ca.h        # api.openai.com HTTPS 校验根证书
├── src/
│   └── main.cpp                # ESP32 固件主程序
├── tools/
│   ├── test_openai.py          # 烧录前先在电脑上测通 OpenAI API
│   └── serial_chat.py          # 支持中文 UTF-8 的电脑端串口聊天客户端
└── docs/
    └── hardware-shopping-list.md
```

---

### 第 1 步：安装依赖（如果还没装）

```bash
# 1. 安装 PlatformIO Core（ESP32 编译与烧录工具）
brew install platformio

# 2. 创建项目 Python 虚拟环境并安装 pyserial（给中文串口聊天脚本用）
python3 -m venv .venv
.venv/bin/pip install pyserial
```

---

### 第 2 步：填写你的 WiFi 和 OpenAI API Key

复制模板文件：

```bash
cp include/secrets.example.h include/secrets.h
```

打开 `include/secrets.h`，填入以下 4 项：

* `WIFI_SSID` / `WIFI_PASSWORD`：**必须是 2.4GHz WiFi**（ESP32 不支持 5GHz，也不支持需要网页登录或账号认证的校园网/酒店网；家里不行可以先用手机开 2.4GHz 热点）。
* `OPENAI_API_KEY`：在 [OpenAI API Keys](https://platform.openai.com/api-keys) 申请，建议为「纸巾」单独创建一个 Key，并在 Billing 页面设置月度消费上限。
* `OPENAI_MODEL`：确认一个便宜小模型的 ID（如 `gpt-5-nano` / `gpt-5-mini` / `gpt-6-luna`）。

> **安全提示**：`include/secrets.h` 已加入 `.gitignore`，不会被提交到 git。

---

### 第 3 步：先在电脑上测试 OpenAI Key 和模型（推荐）

在烧录到板子之前，先用相同接口测通 API（排除 Key/额度/模型名问题）：

```bash
python3 tools/test_openai.py --from-secrets
```

看到连续 3 轮对话成功且「纸巾」记住了你喜欢的颜色，就说明 OpenAI 这边完全 OK！

---

### 第 4 步：连接 ESP32 并编译烧录

1. 用一根**能传数据的 USB 线**插到 ESP32-S3 的 **`UART`** 口并连到 Mac（如使用两头 Type-C 线没反应，优先换 USB-A 转 USB-C 线插扩展坞）。
2. 检查串口设备是否出现：
   ```bash
   ls /dev/cu.*
   ```
   正常会看到类似 `/dev/cu.usbmodem5C940463381`、`/dev/cu.usbserial-0001` 或 `/dev/cu.wchusbserialxxxx` 的设备。
3. 编译并烧录到板子：
   ```bash
   pio run -t upload
   ```
   （默认已指向 `esp32-s3` + `UART` 口；如果改插 `USB/OTG` 口用 `-e esp32-s3-usb`，经典老款 ESP32 用 `-e esp32dev`。）

---

### 第 5 步：和「纸巾」聊天！

运行中文串口聊天客户端：

```bash
.venv/bin/python tools/serial_chat.py
```

看到 WiFi 连接成功和欢迎语后，直接输入中文回车即可聊天！内置命令：

* `/reset`：清空对话记忆，开启新对话
* `/status`：查看 WiFi IP、信号强度、模型和剩余内存
* `/help`：显示帮助
* `/quit`：退出聊天脚本

---

## 常见问题排查

1. **`ls /dev/cu.*` 里看不到 `usbserial` / `SLAB_USBtoUART` / `wchusbserial`**
   * **最常见原因**：USB 线是**纯充电线**（没有数据线芯），换一根确认能给手机传数据的线。
   * **次常见原因**：需要装 USB 转串口驱动。看 ESP32 板子靠近 USB 口那颗黑色小芯片上的字：
     * 写着 **CP2102**：去 Silicon Labs 官网下载 macOS VCP Driver。
     * 写着 **CH340 / CH340C / CH340G**：安装 WCH CH34x macOS 驱动。
2. **烧录卡在 `Connecting........_____.....`**
   * 经典 ESP32-WROOM 部分板子不会自动进入下载模式。当终端出现 `Connecting...` 时，**按住板子上的 `BOOT`（或 `IO0`）键不放**，等看到开始写进度百分比后再松开。
3. **WiFi 一直连不上**
   * 确认路由器开了 2.4GHz 频段；如使用双频合一或企业/校园网，先用手机热点（打开「最大兼容性 / 2.4GHz」）测试。
4. **能连 WiFi，但请求 OpenAI 报超时或网络错误**
   * 检查你所连的 WiFi 本身能否直连 `api.openai.com`（ESP32 默认不走你 Mac 上开的本地代理）。
5. **串口报错 `Resource busy` / 打开串口失败**
   * 同一时间只能有一个程序占用串口。如果开着 `pio device monitor` 或其他串口工具，先关掉再运行 `serial_chat.py` 或烧录。
