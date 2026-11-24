# 配音样本（edge-tts）

同一句话、不同音色/语速/音调，用来挑沈知霁的声音。

测试文本：

> 椅子没人。别碰桌上那摞书，也别把奶茶放这边。看书的时候，不聊天。

| 文件 | 音色 | 语速 | 音调 | 说明 |
|---|---|---|---|---|
| `01-xiaoxiao-cold.ogg` | Xiaoxiao | -12% | -3Hz | 最冷的一版，推荐先听这个 |
| `02-xiaoxiao-neutral.ogg` | Xiaoxiao | 默认 | 默认 | 标准播音腔，做基准 |
| `03-xiaoxiao-soft.ogg` | Xiaoxiao | -6% | +2Hz | 软一点，偏“后来熟了”的语气 |
| `04-xiaoxiao-fast.ogg` | Xiaoxiao | +10% | -2Hz | 偏利落，适合“看书的时候不聊天”这种短句 |
| `05-xiaoyi-cold.ogg` | Xiaoyi | -10% | -3Hz | 活泼音色硬压冷，效果一般 |
| `06-xiaoyi-neutral.ogg` | Xiaoyi | 默认 | 默认 | 元气感，和沈知霁不太搭 |
| `07-xiaobei-dialect.ogg` | 辽宁 Xiaobei | -8% | -1Hz | 方言彩蛋 |
| `08-xiaoni-dialect.ogg` | 陕西 Xiaoni | -8% | -1Hz | 方言彩蛋 |
| `09-xiaoxiao-long.ogg` | Xiaoxiao | -12% | -3Hz | 多行连读，听节奏和停顿 |

## 重新生成

```sh
python -m edge_tts --voice zh-CN-XiaoxiaoNeural --rate=-12% --pitch=-3Hz \
  --text "椅子没人。" --write-media out.mp3
```

- 语速/音调必须带符号（`-12%`、`+2Hz`），写 `0%` 会失败；
- 输出 mp3，仓库里统一用 ffmpeg 转 ogg（体积小一半左右）；
- 中文女声只有 Xiaoxiao（温暖/小说）和 Xiaoyi（活泼）两个标准音色，其余是方言。

## 选定之后

音色最终选的是 **Qwen3-TTS 的 `wenzhi`（文芷）**，不是上面这几版 edge-tts 样本。
全量台词已经生成在 `data/voice/`，`python tools/gen_voice.py --list` 可以看清单。

## 10~18：文芷音色（最终采用）

| 文件 | 内容 |
|---|---|
| `10-wenzhi-short.ogg` | 椅子没人。 |
| `11-wenzhi-book.ogg` | 别碰桌上那摞书，也别把奶茶放这边。 |
| `12-wenzhi-quiet.ogg` | 看书的时候，不聊天。 |
| `13`–`15` | 「……我想一下。」默认 / 伤心 / 开心 |
| `16`–`17` | 「明天……你还来吗。」默认 / 伤心 |
| `18` | 「……伞。」 |

## 19~24：语气指令对比（`--tone-probe` 生成）

用来定 `tools/gen_voice.py` 里的 `TONE_BY_TEXT`。同一句话、不同 instruct：

| 文件 | 语气 |
|---|---|
| `19-probe-think-default.ogg` | 默认（无 instruct） |
| `20-probe-think-sad.ogg` | 用伤心的语气说 |
| `21-probe-think-soft.ogg` | 用很轻的语气说 |
| `22-probe-umbrella-default.ogg` | 「……伞。」默认 |
| `23-probe-umbrella-soft.ogg` | 「……伞。」很轻 |
| `24-probe-tomorrow-soft.ogg` | 「……明天也来。」很轻 |

实测 instruct 确实改变基频（267Hz → 253Hz → 226Hz 逐步下沉），不是被忽略。
哪一版好听就把 `gen_voice.py` 里对应的 `TONE_BY_TEXT` 改成那个说法，然后
`python tools/gen_voice.py --only <节点> --force` 重做那几句。

## 25~26：雨声对比

| 文件 | 内容 |
|---|---|
| `26-rain-synth-vs-real.ogg` | 前 8 秒合成版雨声 → 静音 0.7 秒 → 后 8 秒真实录音版（两者响度已对齐，纯音质对比） |

结论：真实录音在 4~16kHz 有明显更多的雨滴碎响，合成版这一段偏空、听感发闷。
最终采用真实录音，来源与许可见 `data/audio/CREDITS.md`。