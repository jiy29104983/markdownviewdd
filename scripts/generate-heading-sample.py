#!/usr/bin/env python3
"""Write the reproducible REQ-003 sample; default output belongs under build/."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("output", nargs="?", default="build/req003-headings-500.md")
parser.add_argument("--count", type=int, default=500)
args = parser.parse_args()
if args.count < 1:
    parser.error("--count must be positive")
path = Path(args.output)
path.parent.mkdir(parents=True, exist_ok=True)
text = "引言：标题大纲、键盘与双侧定位样本。\n\n"
for index in range(args.count):
    level = index % 6 + 1
    text += "#" * level + f" 章节 {index + 1} · 中文 😀\n\n"
    text += "这里是用于检查源码与预览位置的正文。\n\n" * 3
path.write_text(text, encoding="utf-8")
print(f"{path}: {args.count} headings, {len(text.encode('utf-8'))} bytes")
