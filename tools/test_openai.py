#!/usr/bin/env python3
"""
纸巾 (ZhiJN) OpenAI Responses API 连通性测试脚本（纯 Python 标准库，零额外依赖）

烧录 ESP32 之前，先在电脑上用完全相同的接口和参数验证你的 API Key 和模型名是否可用，
把「账号/Key/模型问题」和「ESP32 硬件/WiFi 问题」彻底分开排查。

用法：
    OPENAI_API_KEY="sk-..." python3 tools/test_openai.py
    python3 tools/test_openai.py --from-secrets
    OPENAI_API_KEY="sk-..." python3 tools/test_openai.py --model gpt-5-nano
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

ENDPOINT = "https://api.openai.com/v1/responses"
DEFAULT_SYSTEM_PROMPT = (
    "你是一个放在桌面上的小机器人，名字叫纸巾。"
    "你性格友好、有点机灵，回答简短自然（尽量不超过3句话），默认使用中文。"
)


def read_define_from_header(path: Path, name: str) -> str | None:
    if not path.exists():
        return None
    text = path.read_text(encoding="utf-8", errors="ignore")
    m = re.search(rf'^\s*#define\s+{re.escape(name)}\s+"([^"]*)"', text, re.M)
    return m.group(1) if m else None


def call_responses_api(
    api_key: str,
    model: str,
    instructions: str,
    user_text: str,
    previous_response_id: str | None = None,
    max_output_tokens: int = 600,
) -> tuple[str, str]:
    payload: dict[str, object] = {
        "model": model,
        "instructions": instructions,
        "input": user_text,
        "max_output_tokens": max_output_tokens,
    }
    if previous_response_id:
        payload["previous_response_id"] = previous_response_id

    req = urllib.request.Request(
        ENDPOINT,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {api_key}",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            data = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        raw = exc.read().decode("utf-8", errors="replace")
        try:
            msg = json.loads(raw).get("error", {}).get("message", raw)
        except json.JSONDecodeError:
            msg = raw
        print(f"[HTTP 错误 {exc.code}] {msg}", file=sys.stderr)
        sys.exit(1)
    except urllib.error.URLError as exc:
        print(f"[网络错误] 无法连接到 {ENDPOINT}: {exc.reason}", file=sys.stderr)
        sys.exit(1)

    resp_id = data.get("id", "")
    parts: list[str] = []
    for item in data.get("output", []):
        if item.get("type") == "message":
            for content in item.get("content", []):
                if content.get("type") == "output_text" and content.get("text"):
                    parts.append(content["text"])
                elif content.get("type") == "refusal" and content.get("refusal"):
                    parts.append(content["refusal"])

    reply = "".join(parts).strip()
    if not reply:
        incomplete = (data.get("incomplete_details") or {}).get("reason")
        print(f"[警告] 未提取到文本回复（incomplete_details={incomplete}）", file=sys.stderr)
        print(json.dumps(data, ensure_ascii=False, indent=2), file=sys.stderr)
        sys.exit(1)
    return resp_id, reply


def main() -> None:
    repo_root = Path(__file__).resolve().parent.parent
    secrets_h = repo_root / "include" / "secrets.h"

    parser = argparse.ArgumentParser(description="测试 OpenAI Responses API 连通性与多轮对话")
    parser.add_argument(
        "--model",
        default=os.environ.get("OPENAI_MODEL")
        or read_define_from_header(secrets_h, "OPENAI_MODEL")
        or "gpt-5-nano",
        help="要测试的模型 ID（默认读取 OPENAI_MODEL 环境变量或 include/secrets.h）",
    )
    parser.add_argument("--prompt", default="你好，介绍一下你自己吧！", help="第一轮测试消息")
    parser.add_argument(
        "--from-secrets",
        action="store_true",
        help="从 include/secrets.h 读取 OPENAI_API_KEY（避免在终端历史里暴露 Key）",
    )
    args = parser.parse_args()

    api_key = os.environ.get("OPENAI_API_KEY", "").strip()
    if not api_key and args.from_secrets:
        api_key = (read_define_from_header(secrets_h, "OPENAI_API_KEY") or "").strip()
    if not api_key or "..." in api_key:
        print("错误：未找到有效的 OPENAI_API_KEY。请使用以下任一方式运行：", file=sys.stderr)
        print('  OPENAI_API_KEY="sk-..." python3 tools/test_openai.py', file=sys.stderr)
        print("  python3 tools/test_openai.py --from-secrets", file=sys.stderr)
        sys.exit(1)

    instructions = (
        read_define_from_header(secrets_h, "SYSTEM_PROMPT") or DEFAULT_SYSTEM_PROMPT
    )

    print(f"正在测试 OpenAI Responses API（模型: {args.model}）...\n")

    print(f"[第 1 轮] 你: {args.prompt}")
    resp_id_1, reply_1 = call_responses_api(api_key, args.model, instructions, args.prompt)
    print(f"[第 1 轮] 纸巾: {reply_1}")
    print(f"          (response_id = {resp_id_1})\n")

    turn2 = "记住，我最喜欢的颜色是天蓝色。"
    print(f"[第 2 轮] 你: {turn2}")
    resp_id_2, reply_2 = call_responses_api(
        api_key, args.model, instructions, turn2, previous_response_id=resp_id_1
    )
    print(f"[第 2 轮] 纸巾: {reply_2}\n")

    turn3 = "我刚才说我最喜欢什么颜色？"
    print(f"[第 3 轮] 你: {turn3}")
    _, reply_3 = call_responses_api(
        api_key, args.model, instructions, turn3, previous_response_id=resp_id_2
    )
    print(f"[第 3 轮] 纸巾: {reply_3}\n")

    print("测试通过！API Key、模型名与多轮对话链路均正常，可以放心烧录到 ESP32。")


if __name__ == "__main__":
    main()
