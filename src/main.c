#include "game.h"
#include "ui.h"
#include "menu.h"
#include "ai.h"
#include "character.h"
#include "bg.h"
#include "sprite.h"

#include "raylib.h"
#include "rlgl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SCREEN_W 1280
#define SCREEN_H  720

/* Windows 在缩放 125%/150% 的屏幕上会对未声明 DPI 感知的进程做虚拟化：
   我们建 1280x720 的窗口，系统却给一个 1920x1080 的后备缓冲，
   raylib 仍按 1280x720 设置 viewport，结果画面只铺在左下角。
   进程的 DPI 感知级别只能设置一次，这里抢先声明为“不感知”，
   raylib 内部再想设置会失败。代价是 Windows 会拉伸窗口内容，高缩放下字略糊。 */
#if defined(_WIN32)
__declspec(dllimport) int __stdcall SetProcessDpiAwarenessContext(void *value);
#endif

static Game  g_game;
static int   g_line_was_read;
static float g_skip_timer;
static char  g_toast[64];
static float g_toast_timer;
static int   g_prev_state = STATE_TITLE;
static int   g_ai_pending;
static int   g_quit;          /* 用户主动退出，与剧本“本章结束”区分开 */
static int   g_fast_force;    /* 调试用：把 Ctrl 快进当成一直按住 */
static unsigned char g_seen_forced[416];  /* 调试用：--seen 预置的已触发事件 */

static void toast(const char *msg)
{
    snprintf(g_toast, sizeof(g_toast), "%s", msg);
    g_toast_timer = 1.8f;
}

static void fix_hidpi_viewport(void)
{
    Vector2 s = GetWindowScaleDPI();
    if (s.x <= 1.01f && s.y <= 1.01f) return;

    int fw = (int)(GetScreenWidth() * s.x);
    int fh = (int)(GetScreenHeight() * s.y);

    rlViewport(0, 0, fw, fh);

    Matrix proj = { 0 };
    proj.m0  =  2.0f / (float)SCREEN_W;
    proj.m5  = -2.0f / (float)SCREEN_H;
    proj.m10 = -1.0f;
    proj.m12 = -1.0f;
    proj.m13 =  1.0f;
    proj.m15 =  1.0f;
    rlSetMatrixProjection(proj);

    Matrix ident = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    rlSetMatrixModelview(ident);
}

/* ---------------- 行的生命周期 ---------------- */

static void mark_read(Game *g)
{
    if (g->ai_active) return;              /* AI 临时节点不参与已读统计 */
    if (g->node_index < 0 || g->node_index >= MAX_NODES) return;
    if (g->line_index < 0 || g->line_index >= 31) return;
    g->read_bits[g->node_index] |= (1 << g->line_index);
}

static int is_read(const Game *g)
{
    if (g->ai_active) return 0;
    if (g->node_index < 0 || g->node_index >= MAX_NODES) return 0;
    if (g->line_index < 0 || g->line_index >= 31) return 0;
    return (g->read_bits[g->node_index] >> g->line_index) & 1;
}

static void start_line(Game *g)
{
    g_line_was_read = is_read(g);

    const Line *ln = game_current_line(g);
    if (ln) {
        ui_begin_line(ln->speaker, ln->text);
        log_push(g, ln->speaker, ln->text);
    } else {
        ui_begin_line("", "");
    }

    mark_read(g);
    g_skip_timer = 0.0f;
}

/* ---------------- AI 支线 ---------------- */

/* 模型给的判定是"这一句话"的分量（通常 ±1~4），剧本里一场戏值 6~15 分。
   乘 3 把两个量纲对齐，再夹住，免得模型抽风一次把好感推掉一个阶段。 */
#define AI_JUDGE_SCALE 3
#define AI_JUDGE_MAX  12

static int g_ai_base_aff;      /* 本条分支剧本里写的好感，离线/取不到判定时用 */

static void apply_affinity(Game *g, int delta);

/* 把模型判定换算成实际好感增减。判定一般 ±1~4（它按自己的规则表打），
   剧本里一场戏值 6~15，乘 3 对齐量纲；再夹到 ±12，免得某次抽风推掉一个阶段。 */
static int delta_from_judgement(int judgement)
{
    int d = judgement * AI_JUDGE_SCALE;
    if (d >  AI_JUDGE_MAX) return  AI_JUDGE_MAX;
    if (d < -AI_JUDGE_MAX) return -AI_JUDGE_MAX;
    return d;
}

/* 把一句对话塞进“已知信息”，只保留最近若干条，满一行丢一行。 */
static void memory_push(Game *g, const char *who, const char *what)
{
    if (!what || !*what) return;

    char entry[640];
    snprintf(entry, sizeof(entry), "%s：%s\n", who, what);

    while (g->ai_memory[0] &&
           strlen(g->ai_memory) + strlen(entry) >= sizeof(g->ai_memory)) {
        char *nl = strchr(g->ai_memory, '\n');
        if (!nl) { g->ai_memory[0] = '\0'; break; }
        memmove(g->ai_memory, nl + 1, strlen(nl + 1) + 1);
    }
    if (strlen(g->ai_memory) + strlen(entry) >= sizeof(g->ai_memory)) return;

    strcat(g->ai_memory, entry);
}

static void fill_ai_prompt(Game *g, const Choice *c, const char *char_id, AiPrompt *pp)
{
    AiPrompt *p = pp;
    memset(p, 0, sizeof(*p));

    /* 人设从角色卡取；卡缺失时用内置默认卡，不会崩 */
    const CharacterCard *card = character_find(char_id);
    if (!card) card = character_default();

    int stage = character_stage_of(g->affinity);

    snprintf(p->character_id,   sizeof(p->character_id),   "%s", card->id[0] ? card->id : char_id);
    snprintf(p->character_name, sizeof(p->character_name), "%s", card->name);
    snprintf(p->persona,        sizeof(p->persona),        "%s", card->persona);
    snprintf(p->rules,          sizeof(p->rules),          "%s", card->rules);
    snprintf(p->scene,          sizeof(p->scene),          "%s",
             (c && c->scene[0])  ? c->scene  : card->scene);
    snprintf(p->user_input,     sizeof(p->user_input),     "%s",
             (c && c->prompt[0]) ? c->prompt : card->line);
    snprintf(p->player_call,    sizeof(p->player_call),    "%s",
             card->player_call[0] ? card->player_call : "你");
    snprintf(p->her_call,       sizeof(p->her_call),       "%s",
             card->her_call[0] ? card->her_call : "你");
    snprintf(p->stage,          sizeof(p->stage),          "%s", character_stage_name(stage));
    snprintf(p->stage_tone,     sizeof(p->stage_tone),     "%s", card->stage_tone[stage]);
    /* 按码点截断，避免把汉字切一半塞进 JSON */
    utf8_truncate(p->memory, (int)sizeof(p->memory), g->ai_memory, 660);

    /* 事件库与状态：地点、最近事件 */
    snprintf(p->events, sizeof(p->events), "%s", card->events);
    snprintf(p->location, sizeof(p->location), "%s", g->location);
    snprintf(p->last_event, sizeof(p->last_event), "%s", g->last_event);
    snprintf(p->history, sizeof(p->history), "%s", g->event_history);

    if (card->offline[stage][0])
        snprintf(p->offline, sizeof(p->offline), "%s", card->offline[stage]);

    /* 节点自带 @offline 就优先用它：同一个阶段在不同场景要说的话不一样，
       靠角色卡那三句兜底很容易和剧本台词撞车（第一章 meet 就撞过）。 */
    {
        const Node *cur = game_current_node(g);
        if (cur && cur->offline[0])
            snprintf(p->offline, sizeof(p->offline), "%s", cur->offline);
    }

    p->affection = g->affinity;
}

static void begin_ai_branch(Game *g, const Choice *c, const char *char_id, const char *ret_id)
{
    AiPrompt p;
    fill_ai_prompt(g, c, char_id, &p);

    /* 玩家这句也记进“已知信息”，下一轮生成能接上 */
    memory_push(g, "玩家", p.user_input);

    g->ai_return_index = script_find(g, ret_id);
    if (g->ai_return_index < 0) g->ai_return_index = g->node_index;

    /* 剧本第四列先存着：模型没给判定（离线、或状态栏写坏了）时用它兜底 */
    g_ai_base_aff = c ? c->affinity : 1;

    g->ai_active = 0;
    g_ai_pending = 1;
    ai_request(&p);
}

static void finish_ai_branch(Game *g)
{
    int judgement = 0;
    int has_judge;

    memset(&g->ai_node, 0, sizeof(Node));
    snprintf(g->ai_node.id, sizeof(g->ai_node.id), "__ai");
    has_judge = parse_reply(ai_reply(), &g->ai_node, &judgement);

    if (g->ai_node.line_count == 0) {
        g_ai_pending = 0;
        toast("生成内容为空，已跳过");
        game_enter_index(g, g->ai_return_index);
        start_line(g);
        return;
    }

    /* 判定落地：以前这一行是被直接丢掉的，模型判了也白判。
       在线时用模型给的分量，离线兜底台词没有状态栏，退回剧本第四列。 */
    apply_affinity(g, has_judge ? delta_from_judgement(judgement) : g_ai_base_aff);

    g_ai_pending  = 0;
    g->ai_active  = 1;
    g->line_index = 0;
    g->waiting_choice = 0;
    g->finished   = 0;

    /* 她第一句也进“已知信息”，下一轮能记住刚才说过什么 */
    memory_push(g, g->ai_node.lines[0].speaker[0] ? g->ai_node.lines[0].speaker : "旁白",
                g->ai_node.lines[0].text);

    start_line(g);

    if (ai_from_cache() == 2)      toast("离线模式 · 未配置 API key");
    else if (ai_from_cache() == 1) toast("读取缓存");
}

/* ---------------- 推进 ---------------- */

static void advance_game(Game *g)
{
    if (g->ai_active) {
        if (g->line_index + 1 < g->ai_node.line_count) {
            g->line_index++;
            start_line(g);
        } else {
            g->ai_active = 0;
            game_enter_index(g, g->ai_return_index);
            start_line(g);
        }
        return;
    }

    if (game_advance(g)) start_line(g);
}

/* 加/减好感。负数只扣到“进这一场时的好感度”，不会把你打回上一档去刷保底。 */
static void apply_affinity(Game *g, int delta)
{
    g->affinity += delta;
    if (g->affinity < g->event_entry_aff) g->affinity = g->event_entry_aff;
    if (g->affinity < 0)   g->affinity = 0;
    if (g->affinity > 100) g->affinity = 100;
}

static void handle_choice(Game *g)
{
    const Node *n = game_current_node(g);
    if (!n || !g->waiting_choice) return;

    int i = ui_choice_hit(GetMousePosition(), n, g->affinity);
    if (i < 0 || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;

    if (choice_is_ai(&n->choices[i])) {
        char cid[MAX_ID], rid[MAX_ID];
        choice_split_ai(&n->choices[i], cid, MAX_ID, rid, MAX_ID);
        /* 好感等生成结束再结算：模型会给出判定，见 finish_ai_branch */
        begin_ai_branch(g, &n->choices[i], cid, rid);
        return;
    }

    int idx = script_find(g, n->choices[i].target);
    if (idx >= 0) {
        memory_push(g, "玩家", n->choices[i].prompt[0] ? n->choices[i].prompt
                                                       : n->choices[i].text);
        apply_affinity(g, n->choices[i].affinity);
        game_enter_index(g, idx);
        start_line(g);
    }
}

static int hovered_choice(Game *g)
{
    const Node *n = game_current_node(g);
    if (!n || !g->waiting_choice) return -1;
    return ui_choice_hit(GetMousePosition(), n, g->affinity);
}

/* ---------------- 各状态的输入 ---------------- */

static void update_title(void)
{
    int act = menu_update_title();
    switch (act) {
        case TITLE_NEW:
            game_restart(&g_game);
            g_game.state = STATE_GAME;
            start_line(&g_game);
            break;
        case TITLE_CONTINUE:
            g_prev_state = STATE_TITLE;
            g_game.state = STATE_LOAD;
            break;
        case TITLE_QUIT:
            g_quit = 1;
            break;
        default: break;
    }
}

static void update_slots(void)
{
    int act = menu_update_slots();
    int saving = (g_game.state == STATE_SAVE);

    if (act == SLOT_CLOSE) {
        g_game.state = g_prev_state;
        return;
    }
    if (act < 0 || act >= SAVE_SLOTS) return;

    if (saving) {
        toast(save_write(&g_game, act) ? "已存档" : "存档失败");
        g_game.state = STATE_GAME;
    } else {
        if (save_read(&g_game, act)) {
            g_game.log.count = 0;          /* 读档后重新记录历史 */
            g_game.ai_active = 0;
            g_game.state = STATE_GAME;
            start_line(&g_game);
            toast("已读取");
        } else {
            toast("该槽位没有存档");
        }
    }
}

static void update_log(void)
{
    if (menu_update_log(&g_game)) g_game.state = STATE_GAME;
}

/* 快进节奏。
   已读行直接跳（0.06 秒一行）；未读行也给一点阅读时间再自动推进，
   否则第一次通读时按住 Ctrl 会“完全没反应”——旧实现只跳已读行，
   而新开一局 read_bits 是全空的，等于快进永远不触发。 */
static float skip_delay(const Line *ln, int was_read)
{
    if (was_read) return 0.06f;

    int chars = ln ? utf8_char_count(ln->text) : 0;
    float d = 0.10f + (float)chars * 0.012f;   /* 约 80 字/秒 */
    if (d > 0.75f) d = 0.75f;
    return d;
}

static void update_game(float dt)
{
    int fast = g_fast_force ||
               IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

    if (IsKeyPressed(KEY_ESCAPE)) {
        g_game.state = STATE_TITLE;
        return;
    }
    if (IsKeyPressed(KEY_S)) {
        g_prev_state = STATE_GAME;
        g_game.state = STATE_SAVE;
        return;
    }
    if (IsKeyPressed(KEY_L)) {
        g_prev_state = STATE_GAME;
        g_game.state = STATE_LOAD;
        return;
    }
    if (IsKeyPressed(KEY_TAB)) {
        g_game.state = STATE_LOG;
        return;
    }

    if (g_ai_pending) return;   /* 等待生成时不接受推进输入 */

    if (g_game.waiting_choice) {
        handle_choice(&g_game);
        return;
    }

    int advance_key = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
                      IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER);

    if (advance_key) {
        if (!ui_is_done()) ui_finish();
        else               advance_game(&g_game);
    }

    if (fast && !g_game.finished && !g_game.waiting_choice && ui_is_done()) {
        g_skip_timer += dt;
        if (g_skip_timer > skip_delay(game_current_line(&g_game), g_line_was_read)) {
            g_skip_timer = 0.0f;
            advance_game(&g_game);
        }
    } else {
        g_skip_timer = 0.0f;
    }
}

/* ---------------- 绘制 ---------------- */

static void draw_ai_pending(void)
{
    const char *msg = "正在生成……";
    float w = ui_text_width(msg, 24);
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 4, 6, 9, 150 });
    ui_draw_text(msg, (SCREEN_W - w) * 0.5f, SCREEN_H * 0.5f - 40, 24,
                 (Color){ 232, 196, 132, 255 });

    if (!ai_is_configured()) {
        const char *hint = "未检测到 API key，将使用离线兜底文本";
        float hw = ui_text_width(hint, 17);
        ui_draw_text(hint, (SCREEN_W - hw) * 0.5f, SCREEN_H * 0.5f + 6, 17,
                     (Color){ 110, 122, 138, 255 });
    }
}

static void draw_toast(float dt)
{
    if (g_toast_timer <= 0.0f) return;
    g_toast_timer -= dt;

    float tw = ui_text_width(g_toast, 20);
    float bx = SCREEN_W - tw - 60;
    DrawRectangle((int)bx - 18, SCREEN_H - 96, (int)tw + 36, 46,
                  (Color){ 20, 26, 34, 235 });
    ui_draw_text(g_toast, bx, SCREEN_H - 84, 20, (Color){ 226, 186, 118, 255 });
}

/* ---------------- 主 ---------------- */

int main(int argc, char **argv)
{
    const char *shot = NULL;
    const char *shot_node = NULL;
    int         shot_aff = 0;
    int         dump_prompt = 0;
    int         parse_demo = 0;
    float       skip_sim = 0.0f;
    const char *script_path = "data/main.gal";
    const char *seen_list = NULL;
    const char *history_seed = NULL;
    int         check_mode = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc)      shot = argv[++i];
        else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) script_path = argv[++i];
        else if (strcmp(argv[i], "--seen") == 0 && i + 1 < argc) seen_list = argv[++i];
        else if (strcmp(argv[i], "--history") == 0 && i + 1 < argc) history_seed = argv[++i];
        else if (strcmp(argv[i], "--node") == 0 && i + 1 < argc) shot_node = argv[++i];
        else if (strcmp(argv[i], "--aff") == 0 && i + 1 < argc)  shot_aff = atoi(argv[++i]);
        else if (strcmp(argv[i], "--skip-sim") == 0 && i + 1 < argc) skip_sim = (float)atof(argv[++i]);
        else if (strcmp(argv[i], "--prompt") == 0) dump_prompt = 1;
        else if (strcmp(argv[i], "--parse-demo") == 0) parse_demo = 1;
        else if (strcmp(argv[i], "--check") == 0) check_mode = 1;
    }

#if defined(_WIN32)
    SetProcessDpiAwarenessContext((void *)(long long)-1);
#endif

    if (!script_load(&g_game, script_path)) {
        fprintf(stderr, "剧本加载失败: %s\n", script_path);
        return 1;
    }
    printf("剧本已加载: %d 个节点 (%s)\n", g_game.node_count, script_path);

    /* 校验模式：用引擎自己的解析器把数据跑一遍，语法错误已经逐行打到 stderr。
       列语义只有解析器这一份实现，check_script.py 不再自己解释一遍。 */
    if (check_mode) {
        printf("CHECK: 节点 %d, 错误 %d\n", g_game.node_count, script_error_count());
        return script_error_count() ? 1 : 0;
    }

    int cards = characters_load("data/characters.gal");
    printf("角色卡已加载: %d 张\n", cards);

    /* 调试：预先标记某些事件已触发（测路由跳过）。用法: --seen A1,B3 */
    if (seen_list) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s", seen_list);
        char *p = buf;
        while (*p) {
            char *comma = strchr(p, ',');
            if (comma) *comma = '\0';
            int id = event_id_of(p);
            if (id > 0) g_seen_forced[id] = 1;
            if (!comma) break;
            p = comma + 1;
        }
        printf("seen forced\n");
    }

    /* 调试：预置“已经发生的事”，用来检查提示词。用法: --history "F5 桌上那滩水；F12 帮拿一半" */
    if (history_seed) {
        snprintf(g_game.event_history, sizeof(g_game.event_history), "%s", history_seed);
        printf("history=%s\n", g_game.event_history);
    }

    /* 调试：验证模型回复的拆行逻辑，特别是状态栏会不会被隐藏。
       用法: gal.exe --parse-demo                                 */
    if (parse_demo) {
        const char *reply =
            "沈知霁：嗯。\n"
            "旁白：她把书往自己那边收了收，笔尖没停。\n"
            "沈知霁：椅子没人。\n"
            "【好感：55/100｜阶段：2｜记忆：玩家问了索引｜判定：+2，资料相关】\n";

        Node tmp;
        int  judgment = 0;
        memset(&tmp, 0, sizeof(tmp));
        int has = parse_reply(reply, &tmp, &judgment);

        printf("parsed lines=%d (状态栏应被丢弃)\n", tmp.line_count);
        for (int i = 0; i < tmp.line_count; i++)
            printf("  [%d] speaker=%s | text=%s\n", i, tmp.lines[i].speaker, tmp.lines[i].text);
        printf("judgement=%d%s -> 实际好感增减 %d\n",
               judgment, has ? "" : "（无状态栏，退回剧本第四列）",
               has ? delta_from_judgement(judgment) : 0);

        /* 顺手验证换算与夹取：+2 应该放成 +6，+9 应该被夹到 +12 */
        printf("换算检查: +2 -> %d, +9 -> %d, -9 -> %d\n",
               delta_from_judgement(2), delta_from_judgement(9), delta_from_judgement(-9));
        return 0;
    }

    /* 调试：把某条 AI 分支真正会发出去的 JSON 打到文件里，不用开窗口。
       用法: gal.exe --prompt --node meet --aff 55            */
    if (dump_prompt) {
        const char *nid = shot_node ? shot_node : "meet";
        if (shot_aff < 0)   shot_aff = 0;
        if (shot_aff > 100) shot_aff = 100;

        g_game.affinity = shot_aff;
        game_enter_node(&g_game, nid);

        const Node *sn = game_current_node(&g_game);
        int picked = -1;
        if (sn) {
            for (int i = 0; i < sn->choice_count; i++)
                if (choice_is_ai(&sn->choices[i])) { picked = i; break; }
        }
        if (!sn) {
            printf("节点 %s 不存在\n", nid);
            return 1;
        }

        /* 没 AI 分支也照样拼一份提示词，方便检查上下文（事件清单等） */
        Choice dummy;
        memset(&dummy, 0, sizeof(dummy));
        const Choice *pick = (picked >= 0) ? &sn->choices[picked] : &dummy;

        char cid[MAX_ID], rid[MAX_ID];
        snprintf(cid, sizeof(cid), "shenzhiji");
        snprintf(rid, sizeof(rid), "hub");
        if (picked >= 0) choice_split_ai(pick, cid, MAX_ID, rid, MAX_ID);

        AiPrompt p;
        fill_ai_prompt(&g_game, pick, cid, &p);

        static char json[70000];
        ai_build_request_json(&p, json, (int)sizeof(json));

        FILE *out = fopen("prompt-preview.json", "wb");
        if (out) {
            fputs(json, out);
            fclose(out);
        }
        printf("prompt -> prompt-preview.json (%d 字节)\n", (int)strlen(json));
        ai_shutdown();
        return 0;
    }

    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "秋日回声 — Galgame Demo");
    SetTargetFPS(60);

    /* raylib 默认把 ESC 绑成“关闭窗口”，WindowShouldClose() 在按 ESC 时
       会直接返回 true，导致游戏内按 ESC 想回标题却把程序关掉了。
       这里解除绑定，ESC 完全交给我们自己的状态机处理。 */
    SetExitKey(KEY_NULL);

    ui_init();
    bg_init();
    sprite_init();
    ai_init();

    /* 预热：启动时先加载一次常用纹理，避免第一帧绘制时纹理还没上传到 GPU */
    {
        extern int bg_draw(const char *name);
        extern int sprite_draw(const char *spec);
        BeginDrawing();
        ClearBackground(BLACK);
        bg_draw("bg_lib_window");
        sprite_draw("shenzhiji:down");
        EndDrawing();
    }

    g_game.state = STATE_TITLE;

    /* 调试：模拟按住 Ctrl 快进 N 秒，打印落到哪一行。
       用来在没有键盘输入的机器上验证快进逻辑。
       用法: gal.exe --skip-sim 3                                  */
    if (skip_sim > 0.0f) {
        const float dt = 1.0f / 60.0f;

        game_restart(&g_game);
        g_game.state = STATE_GAME;
        start_line(&g_game);
        g_fast_force = 1;

        float t = 0.0f;
        int   before = g_game.line_index;
        int   guard = 0;

        while (t < skip_sim && guard++ < 200000) {
            update_game(dt);
            ui_tick(dt, 1);
            t += dt;
            if (g_game.waiting_choice) break;   /* 停在选项上，不替玩家做决定 */
        }

        printf("skip-sim %.2fs: node=%d line=%d waiting_choice=%d aff=%d\n",
               t, g_game.node_index, g_game.line_index,
               g_game.waiting_choice, g_game.affinity);
        if (g_game.line_index != before)
            printf("快进生效：line %d -> %d\n", before, g_game.line_index);
        else
            printf("快进没动，逻辑有问题\n");

        ai_shutdown();
        return 0;
    }

    /* 截图模式：绕开交互，直接渲染指定界面，用于检查排版 */
    if (shot) {
        if (strcmp(shot, "game") == 0) {
            game_restart(&g_game);
            g_game.state = STATE_GAME;
            start_line(&g_game);
        } else if (strcmp(shot, "choices") == 0) {
            game_restart(&g_game);
            for (int k = 0; k < 416; k++) g_game.seen[k] |= g_seen_forced[k];
            g_game.affinity = shot_aff;
            game_enter_node(&g_game, shot_node ? shot_node : "meet");
            if (g_game.node_index >= 0 && g_game.node_index < g_game.node_count)
                g_game.line_index = g_game.nodes[g_game.node_index].line_count - 1;
            g_game.waiting_choice = 1;
            g_game.state = STATE_GAME;
            start_line(&g_game);

            const Node *cn = game_current_node(&g_game);
            int vis = 0;
            if (cn) {
                for (int i = 0; i < cn->choice_count; i++)
                    if (choice_visible(&cn->choices[i], g_game.affinity)) vis++;
            }
            printf("choices: node=%s total=%d visible=%d aff=%d\n",
                   cn ? cn->id : "?", cn ? cn->choice_count : 0, vis, g_game.affinity);
        } else if (strcmp(shot, "save") == 0) {
            game_restart(&g_game);
            save_write(&g_game, 0);
            g_game.line_index = 1;
            g_game.affinity = 4;
            save_write(&g_game, 1);
            g_game.state = STATE_SAVE;
        } else if (strcmp(shot, "node") == 0) {
            /* 指定节点截图：gal.exe --shot node --node rain_cold */
            game_restart(&g_game);
            for (int k = 0; k < 416; k++) g_game.seen[k] |= g_seen_forced[k];
            if (shot_aff < 0)   shot_aff = 0;
            if (shot_aff > 100) shot_aff = 100;
            g_game.affinity = shot_aff;
            game_enter_node(&g_game, shot_node ? shot_node : "start");
            g_game.state = STATE_GAME;
            start_line(&g_game);

            const Node *rn = game_current_node(&g_game);
            printf("node=%s  aff=%d  lines=%d  toast=%s\n",
                   rn ? rn->id : "?", g_game.affinity, rn ? rn->line_count : 0,
                   (rn && rn->toast[0]) ? rn->toast : "-");
            if (rn && rn->toast[0]) toast(rn->toast);
        } else if (strcmp(shot, "ai") == 0) {
            /* 走一遍 AI 支线：无 key 时命中角色卡里的离线台词，按好感阶段取。
               --node 指定从哪个节点取第一条 AI 分支，--aff 指定好感度。 */
            game_restart(&g_game);
            g_game.affinity = shot_aff;
            const char *nid = shot_node ? shot_node : "meet";
            game_enter_node(&g_game, nid);
            g_game.state = STATE_GAME;

            const Node *sn = game_current_node(&g_game);
            int picked = -1;
            if (sn) {
                for (int i = 0; i < sn->choice_count; i++)
                    if (choice_is_ai(&sn->choices[i])) { picked = i; break; }
            }

            if (!sn || picked < 0) {
                printf("节点 %s 没有 AI 分支\n", nid);
            } else {
                char cid[MAX_ID], rid[MAX_ID];
                choice_split_ai(&sn->choices[picked], cid, MAX_ID, rid, MAX_ID);
                begin_ai_branch(&g_game, &sn->choices[picked], cid, rid);
                ai_poll();
                if (ai_state() == AI_DONE) finish_ai_branch(&g_game);
                printf("ai_state=%d  lines=%d  aff=%d  stage=%s\n",
                       ai_state(), g_game.ai_node.line_count, g_game.affinity,
                       character_stage_name(character_stage_of(g_game.affinity)));
                for (int k = 0; k < g_game.ai_node.line_count; k++)
                    printf("  [%d] %s：%s\n", k,
                           g_game.ai_node.lines[k].speaker,
                           g_game.ai_node.lines[k].text);
            }
        } else if (strcmp(shot, "log") == 0) {
            game_restart(&g_game);
            game_enter_node(&g_game, "meet");
            for (int i = 0; i < g_game.nodes[g_game.node_index].line_count; i++) {
                const Line *ln = &g_game.nodes[g_game.node_index].lines[i];
                log_push(&g_game, ln->speaker, ln->text);
            }
            g_game.state = STATE_LOG;
        }

        ui_finish();
        for (int i = 0; i < 2; i++) {
            BeginDrawing();
            fix_hidpi_viewport();

            if (g_game.state == STATE_TITLE)      menu_draw_title();
            else if (g_game.state == STATE_SAVE)  menu_draw_slots(&g_game, 1);
            else if (g_game.state == STATE_LOAD)  menu_draw_slots(&g_game, 0);
            else if (g_game.state == STATE_LOG)   menu_draw_log(&g_game);
            else {
                ui_draw(&g_game);
                ui_draw_choices(&g_game, -1);
            }
            draw_toast(0.016f);
            EndDrawing();
        }

        TakeScreenshot("shot.png");
        printf("screenshot -> shot.png  (state=%d)\n", g_game.state);
        ai_shutdown();
        sprite_shutdown();
        bg_shutdown();
        ui_shutdown();
        CloseWindow();
        return 0;
    }

    while (!WindowShouldClose() && !g_quit) {
        float dt = GetFrameTime();

        ai_poll();
        if (g_ai_pending && ai_state() == AI_DONE)   finish_ai_branch(&g_game);
        if (g_ai_pending && ai_state() == AI_FAILED) {
            g_ai_pending = 0;
            toast(ai_error());
            game_enter_index(&g_game, g_game.ai_return_index);
            start_line(&g_game);
        }

        switch (g_game.state) {
            case STATE_TITLE: update_title(); break;
            case STATE_GAME:  update_game(dt);  break;
            case STATE_SAVE:
            case STATE_LOAD:  update_slots(); break;
            case STATE_LOG:   update_log();   break;
            default: break;
        }

        int fast = (g_game.state == STATE_GAME) &&
                   (g_fast_force ||
                    IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
        ui_tick(dt, fast);

        /* 进新节点就冒一句提示（@toast）：安静坐下这种循环点一次有反馈 */
        {
            static unsigned last_seq = 0;
            if (g_game.enter_seq != last_seq) {
                last_seq = g_game.enter_seq;
                const Node *cn = game_current_node(&g_game);
                if (cn && cn->toast[0]) toast(cn->toast);

                /* 事件本身也进“已知信息”，让 AI 支线记得你帮过什么 */
                if (cn && cn->event[0]) memory_push(&g_game, "事件", cn->event);
            }
        }

        BeginDrawing();
        fix_hidpi_viewport();

        switch (g_game.state) {
            case STATE_TITLE:
                menu_draw_title();
                break;
            case STATE_SAVE:
                ui_draw(&g_game);
                menu_draw_slots(&g_game, 1);
                break;
            case STATE_LOAD:
                menu_draw_slots(&g_game, 0);
                break;
            case STATE_LOG:
                ui_draw(&g_game);
                menu_draw_log(&g_game);
                break;
            default:
                ui_draw(&g_game);
                ui_draw_choices(&g_game, hovered_choice(&g_game));
                /* 按住 Ctrl 时给个提示，免得以为键没反应 */
                if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
                    ui_draw_text("快进中 >>", SCREEN_W - 178, 96, 18,
                                 (Color){ 226, 186, 118, 255 });
                break;
        }

        if (g_ai_pending) draw_ai_pending();
        draw_toast(dt);

        EndDrawing();
    }

    ai_shutdown();
    sprite_shutdown();
    bg_shutdown();
    ui_shutdown();
    CloseWindow();
    return 0;
}
