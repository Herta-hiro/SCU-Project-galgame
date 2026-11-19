#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""剧本 / 角色卡校验器。

用法（在 gal 目录下）：
    python tools/check_script.py
    python tools/check_script.py data/main.gal data/characters.gal

检查的是引擎里写死的那些上限：节点数、每个节点的行数、每行字节数、
选项数、id 长度……超了不会崩，但会被静默截断或漏读，所以先在这里报出来。
退出码 0 = 没有错误（可能有警告），1 = 有错误。
"""

import re
import sys
from pathlib import Path

# ---- 与 src/game.h / src/character.h 保持一致 ----
LIM = {
    "MAX_NODES": 96,
    "MAX_LINES": 16,
    "MAX_TEXT": 512,       # 台词缓冲，最多 511 字节
    "MAX_CHOICES": 4,
    "MAX_ROUTES": 40,
    "MAX_ID": 32,          # 节点 id，最多 31 字节
    "SPEAKER": 40,         # 说话人，最多 39 字节
    "CHOICE_TEXT": 80,     # 选项文字，最多 79 字节
    "CHOICE_PROMPT": 160,
    "CHOICE_SCENE": 160,
}

CARD_LIM = {
    "name": 40,
    "persona": 1600,
    "rules": 1600,
    "scene": 600,
    "line": 600,
    "stage": 300,
    "offline": 600,
    "call": 40,
    "events": 7000,
}

CARD_KEYS = {
    "name", "persona", "rules", "scene", "line", "events",
    "player_call", "her_call",
} | {f"stage{i}" for i in range(5)} | {f"offline{i}" for i in range(5)}

STAGE_TONE_KEYS = {f"stage{i}" for i in range(5)}


class Reporter:
    def __init__(self):
        self.errors = []
        self.warns = []
        self.notes = []

    def err(self, path, line, msg):
        self.errors.append(f"{path}:{line}: 错误: {msg}")

    def warn(self, path, line, msg):
        self.warns.append(f"{path}:{line}: 警告: {msg}")

    def note(self, msg):
        self.notes.append(f"提示: {msg}")

    def dump(self):
        for n in self.notes:
            print(n)
        for w in self.warns:
            print(w)
        for e in self.errors:
            print(e)
        print(f"\n合计: {len(self.errors)} 个错误, {len(self.warns)} 个警告")
        return 1 if self.errors else 0


def read_lines(path):
    raw = Path(path).read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        raw = raw[3:]
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as e:
        raise SystemExit(f"{path}: 不是 UTF-8，第 {e.start} 字节处解码失败")
    return text.splitlines()


def blen(s):
    return len(s.encode("utf-8"))


# ---------------- 角色卡 ----------------

def check_cards(path, rep):
    cards = {}
    cur = None
    seen = set()

    for no, raw in enumerate(read_lines(path), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue

        if line.startswith("::"):
            cid = line[2:].strip()
            if not cid:
                rep.err(path, no, "角色卡 id 为空")
                cur = None
                continue
            if blen(cid) >= LIM["MAX_ID"]:
                rep.err(path, no, f"角色 id「{cid}」{blen(cid)} 字节，上限 {LIM['MAX_ID'] - 1}")
            if cid in seen:
                rep.warn(path, no, f"角色 id「{cid}」重复，后一张会覆盖查找结果")
            seen.add(cid)
            cur = {"_line": no, "_fields": {}}
            cards[cid] = cur
            continue

        if cur is None:
            rep.err(path, no, "字段写在第一张 :: 角色卡之前，会被忽略")
            continue

        parts = re.split(r"[ \t=]", line, maxsplit=1)
        if len(parts) != 2:
            rep.err(path, no, f"字段「{line}」没有值")
            continue
        key, val = parts[0].strip(), parts[1].strip()

        if key not in CARD_KEYS:
            rep.warn(path, no, f"未知字段「{key}」，不会被读取")
            continue

        bucket = cur["_fields"].setdefault(key, [])
        bucket.append(val)
        joined = "\n".join(bucket) if key in {"persona", "rules", "scene", "line"} or \
            key in STAGE_TONE_KEYS or key.startswith("offline") else " ".join(bucket)

        if key in {"name", "player_call", "her_call"}:
            limit = CARD_LIM["call"]
        elif key in STAGE_TONE_KEYS:
            limit = CARD_LIM["stage"]
        elif key.startswith("offline"):
            limit = CARD_LIM["offline"]
        else:
            limit = CARD_LIM[key]

        if blen(joined) >= limit:
            rep.err(path, no, f"「{key}」累计 {blen(joined)} 字节，上限 {limit - 1}，会被截断")

    for cid, c in cards.items():
        f = c["_fields"]
        if "name" not in f:
            rep.warn(path, c["_line"], f"角色「{cid}」没有 name，界面会显示 id")
        if "persona" not in f:
            rep.warn(path, c["_line"], f"角色「{cid}」没有 persona，人设是空的")
        for i in range(5):
            if f"stage{i}" not in f:
                rep.warn(path, c["_line"], f"角色「{cid}」缺 stage{i}（阶段 {i} 的语气）")
    return cards


# ---------------- 剧本 ----------------

def check_script(path, cards, rep):
    nodes = {}
    order = []
    cur = None
    total_lines = 0

    for no, raw in enumerate(read_lines(path), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue

        if line.startswith("::"):
            nid = line[2:].strip()
            if not nid:
                rep.err(path, no, "节点 id 为空")
                cur = None
                continue
            if blen(nid) >= LIM["MAX_ID"]:
                rep.err(path, no, f"节点 id「{nid}」{blen(nid)} 字节，上限 {LIM['MAX_ID'] - 1}")
            if nid in nodes:
                rep.err(path, no, f"节点 id「{nid}」重复，前面那个会被后面覆盖")
            cur = {"id": nid, "line": no, "lines": 0, "choices": [], "routes": [], "next": None}
            nodes[nid] = cur
            order.append(nid)
            if len(order) > LIM["MAX_NODES"]:
                rep.err(path, no, f"节点数超过 {LIM['MAX_NODES']}，后面的节点读不进来")
            continue

        if cur is None:
            rep.err(path, no, "内容写在第一个 :: 节点之前，会被忽略")
            continue

        if line.startswith("@loc"):
            name = line[4:].strip()
            cur["loc"] = name
            if blen(name) >= 64:
                rep.err(path, no, f"地点「{name}」超过 63 字节，会被截断")
            continue

        if line.startswith("@event"):
            name = line[6:].strip()
            cur["event"] = name
            if blen(name) >= 256:
                rep.err(path, no, f"事件描述「{name}」超过 255 字节，会被截断")
            continue

        if line.startswith("@toast"):
            txt = line[6:].strip()
            cur["toast"] = txt
            if blen(txt) >= 64:
                rep.err(path, no, f"@toast「{txt}」超过 63 字节，会被截断")
            continue

        if line.startswith("@once"):
            spec = line[5:].strip()
            if not re.fullmatch(r"[A-Za-z](1[0-5]|[1-9])", spec):
                rep.err(path, no, f"@once 应写成 A1 / B3 / G6 这种（字母 A-Z + 1-15），实际是「{spec}」")
            cur.setdefault("once", []).append(spec)
            continue

        if line.startswith("@offline"):
            txt = line[8:].strip()
            cur.setdefault("offline", []).append(txt)
            continue

        if line.startswith("@char"):
            spec = line[5:].strip()
            cur["char"] = spec
            if blen(spec) >= 48:
                rep.err(path, no, f"立绘写法「{spec}」超过 47 字节，会被截断")
            continue

        if line.startswith("@bg"):
            name = line[3:].strip()
            cur["bg"] = name
            if not name:
                rep.err(path, no, "@bg 后面没写背景名")
            elif blen(name) >= 32:
                rep.err(path, no, f"背景名「{name}」超过 31 字节，会被截断")
            continue

        if line.startswith("=>"):
            body = line[2:].strip()
            parts = body.split(None, 1)
            if len(parts) < 2:
                rep.err(path, no, "路由写法应为「=> aff>=46 目标节点」")
                continue
            cur["routes"].append({"line": no, "cond": parts[0].strip(), "target": parts[1].strip()})
            continue

        if line.startswith("->"):
            body = line[2:].strip()
            fields = body.split("|")
            if len(fields) < 2:
                rep.err(path, no, "选项缺目标：应写成「-> 文字|目标节点」")
                continue
            if len(fields) > 6:
                rep.err(path, no, f"选项有 {len(fields)} 列，引擎只认 6 列（多出来的会算进好感度一栏）")
            cur["choices"].append({"line": no, "fields": fields})
            continue

        total_lines += 1
        cur["lines"] += 1
        if cur["lines"] > LIM["MAX_LINES"]:
            rep.err(path, no, f"节点「{cur['id']}」超过 {LIM['MAX_LINES']} 行，多出来的行会被丢弃")

        if "|" in line:
            speaker, text = line.split("|", 1)
            if blen(speaker) >= LIM["SPEAKER"]:
                rep.err(path, no, f"说话人「{speaker}」{blen(speaker)} 字节，上限 {LIM['SPEAKER'] - 1}")
        else:
            text = line

        if blen(text) >= LIM["MAX_TEXT"]:
            rep.err(path, no, f"台词 {blen(text)} 字节，上限 {LIM['MAX_TEXT'] - 1}，会被截断")

    # 选项引用与长度
    for nid, n in nodes.items():
        if len(n["choices"]) > LIM["MAX_CHOICES"]:
            rep.err(path, n["line"],
                    f"节点「{nid}」有 {len(n['choices'])} 个选项，上限 {LIM['MAX_CHOICES']}，多的会被丢弃")

        for ch in n["choices"]:
            f = ch["fields"]
            text = f[0]
            target = f[1].strip()
            if blen(text) >= LIM["CHOICE_TEXT"]:
                rep.err(path, ch["line"],
                        f"选项文字「{text}」{blen(text)} 字节，上限 {LIM['CHOICE_TEXT'] - 1}")
            if blen(target) >= LIM["MAX_ID"] and not target.startswith("@"):
                rep.err(path, ch["line"], f"目标 id「{target}」{blen(target)} 字节，上限 {LIM['MAX_ID'] - 1}")

            if len(f) > 2 and blen(f[2]) >= LIM["CHOICE_PROMPT"]:
                rep.err(path, ch["line"], "第三列（玩家这句话）超长，会被截断")
            if len(f) > 3 and blen(f[3]) >= LIM["CHOICE_SCENE"]:
                rep.err(path, ch["line"], "第四列（场景）超长，会被截断")
            if len(f) > 4:
                delta = f[4].strip()
                if not re.fullmatch(r"[+-]?\d+", delta):
                    rep.err(path, ch["line"], f"第五列好感度「{delta}」不是整数，会被当成 0")
                elif abs(int(delta)) > 20:
                    rep.warn(path, ch["line"], f"好感度一次变动 {delta}，建议不超过 ±20")

            if len(f) > 5:
                cond = f[5].strip()
                if cond and not re.fullmatch(r"(aff)?\s*(>=|<=|==|=|>|<)\s*\d+", cond):
                    rep.err(path, ch["line"],
                            f"第六列条件「{cond}」写法不对，应写成 aff>=46 / aff<=20 / aff==0")
                else:
                    m2 = re.search(r"(>=|<=|==|=|>|<)\s*(\d+)", cond)
                    if m2 and not (0 <= int(m2.group(2)) <= 100):
                        rep.warn(path, ch["line"],
                                 f"好感度条件 {cond} 超出 0-100，这个选项可能永远不出现")

            if target.startswith("@"):
                m = re.fullmatch(r"@([^:]+)(?::(.+))?", target)
                if not m:
                    rep.err(path, ch["line"], f"AI 分支写法应为 @角色id:返回节点，实际是「{target}」")
                    continue
                char_id, ret = m.group(1), m.group(2)
                if cards is not None and char_id not in cards:
                    rep.err(path, ch["line"], f"AI 分支引用了不存在的角色「{char_id}」")
                if not ret:
                    rep.warn(path, ch["line"], f"AI 分支「{target}」没写返回节点，会留在原地")
                elif ret not in nodes:
                    rep.err(path, ch["line"], f"AI 分支返回节点「{ret}」不存在")
            elif target not in nodes:
                rep.err(path, ch["line"], f"选项指向的节点「{target}」不存在")

    # 自动路由：条数、条件写法、目标存在
    for nid, n in nodes.items():
        rs = n.get("routes", [])
        if len(rs) > LIM["MAX_ROUTES"]:
            rep.err(path, n["line"],
                    f"节点「{nid}」有 {len(rs)} 条路由，上限 {LIM['MAX_ROUTES']}")
        for r in rs:
            cond, tgt = r["cond"], r["target"]
            if not re.fullmatch(r"(aff)?\s*(>=|<=|==|=|>|<)\s*\d+", cond):
                rep.err(path, r["line"], f"路由条件「{cond}」写法不对，应写成 aff>=46")
            if tgt not in nodes:
                rep.err(path, r["line"], f"路由指向的节点「{tgt}」不存在")
            elif tgt == nid:
                rep.warn(path, r["line"], f"路由指回自己（{nid}），会直接停在原地")

    # @offline 兜底台词长度
    for nid, n in nodes.items():
        of = "\n".join(n.get("offline", []))
        if of and blen(of) >= 600:
            rep.err(path, n["line"],
                    f"节点「{nid}」的 @offline 累计 {blen(of)} 字节，上限 599，会被截断")

    # 背景图是否存在
    bgdir = Path(path).resolve().parent / "backgrounds"
    for nid, n in nodes.items():
        bg = n.get("bg")
        if not bg:
            continue
        if not ((bgdir / f"{bg}.png").exists() or (bgdir / f"{bg}.jpg").exists()):
            rep.warn(path, n["line"],
                     f"节点「{nid}」的背景「{bg}」在 {bgdir} 里找不到，会退回渐变底")

    # 立绘是否存在（缺图不算错：没图就不画人，等作者自己放）
    sprdir = Path(path).resolve().parent / "sprites"
    missing = {}
    for nid, n in nodes.items():
        spec = n.get("char", "")
        if not spec or spec == "off":
            continue
        cid, _, expr = spec.partition(":")
        cands = [f"{cid}_{expr}" if expr else "", f"{cid}", f"{cid}_normal"]
        if not any((sprdir / f"{c}.png").exists() for c in cands if c):
            missing.setdefault(spec, []).append(nid)
    if missing:
        specs = "、".join(sorted(missing))
        rep.note(f"{len(missing)} 种立绘还没有图（{specs}），对应节点暂不显示人物；"
                 f"把 png 放进 {sprdir} 即生效")

    # 可达性：从第一个节点开始，沿选项走
    if order:
        seen = set()
        stack = [order[0]]
        while stack:
            nid = stack.pop()
            if nid in seen or nid not in nodes:
                continue
            seen.add(nid)
            for r in nodes[nid].get("routes", []):
                stack.append(r["target"])
            for ch in nodes[nid]["choices"]:
                tgt = ch["fields"][1].strip()
                if tgt.startswith("@"):
                    ret = tgt.split(":", 1)[1] if ":" in tgt else ""
                    if ret:
                        stack.append(ret)
                else:
                    stack.append(tgt)
        for nid in order:
            if nid not in seen:
                rep.warn(path, nodes[nid]["line"], f"节点「{nid}」走不到，检查是不是漏了跳转")

    return nodes, total_lines


def main(argv):
    root = Path.cwd()
    script = Path(argv[1]) if len(argv) > 1 else root / "data" / "main.gal"
    cards_path = Path(argv[2]) if len(argv) > 2 else root / "data" / "characters.gal"

    rep = Reporter()

    if not script.exists():
        raise SystemExit(f"找不到剧本：{script}")
    if not cards_path.exists():
        print(f"提示：{cards_path} 不存在，跳过角色卡检查（游戏会用内置默认人设）")
        cards = None
    else:
        cards = check_cards(str(cards_path), rep)

    nodes, total_lines = check_script(str(script), cards, rep)

    print(f"剧本: {script}  节点 {len(nodes)} 个，台词/旁白 {total_lines} 行")
    print(f"角色卡: {cards_path}  角色 {len(cards) if cards else 0} 张")
    print("上限:", ", ".join(f"{k}={v}" for k, v in LIM.items()))
    print()
    return rep.dump()


if __name__ == "__main__":
    sys.exit(main(sys.argv))