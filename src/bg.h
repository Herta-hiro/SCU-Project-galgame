#ifndef BG_H
#define BG_H

/* 背景图。
 *
 * 图片来源：data/backgrounds/<名字>.png（找不到再试 .jpg）。
 * 名字由剧本里的 `@bg 名字` 指定；没有图就返回 0，调用方自己画渐变兜底。
 *
 * 纹理按需加载、常驻缓存（最多 MAX_BG 张）。一张 1920×1080 的 PNG
 * 占显存约 8 MB，六张也就 50 MB，对演示项目够用。
 */

void bg_init(void);
void bg_shutdown(void);

/* 画一张背景（按屏幕比例裁切铺满，加一层压暗蒙版保证文字可读）。
   返回 1 = 画了图；0 = 没有这张图，请调用方画兜底背景。 */
int  bg_draw(const char *name);

#endif