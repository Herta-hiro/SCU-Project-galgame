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

/* 场景切换的整屏淡入：@bg 变化时从黑场拉起，普通节点不闪。
   每帧调 ui_scene_tick，画面画完后调 ui_draw_scene_fade。 */
void ui_scene_tick(float dt, const char *bg);
void ui_scene_finish(void);      /* 立刻结束淡入，截图模式用 */
void ui_draw_scene_fade(void);

/* 绘制 */
void ui_draw(const Game *g);
void ui_draw_choices(const Game *g, int hover_index);

/* 流式生成的临时文本：生成期间直接显示在对话框里，像正常的打字机在出字，
   不盖全屏遮罩。传 NULL 恢复显示当前台词（生成结束时必须恢复）。
   文本用 '\n' 分行，调用方负责剔除状态栏行、只留最后几行。 */
void ui_set_stream(const char *text);

/* 「（自己说一句）」这个入口要不要出现。
 *
 * 离线模式（没 key，或开关拨到离线）时关掉：那时候 AI 根本不出声，
 * 点进去打完字只会拿到角色卡里那句兜底台词，答非所问，不如不给。
 * 由 main 每帧根据 ai_is_offline() 设置，切换开关后立刻生效。 */
void ui_set_free_choice(int on);

/* 选项的屏幕矩形，供 main 做点击检测 */
Rectangle ui_choice_rect(int index, int total);

/* 鼠标命中哪个选项：返回原始下标（已跳过条件不满足的项），-1 = 没命中 */
int ui_choice_hit(Vector2 mouse, const Node *n, int affinity, int seen_events);

/* 卡通用的中文绘制接口，供 main / menu 使用 */
void  ui_draw_text(const char *text, float x, float y, float size, Color color);
float ui_text_width(const char *text, float size);

/* 按码点折行绘制，返回占用的行数 */
int   ui_draw_wrapped(const char *text, float x, float y, float max_w,
                      float size, Color color, float line_h);

#endif
