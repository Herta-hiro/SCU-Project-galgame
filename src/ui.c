#include "ui.h"
#include "bg.h"
#include "sprite.h"
#include "chinese_font_data.h"

#include "raylib.h"
#include <string.h>
#include <stdio.h>

/* ---------- 版式常量 ---------- */
#define SCREEN_W       1280
#define SCREEN_H        720

#define BOX_X            60
#define BOX_Y           455
#define BOX_W          1160
#define BOX_H           205

#define TEXT_X           92
#define TEXT_Y          512
#define TEXT_W         1096
#define BODY_SIZE        27
#define NAME_SIZE        25
#define LINE_H           42

#define CHOICE_W        620
#define CHOICE_H         54
#define CHOICE_GAP       16
#define CHOICE_TOP      250

/* ---------- 配色 ---------- */
static const Color COL_BG_TOP    = {  14,  18,  26, 255 };
static const Color COL_BG_BOTTOM = {  22,  28,  38, 255 };
static const Color COL_BOX       = {   8,  11,  16, 214 };
static const Color COL_BOX_EDGE  = {  58,  70,  88, 255 };
static const Color COL_NAME      = { 226, 186, 118, 255 };
static const Color COL_BODY      = { 228, 231, 236, 255 };
static const Color COL_HINT      = { 110, 122, 138, 255 };
static const Color COL_CHOICE    = {  26,  32,  44, 235 };
static const Color COL_CHOICE_HI = {  52,  74,  84, 245 };
static const Color COL_CHOICE_ED = {  74,  92, 110, 255 };

/* 好感度显示开关。
   默认关：玩家看不到数值，只能从她的态度、话的长度、愿不愿意等你去感受。
   调试分支数值时改成 1 重新编译，右上角就会显示「好感度 N」。 */
#define SHOW_AFFINITY 0

static Font  g_font;

/* 场景切换的整屏淡入。@bg 一变就从黑场拉起，比硬切干净。
   只跟背景名走：普通节点（不改 @bg）不会闪。 */
static char  g_scene_bg[32];
static float g_scene_fade = 1.0f;     /* 0 = 全黑，1 = 正常 */

void ui_scene_tick(float dt, const char *bg)
{
    if (!bg) bg = "";

    if (strcmp(bg, g_scene_bg) != 0) {
        snprintf(g_scene_bg, sizeof(g_scene_bg), "%s", bg);
        g_scene_fade = 0.0f;
    }

    if (g_scene_fade < 1.0f) {
        g_scene_fade += dt / 0.32f;
        if (g_scene_fade > 1.0f) g_scene_fade = 1.0f;
    }
}

void ui_scene_finish(void)
{
    g_scene_fade = 1.0f;      /* 截图模式用：别拍到黑场 */
}

void ui_draw_scene_fade(void)
{
    if (g_scene_fade < 1.0f)
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 1.0f - g_scene_fade));
}

/* 打字机状态 */
static int   g_total;
static int   g_visible;
static float g_acc;
static char  g_speaker[40];
static char  g_text[MAX_TEXT];

/* 流式生成期间临时显示在对话框里的文本（见 ui_set_stream） */
static const char *g_stream;

void ui_set_stream(const char *text)
{
    g_stream = text;
}

static int utf8_len(unsigned char c)
{
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x06) return 2;
    if ((c >> 4) == 0x0E) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

void ui_init(void)
{
    g_font = LoadFontEx("C:/Windows/Fonts/simkai.ttf", BODY_SIZE,
                        (int *)CHINESE_CODEPOINTS, CHINESE_CODEPOINT_COUNT);
    SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);
}

void ui_shutdown(void)
{
    UnloadFont(g_font);
}

void ui_begin_line(const char *speaker, const char *text)
{
    snprintf(g_speaker, sizeof(g_speaker), "%s", speaker ? speaker : "");
    snprintf(g_text, sizeof(g_text), "%s", text ? text : "");
    g_total   = utf8_char_count(g_text);
    g_visible = 0;
    g_acc     = 0.0f;
}

void ui_tick(float dt, int fast)
{
    if (g_visible >= g_total) return;

    /* 30 字/秒；快进时直接全出 */
    if (fast) {
        g_visible = g_total;
        return;
    }

    g_acc += dt * 30.0f;
    while (g_acc >= 1.0f && g_visible < g_total) {
        g_acc -= 1.0f;
        g_visible++;
    }
}

int ui_is_done(void)
{
    return g_visible >= g_total;
}

void ui_finish(void)
{
    g_visible = g_total;
}

/* 按码点折行绘制。中文字宽按一个字号算，ASCII 按 0.55 字号算。
   不用 MeasureTextEx 逐字测量，那在打字机每帧重算会拖慢帧率。 */
int ui_draw_wrapped(const char *text, float x, float y, float max_w,
                    float fs, Color color, float line_h)
{
    char buf[MAX_TEXT + 8];
    const char *p = text;
    float cur = 0.0f;
    float cy = y;
    int   bi = 0;
    int   in_line = 0;
    int   row = 1;

    while (*p) {
        int len = utf8_len((unsigned char)*p);
        float w = (len == 1) ? (fs * 0.55f) : fs;

        if (cur + w > max_w && in_line > 0) {
            buf[bi] = '\0';
            DrawTextEx(g_font, buf, (Vector2){ x, cy }, fs, 1.0f, color);
            cy += line_h;
            row++;
            bi = 0;
            cur = 0.0f;
            in_line = 0;
        }

        memcpy(buf + bi, p, (size_t)len);
        bi += len;
        p += len;
        cur += w;
        in_line++;
    }

    if (bi > 0) {
        buf[bi] = '\0';
        DrawTextEx(g_font, buf, (Vector2){ x, cy }, fs, 1.0f, color);
    }

    return row;
}

void ui_draw(const Game *g)
{
    /* 背景：有图就画图（bg_draw 自己会加压暗蒙版），没图退回竖直渐变 */
    if (!bg_draw(g->bg))
        DrawRectangleGradientV(0, 0, SCREEN_W, SCREEN_H, COL_BG_TOP, COL_BG_BOTTOM);

    /* 立绘：压在背景上、对话框下面 */
    sprite_draw(g->spr);

    /* 左上角标题 */
    DrawRectangle(60, 56, 4, 22, COL_NAME);
    DrawTextEx(g_font, "秋日回声", (Vector2){ 78, 54 }, 20, 1.0f, COL_NAME);

    /* 好感度：默认不画（见文件顶部 SHOW_AFFINITY） */
#if SHOW_AFFINITY
    {
        char aff[64];
        snprintf(aff, sizeof(aff), "好感度 %d", g->affinity);
        DrawTextEx(g_font, aff, (Vector2){ SCREEN_W - 200, 56 }, 20, 1.0f, COL_HINT);
    }
#endif

    /* 对话框 */
    Rectangle box = { BOX_X, BOX_Y, BOX_W, BOX_H };
    DrawRectangleRounded(box, 0.06f, 8, COL_BOX);
    DrawRectangleRoundedLines(box, 0.06f, 8, COL_BOX_EDGE);

    /* 生成中：正文直接显示已收到的部分，不画全屏遮罩、
       也不弹「正在生成」——玩家看到的就是她的话在对话框里一行行长出来。 */
    if (g_stream) {
        float y = TEXT_Y;
        const char *p = g_stream;
        int row = 0;

        while (p && *p && row < 3) {
            const char *nl = strchr(p, '\n');
            int len = nl ? (int)(nl - p) : (int)strlen(p);
            if (len > 0) {
                char seg[MAX_TEXT];
                if (len >= (int)sizeof(seg)) len = (int)sizeof(seg) - 1;
                memcpy(seg, p, (size_t)len);
                seg[len] = '\0';
                y += (float)ui_draw_wrapped(seg, TEXT_X, y, TEXT_W,
                                            BODY_SIZE, COL_BODY, LINE_H);
                row++;
            }
            if (!nl) break;
            p = nl + 1;
        }

        /* 右下角一个很淡的省略号，表示还在写 */
        Vector2 sz = MeasureTextEx(g_font, "…", 19, 1.0f);
        DrawTextEx(g_font, "…", (Vector2){ BOX_X + BOX_W - sz.x - 28,
                                           BOX_Y + BOX_H - 38 }, 19, 1.0f,
                   (Color){ 120, 134, 148, 190 });
        return;
    }

    /* 说话人 */
    if (g_speaker[0]) {
        Vector2 sz = MeasureTextEx(g_font, g_speaker, NAME_SIZE, 1.0f);
        Rectangle name_bg = { TEXT_X - 14, BOX_Y - 34, sz.x + 40, 44 };
        DrawRectangleRounded(name_bg, 0.4f, 8, COL_BOX);
        DrawRectangleRoundedLines(name_bg, 0.4f, 8, COL_BOX_EDGE);
        DrawTextEx(g_font, g_speaker, (Vector2){ TEXT_X + 6, BOX_Y - 26 },
                   NAME_SIZE, 1.0f, COL_NAME);
    }

    /* 正文（按已显示码点截断） */
    char shown[MAX_TEXT + 8];
    utf8_truncate(shown, (int)sizeof(shown), g_text, g_visible);
    if (shown[0])
        ui_draw_wrapped(shown, TEXT_X, TEXT_Y, TEXT_W, BODY_SIZE, COL_BODY, LINE_H);

    /* 右下角提示 */
    const char *hint = NULL;
    if (g->waiting_choice)      hint = "选择一个选项";
    else if (g->finished)       hint = "本章结束 · Esc 回到标题";
    else if (ui_is_done())      hint = "点击继续";

    if (hint) {
        Vector2 sz = MeasureTextEx(g_font, hint, 19, 1.0f);
        DrawTextEx(g_font, hint, (Vector2){ BOX_X + BOX_W - sz.x - 28,
                                            BOX_Y + BOX_H - 38 }, 19, 1.0f, COL_HINT);
    }

    /* 左下角操作提示 */
    DrawTextEx(g_font, "Ctrl 快进   S 存档   L 读档   Tab 记录   Esc 标题",
               (Vector2){ 60, SCREEN_H - 44 }, 17, 1.0f, (Color){ 108, 120, 136, 255 });
}

Rectangle ui_choice_rect(int index, int total)
{
    float total_h = total * CHOICE_H + (total - 1) * CHOICE_GAP;
    float top = CHOICE_TOP - total_h * 0.5f + 60;
    Rectangle r = { (SCREEN_W - CHOICE_W) * 0.5f,
                    top + index * (CHOICE_H + CHOICE_GAP),
                    CHOICE_W, CHOICE_H };
    return r;
}

static int visible_count(const Node *n, int affinity, int seen_events)
{
    int c = 0;
    for (int i = 0; n && i < n->choice_count; i++)
        if (choice_visible(&n->choices[i], affinity, seen_events)) c++;
    return c;
}

/* 选项区永远比剧本多一个：「（自己说一句）」。
   它不在 Node.choices 里（免得改剧本），由 ui 画出来、main 单独处理。
   这样玩家在**任何**选择点都能自由开口，而不是非要等到 hub——
   而 hub 基本停不住，每次回去都被自动路由带去下一个事件。 */
#define FREE_CHOICE_TEXT "（自己说一句）"

static int total_slots(const Node *n, int affinity, int seen_events)
{
    return visible_count(n, affinity, seen_events) + 1;
}

int ui_choice_hit(Vector2 m, const Node *n, int affinity, int seen_events)
{
    if (!n) return -1;

    int total = total_slots(n, affinity, seen_events);
    int k = 0;
    for (int i = 0; i < n->choice_count; i++) {
        if (!choice_visible(&n->choices[i], affinity, seen_events)) continue;
        if (CheckCollisionPointRec(m, ui_choice_rect(k, total))) return i;
        k++;
    }

    /* 虚拟选项：索引返回 choice_count，调用方据此切到输入界面 */
    if (CheckCollisionPointRec(m, ui_choice_rect(k, total))) return n->choice_count;
    return -1;
}

void ui_draw_choices(const Game *g, int hover_index)
{
    const Node *n = game_current_node(g);
    if (!n || !g->waiting_choice) return;

    int seen_ev = game_seen_count(g);
    int total = total_slots(n, g->affinity, seen_ev);
    int k = 0;

    for (int i = 0; i < n->choice_count; i++) {
        if (!choice_visible(&n->choices[i], g->affinity, seen_ev)) continue;

        Rectangle r = ui_choice_rect(k, total);
        int hover = (i == hover_index);

        DrawRectangleRounded(r, 0.45f, 10, hover ? COL_CHOICE_HI : COL_CHOICE);
        DrawRectangleRoundedLines(r, 0.45f, 10, hover ? COL_NAME : COL_CHOICE_ED);

        Vector2 sz = MeasureTextEx(g_font, n->choices[i].text, 24, 1.0f);
        DrawTextEx(g_font, n->choices[i].text,
                   (Vector2){ r.x + (r.width - sz.x) * 0.5f,
                              r.y + (r.height - sz.y) * 0.5f },
                   24, 1.0f, hover ? COL_NAME : COL_BODY);
        k++;
    }

    /* 最后那个「（自己说一句）」：比正文暗一档，让它是“可选项”而不是主分支 */
    {
        Rectangle r = ui_choice_rect(k, total);
        int hover = (hover_index == n->choice_count);

        DrawRectangleRounded(r, 0.45f, 10,
                             hover ? COL_CHOICE_HI : (Color){ 22, 28, 34, 180 });
        DrawRectangleRoundedLines(r, 0.45f, 10,
                                  hover ? COL_NAME : (Color){ 72, 86, 100, 200 });

        Vector2 sz = MeasureTextEx(g_font, FREE_CHOICE_TEXT, 23, 1.0f);
        DrawTextEx(g_font, FREE_CHOICE_TEXT,
                   (Vector2){ r.x + (r.width - sz.x) * 0.5f,
                              r.y + (r.height - sz.y) * 0.5f },
                   23, 1.0f, hover ? COL_NAME : (Color){ 150, 162, 176, 255 });
    }
}

void ui_draw_text(const char *text, float x, float y, float size, Color color)
{
    DrawTextEx(g_font, text, (Vector2){ x, y }, size, 1.0f, color);
}

float ui_text_width(const char *text, float size)
{
    return MeasureTextEx(g_font, text, size, 1.0f).x;
}
