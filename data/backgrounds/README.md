# 背景图

游戏启动时按剧本里的 `@bg 名字` 去这张目录找 `名字.png`，找不到再试 `名字.jpg`，
都没有就退回原来的深色渐变底（不会报错、不会崩）。

## 现有图（程序生成，无版权问题）

| 文件 | 用在哪 |
|---|---|
| `bg_title.png` | 标题界面 |
| `bg_lib_window.png` | 省图三楼靠窗长桌（第一章主场景） |
| `bg_lib_shelf.png` | 三楼书架区 / 找索引 |
| `bg_rain_window.png` | 雨窗特写 / 她走侧门那一段 |
| `bg_lib_night.png` | 闭馆 |
| `bg_street_night.png` | 台阶与路灯 / 结尾 |

生成脚本：`tools/gen_backgrounds.py`（改配色、光晕、雨的密度都在里面，重跑即可覆盖）。
1920×1080 PNG，单张约 3 MB。

## 换成实拍照片

1. 找一张 16:9（或更宽）的图，裁到 1920×1080 左右；
2. 用同名文件覆盖，例如把拍到的阅览室存成 `bg_lib_window.png`；
3. 直接运行游戏，**不用重新编译**。

也可以不改剧本，只替换文件——`@bg` 的名字就是文件名。

## 找图关键词（自己找的时候用得上）

- `library reading room window seats` / `图书馆 阅览室 靠窗`
- `rain on window bokeh` / `雨天 窗玻璃 水痕`
- `library bookshelf aisle warm light` / `书架 走廊 暖光`
- `university library night empty` / `图书馆 闭馆 夜`

Unsplash、Pexels 这类站点是免费可商用的（署名不强制但建议保留作者）；
新闻图、公众号图、小红书图不要直接用。

## 想换风格

- 更亮/更暗：`tools/gen_backgrounds.py` 里的 `lift(gain=1.32, bias=8)`；
- 换色：文件顶部 `NIGHT_TOP / NIGHT_BOT / LAMP / WINDOW_SKY`；
- 换构图：每个 `bg_*()` 函数画一张图，`window()`、`shelves()`、`table()` 是积木。