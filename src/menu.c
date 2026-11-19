#include "menu.h"
#include "ui.h"
#include "bg.h"
#include "sprite.h"

#include "raylib.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define SCREEN_W 1280
#define SCREEN_H  720

static const Color COL_TITLE     = { 232, 196, 132, 255 };
static const Color COL_TEXT      = { 228, 231, 236, 255 };
static const Color COL_DIM       = { 120, 132, 148, 255 };
static const Color COL_ITEM      = {  22,  28,  38, 210 };
static const Color COL_ITEM_HI   = {  40,  56,  66, 240 };
static const Color COL_EDGE      = {  70,  86, 104, 255 };
static const Color COL_EDGE_HI   = { 196, 156,  96, 255 };
static const Color COL_MASK      = {   4,   6,   9, 200 };
static const Color COL_CARD      = {  18,  23,  31, 240 };

/* ============ 标题 ============ */

static const char *TITLE_ITEMS[] = { "开始新游戏", "继续游戏", "退出" };
#define TITLE_COUNT 3

static int g_title_sel = 0;   /* 当前高亮项，鼠标和键盘共用 */

static int title_hover(void)
{
    Vector2 m = GetMousePosition();
    for (int i = 0; i < TITLE_COUNT; i++) {
        Rectangle r = { 490, 380.0f + i * 72.0f, 300, 56 };
        if (CheckCollisionPointRec(m, r)) return i;
    }
    return -1;
}

int menu_update_title(void)
{
    /* 有任意存档才允许“继续” */
    int has_save = save_exists(0) || save_exists(1) || save_exists(2);

    int mh = title_hover();
    Vector2 md = GetMouseDelta();
    if (mh >= 0 && (md.x != 0.0f || md.y != 0.0f)) g_title_sel = mh;

    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S))
        g_title_sel = (g_title_sel + 1) % TITLE_COUNT;
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W))
        g_title_sel = (g_title_sel + TITLE_COUNT - 1) % TITLE_COUNT;

    int clicked = -1;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mh >= 0) clicked = mh;

    int confirm = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE);
    int pick = clicked >= 0 ? clicked : (confirm ? g_title_sel : -1);

    /* 无存档时“继续”无效，落回当前选区 */
    if (pick == 1 && !has_save) pick = -1;

    switch (pick) {
        case 0: return TITLE_NEW;
        case 1: return TITLE_CONTINUE;
        case 2: return TITLE_QUIT;
        default: break;
    }

    /* 标题是最外层，Esc 不再做事。退出只能点“退出”那一项，
       避免从游戏按 Esc 回到标题后又手滑按一次把程序关掉。 */
    return TITLE_NONE;
}

void menu_draw_title(void)
{
    /* 标题背景（data/backgrounds/bg_title.png），没有就退回渐变 */
    if (!bg_draw("bg_title"))
        DrawRectangleGradientV(0, 0, SCREEN_W, SCREEN_H,
                               (Color){ 12,  16,  24, 255 },
                               (Color){ 26,  32,  44, 255 });

    /* 标题也站一个人，稍微压暗一点，别抢字 */
    sprite_draw("shenzhiji:normal");
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 8, 10, 16, 54 });

    /* 标题 */
    const char *big = "秋 日 回 声";
    const char *sub = "AUTUMN ECHO";
    float bw = ui_text_width(big, 66);
    float sw = ui_text_width(sub, 20);

    ui_draw_text(big, (SCREEN_W - bw) * 0.5f, 190, 66, COL_TITLE);
    ui_draw_text(sub, (SCREEN_W - sw) * 0.5f, 276, 20, (Color){ 98, 110, 126, 255 });

    DrawRectangle(490, 320, 300, 1, (Color){ 60, 70, 86, 255 });

    int has_save = save_exists(0) || save_exists(1) || save_exists(2);

    for (int i = 0; i < TITLE_COUNT; i++) {
        Rectangle r = { 490, 380.0f + i * 72.0f, 300, 56 };
        int disabled = (i == 1 && !has_save);
        int on = (i == g_title_sel && !disabled);

        DrawRectangleRounded(r, 0.3f, 8, on ? COL_ITEM_HI : COL_ITEM);
        DrawRectangleRoundedLines(r, 0.3f, 8,
                                  on ? COL_EDGE_HI : COL_EDGE);

        Color tc = disabled ? (Color){ 72, 78, 88, 255 }
                            : (on ? COL_TITLE : COL_TEXT);
        float tw = ui_text_width(TITLE_ITEMS[i], 24);
        ui_draw_text(TITLE_ITEMS[i], r.x + (r.width - tw) * 0.5f, r.y + 16, 24, tc);
    }

    ui_draw_text("C 语言 + raylib · 课程作业演示", 60, SCREEN_H - 44, 17,
                 (Color){ 96, 108, 124, 255 });
}

/* ============ 存 / 读档 ============ */

static int slot_hover(void)
{
    Vector2 m = GetMousePosition();
    for (int i = 0; i < SAVE_SLOTS; i++) {
        Rectangle r = { 240, 180.0f + i * 144.0f, 800, 120 };
        if (CheckCollisionPointRec(m, r)) return i;
    }
    return -1;
}

int menu_update_slots(void)
{
    int hov = slot_hover();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hov >= 0) return hov;
    if (IsKeyPressed(KEY_ESCAPE)) return SLOT_CLOSE;

    for (int i = 0; i < SAVE_SLOTS; i++) {
        if (IsKeyPressed(KEY_ONE + i)) return i;
    }
    return SLOT_NONE;
}

static void fmt_time(long long ts, char *out, int size)
{
    if (ts <= 0) { snprintf(out, (size_t)size, "—"); return; }
    time_t t = (time_t)ts;
    struct tm *tmv = localtime(&t);
    if (!tmv) { snprintf(out, (size_t)size, "—"); return; }
    strftime(out, (size_t)size, "%Y-%m-%d %H:%M", tmv);
}

void menu_draw_slots(Game *g, int saving)
{
    (void)g;
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, COL_MASK);

    ui_draw_text(saving ? "存 档" : "读 档", 240, 96, 34, COL_TITLE);
    ui_draw_text(saving ? "选择要写入的位置" : "选择要读取的位置",
                 240, 140, 18, COL_DIM);

    int hov = slot_hover();

    for (int i = 0; i < SAVE_SLOTS; i++) {
        SaveSlotInfo info;
        save_slot_info(i, &info);

        Rectangle r = { 240, 180.0f + i * 144.0f, 800, 120 };
        int on = (i == hov);

        DrawRectangleRounded(r, 0.09f, 8, on ? COL_ITEM_HI : COL_CARD);
        DrawRectangleRoundedLines(r, 0.09f, 8, on ? COL_EDGE_HI : COL_EDGE);

        char num[8];
        snprintf(num, sizeof(num), "%d", i + 1);
        ui_draw_text(num, r.x + 30, r.y + 34, 40,
                     info.exists ? COL_TITLE : (Color){ 62, 70, 82, 255 });

        if (!info.exists) {
            ui_draw_text("空", r.x + 110, r.y + 44, 22, (Color){ 88, 98, 112, 255 });
        } else {
            char when[48];
            fmt_time(info.timestamp, when, (int)sizeof(when));
            ui_draw_text(when, r.x + 110, r.y + 22, 18, COL_DIM);

            ui_draw_wrapped(info.preview, r.x + 110, r.y + 52, 560, 21, COL_TEXT, 28);

            /* 好感度不显示：数值是隐藏参数，玩家只通过剧情感受 */
        }
    }

    ui_draw_text("点击选择   Esc 返回", 240, SCREEN_H - 80, 17, COL_DIM);
}

/* ============ 对话记录 ============ */

static int log_scroll = 0;

int menu_update_log(Game *g)
{
    int total = g->log.count;
    int page  = 6;
    int max_scroll = total > page ? total - page : 0;

    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W))      log_scroll++;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S))    log_scroll--;
    if (IsKeyPressed(KEY_PAGE_UP))                        log_scroll += page;
    if (IsKeyPressed(KEY_PAGE_DOWN))                      log_scroll -= page;

    float wheel = GetMouseWheelMove();
    if (wheel > 0.0f) log_scroll += (int)wheel;
    if (wheel < 0.0f) log_scroll += (int)wheel;

    if (log_scroll < 0) log_scroll = 0;
    if (log_scroll > max_scroll) log_scroll = max_scroll;

    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_TAB) ||
        IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
        return 1;

    return 0;
}

void menu_draw_log(Game *g)
{
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 4, 6, 9, 232 });

    ui_draw_text("对话记录", 120, 56, 30, COL_TITLE);

    char cnt[64];
    snprintf(cnt, sizeof(cnt), "共 %d 条", g->log.count);
    ui_draw_text(cnt, SCREEN_W - 220, 66, 18, COL_DIM);

    DrawRectangle(120, 104, SCREEN_W - 240, 1, (Color){ 52, 60, 72, 255 });

    if (g->log.count == 0) {
        ui_draw_text("还没有任何对话。", 120, 160, 20, COL_DIM);
        return;
    }

    int page = 6;
    int end   = g->log.count - log_scroll;
    int start = end - page;
    if (start < 0) start = 0;

    float y = 132.0f;
    for (int i = start; i < end; i++) {
        const Line *ln = &g->log.entries[i];

        if (ln->speaker[0])
            ui_draw_text(ln->speaker, 120, y, 19, COL_TITLE);

        float tx = ln->speaker[0] ? 240.0f : 120.0f;
        int rows = ui_draw_wrapped(ln->text, tx, y, SCREEN_W - 200 - tx, 21,
                                   COL_TEXT, 30);

        y += 30.0f * rows + 26.0f;
        if (y > SCREEN_H - 90) break;
    }

    DrawRectangle(120, SCREEN_H - 84, SCREEN_W - 240, 1,
                  (Color){ 52, 60, 72, 255 });

    ui_draw_text("滚轮 / ↑↓ 翻页    Esc 或 Tab 返回", 120, SCREEN_H - 62, 17, COL_DIM);
}
