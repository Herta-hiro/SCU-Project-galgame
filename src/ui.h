#ifndef UI_H
#define UI_H

#include "game.h"
#include "raylib.h"

void ui_init(void);
void ui_shutdown(void);

/* 打字机 */
void ui_begin_line(const char *speaker, const char *text);
void ui_tick(float dt, int fast);
int  ui_is_done(void);
void ui_finish(void);

/* 绘制 */
void ui_draw(const Game *g);
void ui_draw_choices(const Game *g, int hover_index);

/* 选项的屏幕矩形，供 main 做点击检测 */
Rectangle ui_choice_rect(int index, int total);

/* 鼠标命中哪个选项：返回原始下标（已跳过条件不满足的项），-1 = 没命中 */
int ui_choice_hit(Vector2 mouse, const Node *n, int affinity);

/* 卡通用的中文绘制接口，供 main / menu 使用 */
void  ui_draw_text(const char *text, float x, float y, float size, Color color);
float ui_text_width(const char *text, float size);

/* 按码点折行绘制，返回占用的行数 */
int   ui_draw_wrapped(const char *text, float x, float y, float max_w,
                      float size, Color color, float line_h);

#endif
