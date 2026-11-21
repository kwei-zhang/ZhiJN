#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "openai_root_ca.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "未找到 include/secrets.h，当前使用 secrets.example.h 占位配置编译。烧录前请复制为 include/secrets.h 并填写真实配置。"
#include "secrets.example.h"
#endif

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kHttpTimeoutMs = 30000;
constexpr size_t kMaxInputBytes = 1024;
#ifndef OPENAI_MAX_OUTPUT_TOKENS
#define OPENAI_MAX_OUTPUT_TOKENS 600
#endif
constexpr int kMaxOutputTokens = OPENAI_MAX_OUTPUT_TOKENS;
constexpr const char* kOpenAIEndpoint = "https://api.openai.com/v1/responses";

// 多轮对话状态：只保存在 OpenAI 返回的上一条 response id（几十字节），节省 ESP32 内存
String previousResponseId;
String serialLineBuffer;
bool timeSynced = false;

bool isPlaceholderConfig() {
  String ssid = WIFI_SSID;
  String key = OPENAI_API_KEY;
  return ssid.isEmpty() || ssid.startsWith("你的") || key.isEmpty() ||
         key.indexOf("...") >= 0;
}

void syncTimeIfNeeded() {
  if (timeSynced || WiFi.status() != WL_CONNECTED) {
    return;
  }
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  Serial.print("[系统] 正在同步网络时间...");
  time_t now = 0;
  const uint32_t start = millis();
  while (now < 1700000000 && millis() - start < 3000) {
    delay(200);
    time(&now);
  }
  if (now >= 1700000000) {
    timeSynced = true;
    Serial.println("完成");
  } else {
    Serial.println("超时（跳过，不影响测试）");
  }
}

bool connectWiFi(uint32_t timeoutMs = 15000) {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  Serial.print("[WiFi] 正在连接 ");
  Serial.print(WIFI_SSID);
  Serial.print(" ...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] 已连接！IP: ");
    Serial.print(WiFi.localIP());
    Serial.print("  信号强度: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    syncTimeIfNeeded();
    return true;
  }

  Serial.println("[WiFi] 连接失败！请检查：");
  Serial.println("  1. include/secrets.h 里的 WiFi 名称和密码是否正确");
  Serial.println("  2. 必须是 2.4GHz WiFi（ESP32 不支持 5GHz，也不支持网页认证/校园网）");
  return false;
}

void printHelp() {
  Serial.println("----------------------------------------");
  Serial.println("你好，我是桌面小机器人「纸巾」！");
  Serial.println("直接输入文字按回车即可聊天，可用命令：");
  Serial.println("  /reset  清空对话记忆，开启新对话");
  Serial.println("  /status 查看 WiFi、内存和模型状态");
  Serial.println("  /help   显示本帮助");
  Serial.println("----------------------------------------");
}

void printStatus() {
  Serial.println("---------- 纸巾状态 ----------");
  Serial.print("WiFi: ");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("已连接 (");
    Serial.print(WiFi.localIP());
    Serial.print(", RSSI ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm)");
  } else {
    Serial.println("未连接");
  }
  Serial.print("模型: ");
  Serial.println(OPENAI_MODEL);
  Serial.print("多轮会话: ");
  Serial.println(previousResponseId.isEmpty() ? "新对话" : previousResponseId);
  Serial.print("剩余内存 (Heap): ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");
  Serial.println("------------------------------");
}

// 非阻塞按行读取串口输入（UTF-8 透传，以 '\n' 结束）
bool readLineFromSerial(String& outLine) {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      outLine = serialLineBuffer;
      serialLineBuffer = "";
      outLine.trim();
      return !outLine.isEmpty();
    }
    if (serialLineBuffer.length() < kMaxInputBytes) {
      serialLineBuffer += ch;
    }
  }
  return false;
}

String formatHttpError(int code, const String& apiMessage) {
  String hint;
  switch (code) {
    case 400:
      hint = "请求参数有误（请检查模型名或参数格式）";
      break;
    case 401:
      hint = "API Key 无效或未填写，请检查 include/secrets.h 中的 OPENAI_API_KEY";
      break;
    case 403:
      hint = "无权限访问该模型或当前网络地区受限";
      break;
    case 404:
      hint = String("模型不存在（当前配置: ") + OPENAI_MODEL + "），请检查 OPENAI_MODEL";
      break;
    case 429:
      hint = "请求过于频繁或 OpenAI 账户余额不足，请检查 Billing 余额与限额";
      break;
    default:
      if (code >= 500 && code < 600) {
        hint = "OpenAI 服务器暂时出错，请稍后再试";
      } else {
        hint = "请求失败";
      }
      break;
  }

  String result = "[错误 " + String(code) + "] " + hint;
  if (!apiMessage.isEmpty()) {
    result += "\n  详情: " + apiMessage;
  }
  return result;
}

String askOpenAI(const String& userText) {
  if (WiFi.status() != WL_CONNECTED && !connectWiFi()) {
    return "[错误] WiFi 未连接，无法访问 OpenAI。";
  }

  WiFiClientSecure client;
  client.setCACert(OPENAI_ROOT_CA);

  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);
  if (!http.begin(client, kOpenAIEndpoint)) {
    return "[错误] 无法初始化 HTTPS 连接。";
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);

  JsonDocument req;
  req["model"] = OPENAI_MODEL;
  req["instructions"] = SYSTEM_PROMPT;  // Responses API 不会自动继承 instructions，每次都带上
  req["input"] = userText;
  req["max_output_tokens"] = kMaxOutputTokens;
  if (!previousResponseId.isEmpty()) {
    req["previous_response_id"] = previousResponseId;
  }

  String requestBody;
  serializeJson(req, requestBody);

  Serial.println("[纸巾正在思考...]");
  const int httpCode = http.POST(requestBody);
  if (httpCode <= 0) {
    const String err = http.errorToString(httpCode);
    http.end();
    return "[网络错误 " + String(httpCode) + "] " + err +
           "（请检查网络能否访问 api.openai.com，或证书是否过期）";
  }

  // getString() 会自动处理 Transfer-Encoding: chunked
  const String payload = http.getString();
  http.end();

  // 只解析需要的字段，降低内存占用
  JsonDocument filter;
  filter["id"] = true;
  filter["error"]["message"] = true;
  filter["incomplete_details"]["reason"] = true;
  filter["output"][0]["type"] = true;
  filter["output"][0]["content"][0]["type"] = true;
  filter["output"][0]["content"][0]["text"] = true;
  filter["output"][0]["content"][0]["refusal"] = true;

  JsonDocument res;
  const DeserializationError parseErr =
      deserializeJson(res, payload, DeserializationOption::Filter(filter));
  if (parseErr) {
    return String("[错误] JSON 解析失败: ") + parseErr.c_str();
  }

  if (httpCode != 200) {
    const String apiMsg = res["error"]["message"] | "";
    return formatHttpError(httpCode, apiMsg);
  }

  if (res["id"].is<const char*>()) {
    previousResponseId = res["id"].as<const char*>();
  }

  String reply;
  if (res["output"].is<JsonArray>()) {
    JsonArray outputItems = res["output"].as<JsonArray>();
    for (JsonObject item : outputItems) {
      if (item["type"] == "message" && item["content"].is<JsonArray>()) {
        JsonArray contents = item["content"].as<JsonArray>();
        for (JsonObject part : contents) {
          if (part["type"] == "output_text" && part["text"].is<const char*>()) {
            reply += part["text"].as<const char*>();
          } else if (part["type"] == "refusal" && part["refusal"].is<const char*>()) {
            reply += part["refusal"].as<const char*>();
          }
        }
      }
    }
  }

  reply.trim();
  if (reply.isEmpty()) {
    const String incomplete = res["incomplete_details"]["reason"] | "";
    if (incomplete == "max_output_tokens") {
      return "[提示] 回复达到 token 上限被截断，可在 include/secrets.h 中调大 OPENAI_MAX_OUTPUT_TOKENS。";
    }
    return "[提示] 收到成功响应，但没有可显示的文本内容（可以输入 /reset 后重试）。";
  }
  return reply;
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  // 等待 USB 串口就绪，避免刚上电时开场白被丢掉
  delay(1500);
  Serial.println();
  Serial.println("========================================");
  Serial.println("      纸巾 (ZhiJN) ESP32 AI 启动中      ");
  Serial.println("========================================");

  if (isPlaceholderConfig()) {
    Serial.println("[警告] 检测到你还没有填写真实的 WiFi / OpenAI API Key！");
    Serial.println("       请复制 include/secrets.example.h 为 include/secrets.h，");
    Serial.println("       填入真实信息后重新烧录。");
  } else {
    connectWiFi();
  }

  printHelp();
}

void loop() {
  String line;
  if (!readLineFromSerial(line)) {
    delay(10);
    return;
  }

  if (line == "/reset") {
    previousResponseId = "";
    Serial.println("[系统] 对话记忆已清空，我们重新开始吧！");
    return;
  }
  if (line == "/status") {
    printStatus();
    return;
  }
  if (line == "/help") {
    printHelp();
    return;
  }

  Serial.print("你: ");
  Serial.println(line);
  const String reply = askOpenAI(line);
  Serial.print("纸巾: ");
  Serial.println(reply);
  Serial.println();
}
