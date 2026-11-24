#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""批量生成剧本台词配音（离线固定剧本的那一部分）。

用法（在项目根目录执行）：
    # 看会生成哪些、跳过哪些
    python tools/gen_voice.py --list

    # 只试一句，先验证链路
    python tools/gen_voice.py --only meet --limit 1 --force

    # 全量（缺什么补什么，已有的跳过）
    python tools/gen_voice.py

    # 全部重生成
    python tools/gen_voice.py --force

    # 语气对比样本（试听后调 TONE 表）
    python tools/gen_voice.py --tone-probe

产出：
    data/voice/<节点>_<行号>.ogg     引擎按「节点 + 行号」直接找文件
    data/voice/index.txt             清单：节点|行号|说话人|文件|instruct|文本

设计要点：
  * **行号必须和引擎一致**：引擎里 line_index 是本节点 lines[] 的下标，旁白也占位。
    所以这里按「节点内所有台词行（含旁白）」计数，不是只数说话的行。
  * **按文本 hash 缓存**：同一句话可能在多个节点出现（比如两句「……伞。」），
    合成一次就够，缓存命中直接复制，不再跑模型。
  * **断点续跑**：输出文件已存在且非空就跳过；中途断电重跑只会补缺。
  * 模型只加载一次，全程复用。CPU 上一句约 7~25 秒。

依赖：
    D:\\voicelab\\.venv  （Python 3.12 + CPU torch + qwen_tts + soundfile）
    ffmpeg（转 ogg；找不到就保留 wav，引擎两个都会找）
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

# ---------------------------------------------------------------- 配置

ROOT = Path(__file__).resolve().parents[1]          # 项目根目录
SCRIPT = ROOT / "data" / "main.gal"
OUT_DIR = ROOT / "data" / "voice"

MODEL_DIR = Path(os.environ.get("VOICE_MODEL", r"D:\voicelab\model"))
CACHE_DIR = Path(os.environ.get("VOICE_CACHE", r"D:\voicelab\voice_cache"))

FFMPEG_DEFAULT = Path(
    r"C:\Users\赵一霖\WorkBuddy\2026-11-04-14-03-05\tools\ffmpeg.exe"
)

# 剧本里的说话人 → 模型里的 speaker 名。
# 文芷是 Qwen3-TTS 三色绘恋微调的音色，这里借她的声线配沈知霁。
SPEAKERS = {
    "沈知霁": "wenzhi",
}

LANGUAGE = "Chinese"
OGG_QUALITY = "4"          # libvorbis -q:a，语音用 4 足够（约 64kbps）

# ---------------------------------------------------------------- 语气表
#
# 只给「停一下、放轻」才有味道的句子指定 instruct，其余用模型默认语气。
# 试听用的是 voice-samples/19~21（--tone-probe 生成），不满意就改这里重跑：
#     python tools/gen_voice.py --only <节点> --force

SOFT = "用很轻的语气说"
TIRED = "用有点疲惫的语气说"
SAD = "用伤心的语气说"
HAPPY = "用开心的语气说"
COLD = "用冷淡的语气说"

# 按**台词文本**匹配，不按行号——剧本里加一行旁白，行号就全平移了，
# 按行号写的语气表会悄悄错位。同一句文本要分节点用不同语气时才写 TONE_BY_NODE。
TONE_BY_TEXT: dict[str, str] = {
    # 第一章：那句「明天……还来吗。」是全章的钩子，放轻
    "明天……还来吗。": SOFT,
    "路上小心。": SOFT,
    # 第二章 · 日常
    "……走的时候叫我。": SOFT,
    "……十分钟。": SOFT,
    "……谢谢。": SOFT,
    "……放着就行。": SOFT,
    "……多带了一个。": SOFT,
    "……你吃了吗。": SOFT,
    "……两分钟就好。": SOFT,
    # 表白线
    "……我想一下。": SAD,
    "明天……你还来吗。": SOFT,
    "说了想一下。": COLD,
    # 靠近线 / 结局
    "……伞。": SOFT,
    "……明天也来。": SOFT,
    "这些我用不上了。你要就拿着。": TIRED,
}

# (节点, 文本) -> instruct，优先级高于 TONE_BY_TEXT。目前没有冲突条目，留作扩展。
TONE_BY_NODE: dict[tuple[str, str], str] = {}


def tone_of(node: str, text: str) -> str:
    return TONE_BY_NODE.get((node, text)) or TONE_BY_TEXT.get(text, "")

# ---------------------------------------------------------------- 剧本解析


def read_script_lines(path: Path) -> list[str]:
    raw = path.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        raw = raw[3:]
    return raw.decode("utf-8").splitlines()


def parse_lines(path: Path) -> list[dict]:
    """按引擎的解析规则（src/script.c）把台词逐行拆出来。

    返回 [{node, idx, speaker, text, src_line}]，idx 是本节点 lines[] 的下标，
    旁白也占位——和引擎的 line_index 严格对齐。
    """
    out: list[dict] = []
    node = None
    idx = 0

    for no, raw in enumerate(read_script_lines(path), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue

        if line.startswith("::"):
            node = line[2:].strip()
            idx = 0
            continue

        # 路由 / 选项 / @ 指令都不占台词位
        if line.startswith("=>") or line.startswith("->") or line.startswith("@"):
            continue

        if node is None:
            continue

        speaker, _, text = line.partition("|")
        if _ == "":
            speaker, text = "", line        # 没有竖线 = 纯旁白（引擎按 speaker="" 处理）
        else:
            speaker, text = speaker.strip(), text.strip()

        out.append({"node": node, "idx": idx, "speaker": speaker,
                    "text": text, "src_line": no})
        idx += 1

    return out


# ---------------------------------------------------------------- 工具函数


def find_ffmpeg(explicit: str | None) -> str | None:
    if explicit:
        return explicit if Path(explicit).exists() else None
    found = shutil.which("ffmpeg") or shutil.which("ffmpeg.exe")
    if found:
        return found
    if FFMPEG_DEFAULT.exists():
        return str(FFMPEG_DEFAULT)
    return None


def cache_key(text: str, speaker: str, instruct: str) -> str:
    """同一句话 + 同一语气 + 同一音色 → 同一个缓存文件。"""
    h = hashlib.sha1()
    h.update(f"{speaker}\x1f{instruct}\x1f{text}".encode("utf-8"))
    return h.hexdigest()[:16]


def out_name(node: str, idx: int, ext: str) -> str:
    return f"{node}_{idx}.{ext}"


def human(seconds: float) -> str:
    if seconds < 60:
        return f"{seconds:.0f}s"
    return f"{seconds / 60:.1f}min"


# ---------------------------------------------------------------- 计划


def build_plan(only: list[str] | None, force: bool) -> list[dict]:
    lines = parse_lines(SCRIPT)
    plan = []

    for ln in lines:
        sp = ln["speaker"]
        model_spk = SPEAKERS.get(sp)
        if not model_spk:
            continue                      # 旁白 / 玩家 / 其它角色 → 不配
        if only and not any(ln["node"] == o or ln["node"].startswith(o) for o in only):
            continue

        instruct = tone_of(ln["node"], ln["text"])
        wav_path = CACHE_DIR / f"{cache_key(ln['text'], model_spk, instruct)}.wav"
        item = dict(ln)
        item["model_speaker"] = model_spk
        item["instruct"] = instruct
        item["cache"] = wav_path
        item["done"] = False
        plan.append(item)

    # 同一段文本只排一次合成（节点不同、文本相同的情况由缓存目录兜住）
    for it in plan:
        ogg = OUT_DIR / out_name(it["node"], it["idx"], "ogg")
        wav = OUT_DIR / out_name(it["node"], it["idx"], "wav")
        it["out_ogg"] = ogg
        it["out_wav"] = wav
        it["done"] = (not force) and (
            (ogg.exists() and ogg.stat().st_size > 0)
            or (wav.exists() and wav.stat().st_size > 0)
        )

    return plan


def cmd_list(plan: list[dict], ffmpeg: str | None) -> int:
    todo = [p for p in plan if not p["done"]]
    print(f"剧本: {SCRIPT}")
    print(f"输出: {OUT_DIR}")
    print(f"缓存: {CACHE_DIR}")
    print(f"ffmpeg: {ffmpeg or '（没有，保留 wav）'}")
    print()
    print(f"{'节点':<22}{'行':<4}{'文件':<26}{'语气':<22}文本")
    print("-" * 100)
    for p in plan:
        mark = "  " if p["done"] else "* "
        tone = p["instruct"] or "-"
        print(f"{mark}{p['node']:<20}{p['idx']:<4}{out_name(p['node'], p['idx'], 'ogg'):<26}"
              f"{tone:<22}{p['text']}")
    print("-" * 100)
    print(f"合计 {len(plan)} 句：已有 {len(plan) - len(todo)}，待生成 {len(todo)}"
          f"（* 标记 = 待生成）")
    cached = sum(1 for p in todo if p["cache"].exists())
    if cached:
        print(f"其中 {cached} 句缓存命中，不用跑模型")
    return 0


# ---------------------------------------------------------------- 生成


def synth(items: list[dict], ffmpeg: str | None) -> int:
    """加载模型 → 逐句合成 → 转 ogg。返回失败数。"""
    import torch                                  # noqa: F401（先导入，报错信息更清楚）
    import soundfile as sf
    from qwen_tts import Qwen3TTSModel

    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    t0 = time.time()
    print(f"加载模型 {MODEL_DIR} …", flush=True)
    tts = Qwen3TTSModel.from_pretrained(str(MODEL_DIR), device_map="cpu",
                                        dtype=torch.float32)
    print(f"模型就绪（{time.time() - t0:.1f}s），开始合成 {len(items)} 句\n", flush=True)

    failed: list[tuple[dict, str]] = []
    t_start = time.time()

    for n, it in enumerate(items, 1):
        tag = f"[{n}/{len(items)}] {it['node']}_{it['idx']}"
        wav = it["cache"]
        cost = 0.0

        try:
            if wav.exists() and wav.stat().st_size > 0:
                print(f"{tag} 缓存命中  {it['text']}", flush=True)
            else:
                t = time.time()
                kw = dict(text=it["text"], speaker=it["model_speaker"], language=LANGUAGE)
                if it["instruct"]:
                    kw["instruct"] = it["instruct"]
                wavs, sr = tts.generate_custom_voice(**kw)
                sf.write(str(wav), wavs[0], sr)
                cost = time.time() - t
                print(f"{tag} 合成 {cost:.1f}s sr={sr}  {it['text']}", flush=True)

            # 转 ogg（缓存里的 wav 留着，重跑不用再合成）
            target = it["out_wav"]
            if ffmpeg:
                target = it["out_ogg"]
                r = subprocess.run(
                    [ffmpeg, "-y", "-loglevel", "error", "-i", str(wav),
                     "-c:a", "libvorbis", "-q:a", OGG_QUALITY, str(target)],
                    capture_output=True,
                )
                if r.returncode != 0 or not target.exists():
                    raise RuntimeError("ffmpeg 转换失败: "
                                       + r.stderr.decode("utf-8", "replace").strip()[:200])
            else:
                shutil.copyfile(wav, target)

            print(f"        -> {target.relative_to(ROOT)}  "
                  f"{target.stat().st_size / 1024:.0f} KB", flush=True)

        except Exception as e:                     # noqa: BLE001 —— 单句失败不该中断整批
            failed.append((it, str(e)))
            print(f"{tag} 失败: {e}", flush=True)

        # 剩余时间估算
        if n < len(items) and cost > 0:
            remain = (len(items) - n) * cost
            print(f"        预计剩余 {human(remain)}", flush=True)

    total = time.time() - t_start
    print(f"\n完成：成功 {len(items) - len(failed)} 句，失败 {len(failed)} 句，"
          f"用时 {human(total)}")

    if failed:
        print("\n失败清单：")
        for it, msg in failed:
            print(f"  {it['node']}_{it['idx']}  {it['text']}\n    {msg}")
        return len(failed)
    return 0


# ---------------------------------------------------------------- 索引


def write_index(plan: list[dict]) -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    path = OUT_DIR / "index.txt"
    rows = ["# 节点|行|说话人|文件|instruct|文本"]

    for p in parse_order(plan):
        ext = "ogg" if (OUT_DIR / out_name(p["node"], p["idx"], "ogg")).exists() else "wav"
        has = (OUT_DIR / out_name(p["node"], p["idx"], ext)).exists()
        rows.append("|".join([
            p["node"], str(p["idx"]), p["speaker"],
            out_name(p["node"], p["idx"], ext) if has else "（缺）",
            p["instruct"] or "-", p["text"],
        ]))

    path.write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"清单 -> {path.relative_to(ROOT)}（{len(rows) - 1} 条）")


def parse_order(plan: list[dict]) -> list[dict]:
    return sorted(plan, key=lambda p: p["src_line"])


# ---------------------------------------------------------------- 语气对比样本


def tone_probe(ffmpeg: str | None) -> int:
    """生成 3 组「同一句话、不同语气」样本，放在 voice-samples/ 里试听。"""
    samples = OUT_DIR.parent.parent / "voice-samples"
    samples.mkdir(parents=True, exist_ok=True)

    jobs = [
        ("……我想一下。", "", "19-probe-think-default"),
        ("……我想一下。", SAD, "20-probe-think-sad"),
        ("……我想一下。", SOFT, "21-probe-think-soft"),
        ("……伞。", "", "22-probe-umbrella-default"),
        ("……伞。", SOFT, "23-probe-umbrella-soft"),
        ("……明天也来。", SOFT, "24-probe-tomorrow-soft"),
    ]

    import torch
    import soundfile as sf
    from qwen_tts import Qwen3TTSModel

    tts = Qwen3TTSModel.from_pretrained(str(MODEL_DIR), device_map="cpu",
                                        dtype=torch.float32)
    failed = 0
    for text, instruct, name in jobs:
        wav = samples / f"{name}.wav"
        try:
            kw = dict(text=text, speaker="wenzhi", language=LANGUAGE)
            if instruct:
                kw["instruct"] = instruct
            wavs, sr = tts.generate_custom_voice(**kw)
            sf.write(str(wav), wavs[0], sr)

            target = wav
            if ffmpeg:
                target = samples / f"{name}.ogg"
                r = subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", str(wav),
                                    "-c:a", "libvorbis", "-q:a", OGG_QUALITY, str(target)],
                                   capture_output=True)
                if r.returncode == 0 and target.exists():
                    wav.unlink()
                else:
                    target = wav
            print(f"OK {target.name}  {instruct or '默认'}  {text}", flush=True)
        except Exception as e:                     # noqa: BLE001
            failed += 1
            print(f"失败 {name}: {e}", flush=True)

    print(f"\n样本 -> {samples}（失败 {failed}）")
    return failed


# ---------------------------------------------------------------- main


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="批量生成剧本台词配音")
    ap.add_argument("--list", action="store_true", help="只列出计划，不生成")
    ap.add_argument("--only", default="", help="只处理这些节点（逗号分隔，支持前缀）")
    ap.add_argument("--limit", type=int, default=0, help="最多生成 N 句（试跑用）")
    ap.add_argument("--force", action="store_true", help="已有的也重生成")
    ap.add_argument("--ffmpeg", default=None, help="ffmpeg 路径（默认自动找）")
    ap.add_argument("--tone-probe", action="store_true", help="生成语气对比样本")
    args = ap.parse_args(argv)

    if not SCRIPT.exists():
        print(f"找不到剧本：{SCRIPT}")
        return 1
    if not MODEL_DIR.exists():
        print(f"找不到模型：{MODEL_DIR}")
        return 1

    ffmpeg = find_ffmpeg(args.ffmpeg)

    if args.tone_probe:
        return 1 if tone_probe(ffmpeg) else 0

    only = [s.strip() for s in args.only.split(",") if s.strip()] or None
    plan = build_plan(only, args.force)

    if not plan:
        print("没有匹配的台词。检查 --only 或剧本里有没有 SPEAKERS 里的说话人。")
        return 1

    if args.list:
        return cmd_list(plan, ffmpeg)

    todo = [p for p in plan if not p["done"]]
    if not todo:
        print(f"{len(plan)} 句都已经生成过了。要重生成加 --force。")
        write_index(build_plan(None, False))
        return 0

    if args.limit > 0:
        todo = todo[:args.limit]

    rc = 1 if synth(todo, ffmpeg) else 0
    # 索引永远写全量：--only / --limit 只是少跑几句，清单不该跟着缩水
    write_index(build_plan(None, False))
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
