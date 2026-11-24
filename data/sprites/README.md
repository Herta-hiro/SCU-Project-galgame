# 立绘 · 放图规范

这块由你自己出图，引擎这边已经接好了：**把文件丢进来就生效，不用编译、不用改代码。**

## 一、怎么放

```
data/sprites/
├── shenzhiji_normal.png    常态
├── shenzhiji_look.png      抬眼
├── shenzhiji_down.png      低头看书
├── shenzhiji_soft.png      软下来
├── shenzhiji_surprised.png 愣住意外
├── shenzhiji_shy.png       害羞躲闪
├── shenzhiji_smile.png     克制浅笑
├── shenzhiji_cold.png      戒备拒绝
└── shenzhiji_umbrella.png   手持收拢伞
```

命名规则：`<角色id>_<表情>.png`。剧本里写的是 `@char shenzhiji:look`，所以文件名必须对上。

## 二、画布要求

| 项 | 要求 |
|---|---|
| 格式 | PNG，带 alpha（透明底） |
| 尺寸 | 宽 800~1200，高 1200~1600，四张**同一画布尺寸** |
| 构图 | 半身：从头顶裁到腰胯，人物水平居中，底部不留白 |
| 底部 | 引擎会贴底摆放，下沿会被对话框压住；留一点余量别卡在关键位置 |
| 背景 | 透明，不要画背景、不要画阴影地面 |

引擎按屏幕高度缩放、底部对齐、靠右摆放（距右边缘 70px）。

## 三、四个表情的戏

| 文件名 | 出现在 | 情绪 |
|---|---|---|
| `shenzhiji_down` | 开局她低头做笔记 | 专注、不看你 |
| `shenzhiji_look` | 「椅子没人」、雨夜、你越界时 | 抬眼、审视、带距离 |
| `shenzhiji_normal` | 第二天、找索引、闭馆 | 平静、公事公办 |
| `shenzhiji_soft` | 还伞那天、闭馆前那句「明天…还来吗」 | 嘴硬但松了一点 |
| `shenzhiji_surprised` | 天府馆偶遇「……你也来这边」 | 愣住、睁眼、意外 |
| `shenzhiji_shy` | 递水「别多想」、表白后低头看鞋尖耳红 | 害羞、躲闪 |
| `shenzhiji_smile` | 朋友结局、暖结局「明天也来」 | 克制浅笑、温和 |
| `shenzhiji_cold` | 「我自己来」「别拍」「别。」「说了想一下」 | 皱眉、戒备、拒绝 |
| `shenzhiji_umbrella` | 结尾起身那句「……伞」 | 手持收拢黑伞、准备离开 |

## 四、已经有图但尺寸不对

用导入器处理（自动裁边、按比例裁半身、缩放、底部渐隐）：

```sh
python tools/import_sprite.py 你的图.png --as shenzhiji_normal --crop 0.58 --height 1600
```

`--crop` 是保留高度比例：全身图用 0.55~0.6，已经是半身图就写 1.0（不裁）。

## 五、没有图会怎样

对应节点不画人，背景和对话照常，**不报错、不白屏**。校验器只给一行提示，不算错误。

## 六、想加表情 / 加角色

- 加表情：多放一个 `shenzhiji_<新表情>.png`，剧本里 `@char shenzhiji:<新表情>` 就能用；
- 加角色：放 `linxia_normal.png`，剧本里 `@char linxia:normal`；
- 收人：`@char off`。

## 七、目录里的其他东西

- 早期的两套占位图（程序绘制 + OpenGameArt 的 Codel 素材）剧本已不再引用，
  目录精简时移除，文件留存于清理前的备份包；
- `CREDITS.md`：素材署名记录（如果哪天用回 Codel，需要保留那段 CC BY 署名）。

## 白边 / 亮边处理

AI 出的透明底立绘容易在轮廓上留一圈白边（白底混色 + 缩小时的插值把它摊开）。
仓库里的四张图已经处理过；换新图如果又出现白边：

```sh
python tools/defringe_sprites.py            # 处理 data/sprites/shenzhiji_*.png
python tools/defringe_sprites.py 新图.png
```

脚本做三件事：半透明像素按白底反预乘、轮廓裁 1 像素、透明区 RGB 填成轮廓平均色
（这样 mipmap 采样不会把白/黑拖进来）。原图会留在 `_backup/sprites-pre-defringe/`。

引擎侧也配合改了：`sprite.c` 加载立绘时生成 mipmap 并用三线性过滤，缩放时边缘更稳。

