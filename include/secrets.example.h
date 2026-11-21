#pragma once

// 复制本文件为 include/secrets.h 并填入你的真实信息
// 注意：include/secrets.h 已加入 .gitignore，不会被提交到 git

// 只支持 2.4GHz WiFi（不支持 5GHz，也不支持需要网页登录或账号认证的校园网）
#define WIFI_SSID       "你的2.4GHz_WiFi名称"
#define WIFI_PASSWORD   "你的WiFi密码"

// 在 https://platform.openai.com/api-keys 申请
// 强烈建议为「纸巾」单独建一个 Key，并在 Billing 页面设置月度消费上限
#define OPENAI_API_KEY  "sk-proj-..."

// 可在 https://platform.openai.com/docs/models 确认你要用的便宜小模型 ID
// 例如 gpt-5-nano / gpt-5-mini / gpt-6-luna（先用 tools/test_openai.py 测试通过再烧录）
#define OPENAI_MODEL    "gpt-5-nano"

// 机器人「纸巾」的人设提示词
#define SYSTEM_PROMPT   "你是一个放在桌面上的小机器人，名字叫纸巾。你性格友好、有点机灵，回答简短自然（尽量不超过3句话），默认使用中文。"

// 单次回复最大 token 数（留一点余量兼容推理小模型）
#define OPENAI_MAX_OUTPUT_TOKENS 600
