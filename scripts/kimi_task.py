#!/usr/bin/env python3
"""
kimi_task.py - Delegate a task to Kimi K3 and print the reply.

Lets the orchestrating agent hand off token-heavy or parallelisable work
(bulk file review, boilerplate generation, image critique) without spending
its own context on it.

The key is read from the KIMI_API_KEY environment variable and is NEVER
written to disk. Do not pass it on the command line - it would land in shell
history and in this repo's process listings.

    $env:KIMI_API_KEY = "sk-..."          # PowerShell, current session only
    python scripts/kimi_task.py --prompt "..." [--image path.png] [--system "..."]
    python scripts/kimi_task.py --file task.md

Endpoint: Kimi's Anthropic-compatible coding API, so the request shape matches
the Messages API (x-api-key + anthropic-version headers, content blocks).
"""

import argparse
import base64
import json
import mimetypes
import os
import sys
import urllib.error
import urllib.request

BASE_URL = os.environ.get("KIMI_BASE_URL", "https://api.kimi.com/coding")
MODEL = os.environ.get("KIMI_MODEL", "k3-256k")
TIMEOUT = 180


def build_content(prompt, image_paths):
    """Anthropic-style content blocks: images first, then the prompt."""
    blocks = []
    for path in image_paths or []:
        mime = mimetypes.guess_type(path)[0] or "image/png"
        with open(path, "rb") as fh:
            data = base64.standard_b64encode(fh.read()).decode("ascii")
        blocks.append({
            "type": "image",
            "source": {"type": "base64", "media_type": mime, "data": data},
        })
    blocks.append({"type": "text", "text": prompt})
    return blocks


def ask(prompt, images=None, system=None, max_tokens=4096):
    key = os.environ.get("KIMI_API_KEY")
    if not key:
        raise SystemExit(
            "kimi_task: KIMI_API_KEY is not set.\n"
            "  PowerShell:  $env:KIMI_API_KEY = 'sk-...'\n"
            "Get a key from the Kimi Code console; the full value is shown\n"
            "only once at creation.")

    payload = {
        "model": MODEL,
        "max_tokens": max_tokens,
        "messages": [{"role": "user", "content": build_content(prompt, images)}],
    }
    if system:
        payload["system"] = system

    req = urllib.request.Request(
        f"{BASE_URL}/v1/messages",
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "content-type": "application/json",
            "x-api-key": key,
            "anthropic-version": "2023-06-01",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
            body = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        detail = e.read().decode("utf-8", "replace")[:600]
        raise SystemExit(f"kimi_task: HTTP {e.code}\n{detail}")
    except urllib.error.URLError as e:
        raise SystemExit(f"kimi_task: cannot reach {BASE_URL}: {e.reason}")

    parts = [b.get("text", "") for b in body.get("content", [])
             if b.get("type") == "text"]
    usage = body.get("usage", {})
    return "\n".join(parts).strip(), usage


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prompt", help="task text")
    ap.add_argument("--file", help="read the task text from a file")
    ap.add_argument("--image", action="append", help="attach an image; repeatable")
    ap.add_argument("--system", help="system prompt")
    ap.add_argument("--max-tokens", type=int, default=4096)
    ap.add_argument("--out", help="write the reply here instead of stdout")
    args = ap.parse_args()

    if args.file:
        prompt = open(args.file, encoding="utf-8").read()
    elif args.prompt:
        prompt = args.prompt
    else:
        raise SystemExit("kimi_task: need --prompt or --file")

    text, usage = ask(prompt, args.image, args.system, args.max_tokens)

    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text)
        print(f"kimi_task: wrote {len(text)} chars to {args.out}")
    else:
        print(text)

    if usage:
        print(f"\n[kimi usage: in={usage.get('input_tokens')} "
              f"out={usage.get('output_tokens')}]", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
