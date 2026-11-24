#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把真实的 CC0 雨声录音处理成引擎用的 60 秒无缝循环。

用法（在项目根目录执行）：
    python tools/import_rain.py                 # 下载并重新生成 data/audio/bgm_rain.ogg
    python tools/import_rain.py --no-download   # 用本地缓存，不联网
    python tools/import_rain.py --seconds 45    # 改成别的循环长度

为什么不继续用程序合成（tools/gen_audio.py 的 make_rain）：
    合成雨声能做到"像雨"，但做不到"是雨"。差别在 2kHz 以上：真实雨滴撞击
    录音在 8~16kHz 有 20% 左右的能量，那是雨点打在玻璃/书页上的高频碎响；
    合成版这里基本是空的，听感就是一条闷闷的"沙——"，所以被听成噪音。
    现在改成真实录音 + 无缝化处理，合成版只保留给没有网络的场景兜底。

素材来源（CC0 1.0 公共领域，可商用、可修改、无需署名，这里仍然署名）：
    ferrosintesis-samples-rain / rain_loop.flac
    https://github.com/0x4D44/ferrosintesis/tree/main/crates/ferrosintesis-samples-rain
    仓库所有者 2017 年录制，裁剪 00:31–00:36 的干净窗口，单声道 44.1kHz。

处理流程：
    1. 高通 55Hz     去掉远处的车流隆隆
    2. 修循环点      源文件的 crossfade 做得不彻底（首尾差 0.16），重做 0.35s
    3. 铺到目标长度  4.6s 的素材循环 13 次会有明显的周期感，所以叠两层：
                       主层  原速循环
                       副层  随机偏移 + 1.024x 变速，音量 0.55
                     两层周期不同，最小公倍数远大于 60 秒，听不出重复
    4. 阵风 LFO      0.07Hz 的慢起伏，幅度 ±10%
    5. 归一化        peak 0.16（和原来的合成版一致，音量在主循环里调）
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import urllib.request
from math import gcd
from pathlib import Path

import numpy as np
import soundfile as sf

try:
    from scipy.signal import resample_poly
except ImportError:                 # 系统 Python 可能没有 scipy，退回线性插值
    resample_poly = None

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "data" / "audio" / "bgm_rain.ogg"
CACHE = Path(os.environ.get("RAIN_CACHE", r"D:\voicelab\rain_src"))
FFMPEG_DEFAULT = Path(r"C:\Users\赵一霖\WorkBuddy\2026-11-04-14-03-05\tools\ffmpeg.exe")

SOURCE_URL = (
    "https://raw.githubusercontent.com/0x4D44/ferrosintesis/main/"
    "crates/ferrosintesis-samples-rain/samples/rain_loop.flac"
)
SOURCE_SHA256 = ""          # 首次下载后填；留空则跳过校验

HIGHPASS_HZ = 55.0
LOOP_XFADE = 0.35
GHOST_RATE = 1.024          # 副层相对主层的速率
GHOST_GAIN = 0.55
PEAK_TARGET = 0.16
OUT_SR = 48000              # 引擎音频设备的采样率，同采样率可以直通


# ---------------------------------------------------------------- 下载


def download(dest: Path) -> Path:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists() and dest.stat().st_size > 4096:
        print(f"用本地缓存: {dest}  {dest.stat().st_size / 1024:.0f} KB")
        return dest

    print(f"下载 {SOURCE_URL}")
    req = urllib.request.Request(SOURCE_URL, headers={"User-Agent": "galgame-rain-import"})
    with urllib.request.urlopen(req, timeout=120) as r, open(dest, "wb") as f:
        shutil.copyfileobj(r, f)

    print(f"已保存: {dest}  {dest.stat().st_size / 1024:.0f} KB")
    return dest


def verify(path: Path) -> None:
    if SOURCE_SHA256:
        import hashlib
        h = hashlib.sha256(path.read_bytes()).hexdigest()
        if h != SOURCE_SHA256:
            raise SystemExit(f"校验失败: {h} != {SOURCE_SHA256}")
    head = path.read_bytes()[:4]
    if head != b"fLaC":
        raise SystemExit(f"{path} 不是 FLAC（头 {head!r}）")


# ---------------------------------------------------------------- 信号处理


def highpass(x: np.ndarray, cutoff: float, sr: int, order: int = 3) -> np.ndarray:
    """频域高通。比 onepole 级联干净，不会在截止点附近留下拖尾。"""
    spec = np.fft.rfft(x)
    freq = np.fft.rfftfreq(len(x), 1.0 / sr)
    # 高通的幅度响应：比 1 - 低通更陡，且没有振铃
    resp = (freq / cutoff) ** (2 * order) / (1.0 + (freq / cutoff) ** (2 * order))
    return np.fft.irfft(spec * resp, n=len(x))


def make_seamless(x: np.ndarray, sr: int, xfade: float) -> np.ndarray:
    """把首尾接起来：尾部 xfade 秒交叉淡入到开头，然后切掉尾巴。

    源文件本身做过 crossfade，但首尾样本差 0.16，直接循环会有咔哒声——
    这里重做一次，保证 loop 点的样本连续。
    """
    nf = int(sr * xfade)
    head = x[:nf].copy()
    tail = x[-nf:].copy()
    w = np.linspace(0.0, 1.0, nf) ** 0.5      # 开方曲线：能量守恒更接近，避免中间凹下去

    out = x[:-nf].copy()
    out[:nf] = head * w + tail * (1.0 - w)
    return out


def varispeed(x: np.ndarray, rate: float, n_out: int) -> np.ndarray:
    """线性插值变速：rate > 1 变快（音高升高）。只用来做副层的相位漂移。"""
    idx = np.arange(n_out) * rate
    idx = idx % len(x)
    i = np.floor(idx).astype(int)
    f = idx - i
    j = (i + 1) % len(x)
    return x[i] * (1.0 - f) + x[j] * f


def tile(x: np.ndarray, n: int) -> np.ndarray:
    """把循环素材平铺到 n 个样本。"""
    reps = int(np.ceil(n / len(x)))
    return np.tile(x, reps)[:n]


def build(src: np.ndarray, sr: int, seconds: float) -> tuple[np.ndarray, int]:
    n = int(sr * seconds)

    # 1) 去隆隆
    src = highpass(src, HIGHPASS_HZ, sr)
    src = src - src.mean()

    # 2) 修循环点
    base = make_seamless(src, sr, LOOP_XFADE)

    # 3) 主层：原速平铺
    main = tile(base, n)

    # 4) 副层：随机偏移 + 变速。周期 = len(base) / rate，和主层错开，
    #    两层的最小公倍数远大于 60 秒，所以听不出"每 4.6 秒重复一次"。
    rng = np.random.default_rng(20261124)
    offset = int(rng.integers(0, len(base)))
    ghost = varispeed(np.roll(base, offset), GHOST_RATE, n) * GHOST_GAIN
    ghost = tile(ghost, n)

    sig = main + ghost

    # 5) 阵风 LFO
    t = np.arange(n) / sr
    gust = 1.0 + 0.10 * np.sin(2 * np.pi * 0.07 * t) \
                + 0.04 * np.sin(2 * np.pi * 0.019 * t + 2.1)
    sig *= gust

    # 6) 归一化到统一峰值（真正的音量由 audio.c 里的 BGM_VOLUME 管）
    peak = np.abs(sig).max()
    if peak > 1e-9:
        sig *= PEAK_TARGET / peak

    # 7) 闭合循环：把尾巴的 nf 段淡入到开头，然后**切掉尾巴**。
    #
    #    注意顺序：先切再混。之前写成「混开头、留尾巴」，循环点变成
    #    sig[-1] → sig[-nf]，中间隔了 nf 个样本，跳变 0.04（峰值的 26%），
    #    听上去就是每圈一下轻咔哒。
    #    正确做法保证 out[-1] = sig[n-nf-1] 后面正好接 out[0] = sig[n-nf]。
    sig = close_loop(sig, sr, 0.05)

    # 8) 重采样到 48kHz：引擎的音频设备就是 48kHz，同采样率可以直通，
    #    省掉 miniaudio 的一次重采样（44.1→48 会在 22kHz 以上留镜像）。
    if sr != OUT_SR:
        if resample_poly is not None:
            g = gcd(OUT_SR, sr)
            sig = resample_poly(sig, OUT_SR // g, sr // g)
        else:
            # 没有 scipy：线性插值够用（雨声宽带，轻微镜像听不出来）
            n_out = int(round(len(sig) * OUT_SR / sr))
            idx = np.linspace(0, len(sig) - 1, n_out)
            i = np.floor(idx).astype(int)
            f = idx - i
            j = np.minimum(i + 1, len(sig) - 1)
            sig = sig[i] * (1 - f) + sig[j] * f
        sr = OUT_SR
        # 重采样后峰值会略变，再归一次
        peak = np.abs(sig).max()
        if peak > 1e-9:
            sig *= PEAK_TARGET / peak

    return sig, sr


def close_loop(sig: np.ndarray, sr: int, xfade: float) -> np.ndarray:
    """标准循环交叉淡化：尾部淡入到开头并丢弃尾部。

    保证 out[-1] 与 out[0] 在信号上是相邻样本，循环播放听不出接缝。
    """
    nf = int(sr * xfade)
    if nf <= 0 or nf * 2 >= len(sig):
        return sig

    w = np.linspace(0.0, 1.0, nf) ** 0.5      # 开方：中点能量不塌陷
    out = sig[:-nf].copy()
    out[:nf] = sig[:nf] * w + sig[-nf:] * (1.0 - w)
    return out


# ---------------------------------------------------------------- 输出


def find_ffmpeg(explicit: str | None) -> str | None:
    if explicit:
        return explicit if Path(explicit).exists() else None
    found = shutil.which("ffmpeg") or shutil.which("ffmpeg.exe")
    if found:
        return found
    if FFMPEG_DEFAULT.exists():
        return str(FFMPEG_DEFAULT)
    return None


def write_out(sig: np.ndarray, sr: int, ffmpeg: str | None) -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    wav = OUT.with_suffix(".wav")
    sf.write(str(wav), sig, sr, subtype="PCM_16")

    if ffmpeg:
        r = subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", str(wav),
                            "-c:a", "libvorbis", "-q:a", "4", str(OUT)],
                           capture_output=True)
        if r.returncode == 0 and OUT.exists():
            wav.unlink()
            print(f"-> {OUT.relative_to(ROOT)}  {OUT.stat().st_size / 1024:.0f} KB")
            return
        print("ffmpeg 转换失败，保留 wav")

    print(f"-> {wav.relative_to(ROOT)}（没有 ffmpeg，引擎会读 wav）")


def report(sig: np.ndarray, sr: int) -> None:
    spec = np.abs(np.fft.rfft(sig))**2
    fr = np.fft.rfftfreq(len(sig), 1/sr)
    tot = spec.sum()

    print(f"\n时长 {len(sig)/sr:.1f}s  sr={sr}  peak={np.abs(sig).max():.3f}  "
          f"rms={np.sqrt((sig**2).mean()):.4f}")
    print(f"循环点跳变 {abs(sig[-1]-sig[0]):.4f}")
    print("频段能量:")
    for lo, hi in [(0, 80), (80, 200), (200, 1000), (1000, 4000),
                   (4000, 8000), (8000, 16000), (16000, sr/2)]:
        m = (fr >= lo) & (fr < hi)
        print(f"  {lo:5g}-{hi:6.0f}Hz: {spec[m].sum()/tot*100:5.2f}%")

    win = int(sr * 0.01)
    env = np.sqrt((sig[:len(sig)//win*win].reshape(-1, win)**2).mean(axis=1))
    print(f"颗粒感 {env.std()/env.mean():.3f}")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="导入 CC0 真实雨声并处理成无缝循环")
    ap.add_argument("--seconds", type=float, default=60.0)
    ap.add_argument("--no-download", action="store_true", help="只用本地缓存")
    ap.add_argument("--ffmpeg", default=None)
    args = ap.parse_args(argv)

    src_path = CACHE / "rain_loop.flac"
    if args.no_download and not src_path.exists():
        print(f"没有缓存: {src_path}（去掉 --no-download 联网下载）")
        return 1

    path = src_path if args.no_download else download(src_path)
    verify(path)

    data, sr = sf.read(str(path))
    if data.ndim > 1:
        data = data.mean(axis=1)
    data = data.astype(np.float64)
    print(f"源: {len(data)/sr:.2f}s  sr={sr}")

    sig, sr = build(data, sr, args.seconds)
    report(sig, sr)
    write_out(sig, sr, find_ffmpeg(args.ffmpeg))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))