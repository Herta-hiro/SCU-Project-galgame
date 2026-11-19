# AI 生成「省图三楼靠窗座位」背景 · 提示词

给 `data/backgrounds/bg_lib_window.png` 用。现在这张是 `tools/gen_lib_window.py`
程序画的（只是兜底），如果你用 SD / NovelAI / 即梦 出图，直接覆盖同名文件即可，不用编译。

## 一、构图要求（先看这个，比提示词重要）

- 画幅 **16:9**，出图 1920×1080 或更大，最后缩到 1920×1080；
- **左侧约 55% 是一整面窗**：窗外是雨天的树影和模糊楼体，玻璃上有雨痕；
- **右后方是书架**，压暗、轻微失焦；
- **前景一张长桌横贯画面下缘**：台灯（暖光）、几摞书、摊开的笔记本、一个保温杯；
- **画面右侧 1/3 留空**：立绘站在那儿（引擎把人物贴右侧、底部对齐），别在右侧摆高柜子或大灯；
- 视角：坐在桌前的第一人称，略微俯视桌面，窗口在左前方；
- 下部要够暗（对话框压在上面），上部可以有窗光。

## 二、正向提示词（可直接贴）

```text
visual novel background, no characters, empty room,
third floor reading room of a Chinese provincial library, late autumn rainy afternoon,
large window on the left with rain streaks on glass, blurred tree silhouettes and
distant buildings outside, cold grey-blue daylight,
long wooden reading table in the foreground with a warm desk lamp,
stacks of old local chronicle books with paper bookmarks, an open notebook,
a stainless steel thermos cup,
tall bookshelves in the right background, out of focus, dark,
quiet academic atmosphere, soft volumetric light from the window,
muted desaturated palette, cool shadows, subtle warm lamp glow,
cinematic composition, slight vignette, film grain,
anime background art style, painterly, detailed, 16:9
```

## 三、负向提示词

```text
people, character, hands, text, watermark, signature, logo,
hud, ui, frame, border, bright cartoon colors, oversaturated,
neon, lens flare, fisheye, tilted horizon, messy composition,
cluttered right side, giant lamp blocking the right third
```

## 四、参数建议

| 项 | 建议 |
|---|---|
| 尺寸 | 1920×1080（或 1216×832 生成后放大） |
| 采样 | 25–35 步，CFG 5–7 |
| 模型 | 二次元背景向（如 Anything / 背景专用 LoRA），别用真人写实模型 |
| 批量 | 一次出 4 张，挑构图对的；同一 seed 微调重出更容易成套 |

## 五、色调对齐（跟其他五张保持一致）

从现有 `data/backgrounds/` 里量出来的目标值，出图后可以用它自查：

| 指标 | 目标 |
|---|---|
| 整体亮度 | 55–65（0–255） |
| 高光 95 分位 | 130–160 |
| 饱和度 | 0.25–0.35（低饱和） |
| 冷暖 | R−B ≈ −5 ~ −10（偏冷） |
| 上/中/下亮度 | 约 75 / 60 / 37（上亮下暗，给对话框留空间） |

快速自查：

```sh
python - <<'PY'
from PIL import Image
import numpy as np
a = np.array(Image.open("data/backgrounds/bg_lib_window.png").convert("RGB")).astype(float)
print("L", a.mean().round(1), "p95", np.percentile(a,95).round(1),
      "warmth", (a[...,0]-a[...,2]).mean().round(1))
PY
```

## 六、出完图之后

1. 覆盖 `data/backgrounds/bg_lib_window.png`；
2. 跑一下看构图：

```sh
gal.exe --shot node --node meet
```

3. 如果人物把台灯挡住了、或者右侧太挤，`src/sprite.c` 里改这两个数即可：
   - 人物靠右距离：`x = SCREEN_W - w - 70.0f;`
   - 人物高度占比：`scale = SCREEN_H * 1.02f / height;`