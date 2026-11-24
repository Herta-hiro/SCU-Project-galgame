# 音频来源与署名

## bgm_rain.ogg（雨声环境音）

**当前用的是真实录音，不是程序合成。**

- 作品：`rain_loop.flac`（ferrosintesis-samples-rain）
- 作者：**0x4D44**（仓库所有者本人录制）
- 来源：https://github.com/0x4D44/ferrosintesis/tree/main/crates/ferrosintesis-samples-rain
- 许可：**CC0 1.0 Universal（公共领域贡献）**
  全文见 https://github.com/0x4D44/ferrosintesis/blob/main/crates/ferrosintesis-samples-rain/LICENSE-CC0
- 原始素材：2017 年录制的一段稳定雨声，取自源视频 00:31–00:36 的干净窗口
  （避开车辆经过），单声道 16-bit 44.1kHz，已做 0.4s 交叉淡化。

CC0 不要求署名，这里仍然写出来，方便追溯。

### 我们做的处理

`tools/import_rain.py` 把它做成引擎用的 60 秒无缝循环：

1. 高通 55Hz，去掉远处的车流隆隆
2. 重做循环点的交叉淡化（源文件首尾差 0.16，直接循环会咔哒）
3. 铺到 60 秒：主层原速 + 副层随机偏移/1.024x 变速叠加，
   两层周期错开，避免 4.6 秒素材循环 13 次的机械感
4. 0.07Hz 阵风起伏
5. 重采样到 48kHz（和引擎音频设备一致，省一次重采样）
6. 循环点闭合，最终跳变 0.0001（峰值 0.16 的 0.06%）

重新生成：`python tools/import_rain.py`（需要 scipy + soundfile，用 `D:\voicelab\.venv`）

## se_click.wav / se_page.wav（界面音效）

程序合成，无第三方素材。`tools/gen_audio.py` 生成：

```sh
python tools/gen_audio.py
```

注意：这个脚本**默认不再生成雨声**（会覆盖真实录音）。要合成版雨声做对比或离线兜底，
加 `--with-synth-rain`。

## 合成版雨声（可选兜底）

`tools/gen_audio.py` 里的 `make_rain()` 还在，是颗粒合成（雨幕 + 泊松分布雨滴脉冲）。
音质不如真实录音——2kHz 以上能量明显少，所以听感偏闷。
只建议在没有网络、拿不到 CC0 素材时用。