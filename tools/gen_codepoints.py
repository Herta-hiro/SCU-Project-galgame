# -*- coding: utf-8 -*-
"""生成中文码点表，供 raylib 的 LoadFontEx 预加载字形。

用法: python tools/gen_codepoints.py
输出: src/chinese_font_data.h / src/chinese_font_data.c
"""
import os

ASCII_START, ASCII_END = 0x20, 0x7E
PUNCT = ("，。！？；：、「」『』（）《》〈〉…—·～　￥％＆＃＠※★☆○●□■△▲▽▼"
         "↑↓←→↔↕◀▶◁▷±×÷°℃‰№§¶†‡•‥")

def build():
    cps = set(range(ASCII_START, ASCII_END + 1))
    for hi in range(0xB0, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                ch = bytes([hi, lo]).decode("gb2312")
            except UnicodeDecodeError:
                continue
            cps.add(ord(ch))
    for ch in PUNCT:
        cps.add(ord(ch))
    return sorted(cps)

def fmt(cps, per_line=12):
    lines = []
    for i in range(0, len(cps), per_line):
        chunk = cps[i:i + per_line]
        lines.append("    " + ", ".join("0x%04X" % c for c in chunk) + ",")
    return "\n".join(lines)

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    src = os.path.join(os.path.dirname(here), "src")
    os.makedirs(src, exist_ok=True)

    cps = build()

    h = """/* 自动生成，请勿手改。见 tools/gen_codepoints.py */
#ifndef CHINESE_FONT_DATA_H
#define CHINESE_FONT_DATA_H

extern const int  CHINESE_CODEPOINTS[];
extern const int  CHINESE_CODEPOINT_COUNT;

#endif
"""
    c = """/* 自动生成，请勿手改。见 tools/gen_codepoints.py */
#include "chinese_font_data.h"

const int CHINESE_CODEPOINTS[] = {
%s
};

const int CHINESE_CODEPOINT_COUNT = %d;
""" % (fmt(cps), len(cps))

    with open(os.path.join(src, "chinese_font_data.h"), "w", encoding="utf-8") as f:
        f.write(h)
    with open(os.path.join(src, "chinese_font_data.c"), "w", encoding="utf-8") as f:
        f.write(c)

    print("codepoints: %d" % len(cps))
    print("written to: %s" % src)

if __name__ == "__main__":
    main()
