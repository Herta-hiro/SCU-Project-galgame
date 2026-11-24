#include "game.h"
#include "ui.h"
#include "menu.h"
#include "ai.h"
#include "audio.h"
#include "voice.h"
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

/* ---------------- 自由输入 ---------------- */
/*
 * raylib 不处理 IME：中文能从 Windows 拿到 WM_CHAR（GetCharPressed 收得到），
 * 但**候选窗不显示**，玩家等于盲打。所以这里补一条 Ctrl+V 粘贴通道：
 * 在别处（记事本、微信）打好一句话复制过来，粘进输入框。
 */
/* 输入上限跟 Choice.prompt 的容量（160 字节）对齐：句子最终要装进那个字段
   喂给模型，输入框比它长没有意义，只会被截断。约 50 个汉字。 */
#define FREE_INPUT_MAX 152

static char g_free_input[FREE_INPUT_MAX];

#if defined(_WIN32)
/* 不 include <windows.h>：它和 raylib 的 Rectangle / CloseWindow / ShowCursor
   正面撞名，会把整个编译单元搞崩。这里只按需声明剪贴板那几个导出函数。 */
__declspec(dllimport) int    __stdcall OpenClipboard(void *hwnd);
__declspec(dllimport) int    __stdcall CloseClipboard(void);
__declspec(dllimport) void  *__stdcall GetClipboardData(unsigned int format);
__declspec(dllimport) void  *__stdcall GlobalLock(void *mem);
__declspec(dllimport) int    __stdcall GlobalUnlock(void *mem);

#define CF_UNICODETEXT_W 13

/* 读剪贴板里的 Unicode 文本，转成 UTF-8 追加到 buf。返回追加的字节数。 */
static int clipboard_paste_utf8(char *buf, int size)
{
    if (!OpenClipboard(NULL)) return 0;

    int added = 0;
    void *h = GetClipboardData(CF_UNICODETEXT_W);
    if (h) {
        const unsigned short *w = (const unsigned short *)GlobalLock(h);
        if (w) {
            int used = (int)strlen(buf);
            for (; *w && used < size - 5; w++) {
                unsigned int cp = (unsigned int)*w;

                /* 跳过换行：输入框是单行，粘多行会脏 */
                if (cp == '\r' || cp == '\n' || cp == '\t') cp = ' ';
                if (cp < 0x20) continue;

                char tmp[4];
                int n;
                if (cp < 0x80) {
                    tmp[0] = (char)cp; n = 1;
                } else if (cp < 0x800) {
                    tmp[0] = (char)(0xC0 | (cp >> 6));
                    tmp[1] = (char)(0x80 | (cp & 0x3F));
                    n = 2;
                } else if (cp < 0x10000) {
                    tmp[0] = (char)(0xE0 | (cp >> 12));
                    tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    tmp[2] = (char)(0x80 | (cp & 0x3F));
                    n = 3;
                } else {
                    continue;               /* UTF-16 代理对不处理，中文用不到 */
                }
                memcpy(buf + used, tmp, (size_t)n);
                used += n;
                added += n;
                buf[used] = '\0';
            }
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return added;
}
#endif

/* 追加一个码点（GetCharPressed 给的是 Unicode） */
static void input_append_cp(int cp)
{
    int used = (int)strlen(g_free_input);
    char tmp[4];
    int n;

    if (cp < 0x20 || cp == 0x7F) return;      /* 控制字符不要 */

    if (cp < 0x80)                          { tmp[0] = (char)cp; n = 1; }
    else if (cp < 0x800)                    { tmp[0] = (char)(0xC0 | (cp >> 6));
                                              tmp[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000)                  { tmp[0] = (char)(0xE0 | (cp >> 12));
                                              tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                              tmp[2] = (char)(0x80 | (cp & 0x3F)); n = 3; }
    else                                    return;

    if (used + n >= FREE_INPUT_MAX - 1) return;
    memcpy(g_free_input + used, tmp, (size_t)n);
    g_free_input[used + n] = '\0';
}

/* 删掉最后一个字符（按 UTF-8 字符边界，不是按字节） */
static void input_pop_cp(void)
{
    int n = (int)strlen(g_free_input);
    if (n == 0) return;
    int i = n - 1;
    while (i > 0 && ((unsigned char)g_free_input[i] & 0xC0) == 0x80) i--;
    g_free_input[i] = '\0';
}

static Game  g_game;
static int   g_line_was_read;
static float g_skip_timer;
static char  g_toast[64];
static float g_toast_timer;
static int   g_prev_state = STATE_TITLE;
static int   g_ai_pending;
static int   g_ai_free;        /* 这一次请求是不是「自己说一句」（不走缓存的那种） */
static float g_ai_wait;        /* 已经等了多久（秒）。超过 AI_WAIT_LIMIT 就放弃，
                                  免得网络挂住时界面定在「生成中」不动。 */
#define AI_WAIT_LIMIT 20.0f
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
    ui_set_stream(NULL);      /* 新一轮开始，把上一轮的流式残留清掉 */
    g_line_was_read = is_read(g);

    const Line *ln = game_current_line(g);
    if (ln) {
        ui_begin_line(ln->speaker, ln->text);
        log_push(g, ln->speaker, ln->text);
    } else {
        ui_begin_line("", "");
    }

    /* 配音跟着台词走：一行一个文件，切行即停上一句。
       旁白（speaker 空）和没生成过的行都是静默跳过，不阻塞流程。 */
    {
        const Node *cn = game_current_node(g);
        if (ln && cn) voice_play_line(cn->id, g->line_index, ln->speaker);
        else          voice_stop();
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
    p->stage_index = stage;
}

/* ---------------- 自由输入的返回点 ---------------- */

/* 玩家是在某个"等待选择"的状态下开口的，演完 AI 那段之后要原样回到这里。
   不能走 game_enter_index：那会重跑自动路由，把玩家带去别的事件，
   或者把当前事件从头再演一遍。 */
static int g_free_node = -1;
static int g_free_line = -1;

/* 有保存点就恢复并返回 1 */
static int resume_after_free(Game *g)
{
    if (g_free_node < 0) return 0;

    int n = g_free_node;
    int l = g_free_line;
    g_free_node = g_free_line = -1;

    if (n < 0 || n >= g->node_count) return 0;

    game_restore_index(g, n);          /* 只铺开节点，不跑路由 */
    if (l < 0 || l >= g->nodes[n].line_count) l = g->nodes[n].line_count - 1;
    if (l < 0) l = 0;

    g->line_index     = l;
    g->waiting_choice = 1;             /* 回到"还在等玩家选"的状态 */
    g->finished       = 0;
    start_line(g);
    return 1;
}

/* 请求没发出去（上一次还在跑）：立刻把界面还原，别让玩家干等。
   返回 1 表示已经处理完毕（界面恢复了），调用方直接 return。 */
static int ai_request_failed(Game *g)
{
    g_ai_pending = 0;
    ai_abandon();
    toast("上一次还没结束，稍等一下");

    if (!resume_after_free(g)) {
        game_enter_index(g, g->ai_return_index);
        start_line(g);
    }
    return 1;
}

static void begin_ai_branch(Game *g, const Choice *c, const char *char_id,
                            const char *ret_id, int no_cache)
{
    AiPrompt p;
    fill_ai_prompt(g, c, char_id, &p);

    /* 玩家这句也记进“已知信息”，下一轮生成能接上 */
    memory_push(g, "玩家", p.user_input);

    g->ai_return_index = script_find(g, ret_id);
    if (g->ai_return_index < 0) g->ai_return_index = g->node_index;

    /* 自由输入绕过缓存：同一句话每次都要有新回答，见 AiPrompt.no_cache */
    p.no_cache = no_cache;
    g_ai_free  = no_cache;

    /* 剧本第四列先存着：模型没给判定（离线、或状态栏写坏了）时用它兜底 */
    g_ai_base_aff = c ? c->affinity : 1;

    g->ai_active = 0;
    g_ai_pending = 1;
    g_ai_wait    = 0.0f;

    if (!ai_request(&p)) ai_request_failed(g);
}

/* 自由输入：玩家自己打的一句话，当作一次 AI 支线来演。
   没有剧本第四列可兜底，所以离线时按 0 结算（不奖不罚），
   在线时全看模型的「判定」。 */
static void begin_ai_free(Game *g, const char *text)
{
    Choice tmp;
    memset(&tmp, 0, sizeof(tmp));
    /* text 只有 80 字节，放不下整句；它只用于选项/日志显示，
       真正喂给模型的是 prompt（160 字节）。都按码点截，不切半个汉字。 */
    utf8_truncate(tmp.text,   (int)sizeof(tmp.text),   text, 26);
    utf8_truncate(tmp.prompt, (int)sizeof(tmp.prompt), text, 50);
    tmp.affinity = 0;

    /* 回到玩家说话的那个节点（hub）。自由发言不该把玩家推去别的地方。 */
    const Node *n = game_current_node(g);
    begin_ai_branch(g, &tmp, "shenzhiji", n ? n->id : "hub", 1);
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
        if (!resume_after_free(g)) {
            game_enter_index(g, g->ai_return_index);
            start_line(g);
        }
        return;
    }

    /* 判定落地：以前这一行是被直接丢掉的，模型判了也白判。
       在线时用模型给的分量，离线兜底台词没有状态栏，退回剧本第四列。 */
    {
        int before = g->affinity;
        int delta  = has_judge ? delta_from_judgement(judgement) : g_ai_base_aff;
        apply_affinity(g, delta);

        /* 控制台留一行账：判了多少、折算多少、好感从多少到多少。
           模型偶尔会漏掉状态栏（实测约十次一次），这时退回剧本第四列，
           自由输入没有第四列，就是 0——写清楚免得看着像“它没反应”。 */
        printf("[AI] %s 判定=%+d ×%d → %+d   好感 %d → %d\n",
               has_judge ? "模型" : "缺判定，退回剧本",
               judgement, AI_JUDGE_SCALE, delta, before, g->affinity);

        /* 自由说完给个即时反馈：不然玩家不知道刚才那句话到底算不算数，
           好感没涨就会以为游戏卡住了（表现是「说了也不往前走」）。
           剧本分支不给这个提示——那边有台词和画面在演，不需要额外说明。 */
        if (g_ai_free) {
            if (delta > 0)       toast("她对这句话有反应");
            else if (delta < 0)  toast("她没接这句话");
            else                 toast("她只是听着，没什么变化");
        }
    }

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
    else                           toast("AI 现场生成");   /* 让玩家看得出这是模型写的 */
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
            if (!resume_after_free(g)) {
                game_enter_index(g, g->ai_return_index);
                start_line(g);
            }
        }
        return;
    }

    if (game_advance(g)) start_line(g);
}

/* 好感按选项结算，并限制在 0-100。负面选择会影响后续路由。 */
static void apply_affinity(Game *g, int delta)
{
    g->affinity += delta;
    if (g->affinity < 0)   g->affinity = 0;
    if (g->affinity > 100) g->affinity = 100;
}

/* 按当前节点的 @meter 分流：默认加好感，H 段加 bond（距离/心结）。
   好感在阶段 4 早就封顶，H 段的 +2/+3 加在好感上等于没有，所以单独记一条。 */
static void apply_delta(Game *g, int delta)
{
    const Node *n = game_current_node(g);
    if (n && n->meter == METER_BOND) {
        g->bond += delta;
        if (g->bond < 0)   g->bond = 0;
        if (g->bond > 100) g->bond = 100;
        return;
    }
    apply_affinity(g, delta);
}

static void handle_choice(Game *g)
{
    const Node *n = game_current_node(g);
    if (!n || !g->waiting_choice) return;

    int i = ui_choice_hit(GetMousePosition(), n, g->affinity, game_seen_count(g));
    if (i < 0 || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;

    audio_se_click();

    /* 虚拟选项「（自己说一句）」：索引 == choice_count，切到输入界面 */
    if (i >= n->choice_count) {
        g_free_node = g->node_index;
        g_free_line = g->line_index;
        g_free_input[0] = '\0';
        g_game.state = STATE_INPUT;
        return;
    }

    /* 剧本里也可以显式写 -> （自己说一句）|@input，效果一样 */
    if (strcmp(n->choices[i].target, "@input") == 0) {
        g_free_node = g->node_index;
        g_free_line = g->line_index;
        g_free_input[0] = '\0';
        g_game.state = STATE_INPUT;
        return;
    }

    if (choice_is_ai(&n->choices[i])) {
        char cid[MAX_ID], rid[MAX_ID];
        choice_split_ai(&n->choices[i], cid, MAX_ID, rid, MAX_ID);
        /* 好感等生成结束再结算：模型会给出判定，见 finish_ai_branch */
        begin_ai_branch(g, &n->choices[i], cid, rid, 0);
        return;
    }

    int idx = script_find(g, n->choices[i].target);
    if (idx >= 0) {
        memory_push(g, "玩家", n->choices[i].prompt[0] ? n->choices[i].prompt
                                                       : n->choices[i].text);
        apply_delta(g, n->choices[i].affinity);

        int from = g->node_index;
        game_enter_index(g, idx);
        start_line(g);

        /* 自环且没被路由带走：说明当前好感下已经没有可触发的事件了。
           以前这里什么都不提示，玩家会一直点「继续坐着」，以为游戏卡住了。 */
        if (idx == from && g->node_index == from) {
            if (game_seen_count(g) >= 6)
                toast("今天没有别的事了 · 收拾东西可以走了");
            else
                toast("还早 · 再坐着看看");
        }
    }
}

static int hovered_choice(Game *g)
{
    const Node *n = game_current_node(g);
    if (!n || !g->waiting_choice) return -1;
    return ui_choice_hit(GetMousePosition(), n, g->affinity, game_seen_count(g));
}

/* ---------------- 各状态的输入 ---------------- */

static void update_title(void)
{
    int act = menu_update_title();
    if (act != TITLE_NONE) audio_se_click();

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
        case TITLE_CONFIG:
            g_prev_state = STATE_TITLE;
            menu_config_open();
            g_game.state = STATE_CONFIG;
            break;
        case TITLE_QUIT:
            g_quit = 1;
            break;
        default: break;
    }
}

/* ---------------- 自由输入界面 ---------------- */

/* 去掉首尾空白，顺便把连续空格压成一个——模型对一长串空格没兴趣 */
static void trim_input(char *s)
{
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);

    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t')) s[--n] = '\0';
}

static void update_input(void)
{
    if (IsKeyPressed(KEY_ESCAPE)) {
        g_game.state = STATE_GAME;
        return;
    }

    /* Ctrl+V 粘贴：中文输入法在 raylib 里没有候选窗，这是最实用的一条路 */
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
        IsKeyPressed(KEY_V)) {
#if defined(_WIN32)
        if (clipboard_paste_utf8(g_free_input, FREE_INPUT_MAX) > 0)
            audio_se_click();
#endif
    }

    if (IsKeyPressed(KEY_BACKSPACE)) input_pop_cp();

    int ch;
    while ((ch = GetCharPressed()) > 0) input_append_cp(ch);

    if (IsKeyPressed(KEY_ENTER)) {
        trim_input(g_free_input);
        if (!g_free_input[0]) return;

        audio_se_click();
        g_game.state = STATE_GAME;
        begin_ai_free(&g_game, g_free_input);
    }
}

static void draw_input(void)
{
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 4, 6, 9, 210 });

    const char *title = "说点什么";
    float tw = ui_text_width(title, 26);
    ui_draw_text(title, (SCREEN_W - tw) * 0.5f, 210, 26,
                 (Color){ 232, 196, 132, 255 });

    Rectangle box = { (SCREEN_W - 720) * 0.5f, 268, 720, 150 };
    DrawRectangleRounded(box, 0.12f, 8, (Color){ 18, 22, 28, 255 });
    DrawRectangleRoundedLines(box, 0.12f, 8, (Color){ 96, 116, 136, 255 });

    /* 按字符折行，每行 26 个汉字左右 */
    if (g_free_input[0]) {
        char buf[FREE_INPUT_MAX];
        snprintf(buf, sizeof(buf), "%s", g_free_input);

        float y = box.y + 26;
        int   row = 0;
        char *p = buf;
        while (p && *p && row < 4) {
            char seg[128];
            utf8_truncate(seg, (int)sizeof(seg), p, 26);

            float w = ui_text_width(seg, 22);
            ui_draw_text(seg, box.x + 24, y, 22, (Color){ 226, 220, 208, 255 });
            (void)w;

            p += strlen(seg);
            y += 34;
            row++;
        }

        /* 光标：跟在最后一行末尾闪 */
        if (((int)(GetTime() * 2)) % 2 == 0) {
            char seg[128];
            utf8_truncate(seg, (int)sizeof(seg), buf, 26);
            const char *last = buf;
            while (strlen(last) > 0 && strcmp(last, seg) != 0) {
                if (strlen(last) <= strlen(seg)) break;
                /* 找最后一段 */
                last += strlen(seg);
                if (*last == '\0') break;
                seg[0] = '\0';
                utf8_truncate(seg, (int)sizeof(seg), last, 26);
            }
            float w = ui_text_width(seg, 22);
            float cy = box.y + 26 + (row - 1) * 34;
            DrawRectangle((int)(box.x + 24 + w + 2), (int)cy, 2, 24,
                          (Color){ 232, 196, 132, 255 });
        }
    } else {
        ui_draw_text("（在这里输入，或者 Ctrl+V 粘贴）", box.x + 24, box.y + 28, 20,
                     (Color){ 92, 104, 118, 255 });
    }

    const char *hint = "Enter 说出口    Esc 取消    Ctrl+V 粘贴（中文建议在别处打好再粘）";
    float hw = ui_text_width(hint, 16);
    ui_draw_text(hint, (SCREEN_W - hw) * 0.5f, box.y + box.height + 28, 16,
                 (Color){ 110, 122, 138, 255 });
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

static void update_config(void)
{
    if (menu_update_config()) g_game.state = STATE_TITLE;
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
        /* 生成中按 Esc 只取消这一次，不退游戏——玩家多半只是不想等了，
           直接把人踢回标题太粗暴。 */
        if (g_ai_pending) {
            g_ai_pending = 0;
            ai_abandon();
            toast("已跳过这次生成");
            if (!resume_after_free(&g_game)) {
                game_enter_index(&g_game, g_game.ai_return_index);
                start_line(&g_game);
            }
            return;
        }
        voice_stop();
        g_game.state = STATE_TITLE;
        return;
    }
    if (IsKeyPressed(KEY_S)) {
        voice_stop();
        g_prev_state = STATE_GAME;
        g_game.state = STATE_SAVE;
        audio_se_page();
        return;
    }
    if (IsKeyPressed(KEY_L)) {
        voice_stop();
        g_prev_state = STATE_GAME;
        g_game.state = STATE_LOAD;
        audio_se_page();
        return;
    }
    if (IsKeyPressed(KEY_TAB)) {
        voice_stop();
        g_game.state = STATE_LOG;
        audio_se_page();
        return;
    }
    if (IsKeyPressed(KEY_V)) {
        voice_set_enabled(!voice_enabled());
        toast(voice_enabled() ? "配音已开" : "配音已关");
    }
    if (IsKeyPressed(KEY_M)) {
        audio_set_bgm_enabled(!audio_bgm_enabled());
        toast(audio_bgm_enabled() ? "雨声已开" : "雨声已关");
    }

    if (g_ai_pending) { voice_stop(); return; }   /* 等待生成时不接受推进输入 */

    if (g_game.waiting_choice) {
        voice_stop();                              /* 停在选项上不用再念 */
        handle_choice(&g_game);
        return;
    }

    int advance_key = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
                      IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER);

    if (advance_key) {
        audio_se_click();
        if (!ui_is_done()) ui_finish();
        else               advance_game(&g_game);
    }

    if (fast && !g_game.finished && !g_game.waiting_choice && ui_is_done()) {
        voice_stop();                              /* 快进时不出声，否则一路叠着叫 */
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

/* 生成期间把已收到的部分喂给对话框。
   以前这里是「全屏压暗 + 居中一行『正在生成……』」，画面整个黑一下再弹开，
   很跳。现在只在对话框里长字，背景、立绘、标题都不动，看起来就是她在说话。 */
static void feed_ai_stream(void)
{
    static char raw[AI_REPLY_MAX];
    static char tail[900];

    if (!g_ai_pending || !ai_is_configured() || ai_from_cache() == 2) {
        ui_set_stream(NULL);
        return;
    }

    int n = ai_partial_copy(raw, (int)sizeof(raw));
    if (n <= 0) { ui_set_stream(NULL); return; }

    /* 只留最后 3 个非状态栏的行：前面的玩家已经看过了，
       状态栏【判定：…】是引擎读的，不能出现在剧情里。 */
    char *keep[3] = { NULL, NULL, NULL };
    int   rows = 0;
    char *line = raw;

    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        if (line[0] && strncmp(line, "【", 3) != 0) {
            if (rows < 3) {
                keep[rows++] = line;
            } else {
                keep[0] = keep[1];
                keep[1] = keep[2];
                keep[2] = line;
            }
        }
        if (!nl) break;
        line = nl + 1;
    }

    tail[0] = '\0';
    for (int i = 0; i < rows; i++) {
        if (i) strncat(tail, "\n", sizeof(tail) - strlen(tail) - 1);
        strncat(tail, keep[i], sizeof(tail) - strlen(tail) - 1);
    }
    ui_set_stream(tail);
}

static void draw_ai_pending(void)
{
    /* 没有 key 时给个说明，其余情况什么都不画——正文区已经在长字了 */
    if (ai_is_configured()) return;

    const char *msg = "未检测到 API key，使用离线兜底文本";
    float w = ui_text_width(msg, 16);
    ui_draw_text(msg, (SCREEN_W - w) * 0.5f, SCREEN_H - 92, 16,
                 (Color){ 120, 132, 146, 200 });
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
    int         shot_bond = 0;
    int         shot_pick = -1;
    int         dump_prompt = 0;
    int         parse_demo = 0;
    float       skip_sim = 0.0f;
    const char *script_path = "data/main.gal";
    const char *seen_list = NULL;
    const char *history_seed = NULL;
    int         check_mode = 0;
    int         voice_check = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc)      shot = argv[++i];
        else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) script_path = argv[++i];
        else if (strcmp(argv[i], "--seen") == 0 && i + 1 < argc) seen_list = argv[++i];
        else if (strcmp(argv[i], "--history") == 0 && i + 1 < argc) history_seed = argv[++i];
        else if (strcmp(argv[i], "--node") == 0 && i + 1 < argc) shot_node = argv[++i];
        else if (strcmp(argv[i], "--aff") == 0 && i + 1 < argc)  shot_aff = atoi(argv[++i]);
        else if (strcmp(argv[i], "--bond") == 0 && i + 1 < argc) shot_bond = atoi(argv[++i]);
        else if (strcmp(argv[i], "--pick") == 0 && i + 1 < argc) shot_pick = atoi(argv[++i]);
        else if (strcmp(argv[i], "--skip-sim") == 0 && i + 1 < argc) skip_sim = (float)atof(argv[++i]);
        else if (strcmp(argv[i], "--prompt") == 0) dump_prompt = 1;
        else if (strcmp(argv[i], "--parse-demo") == 0) parse_demo = 1;
        else if (strcmp(argv[i], "--check") == 0) check_mode = 1;
        else if (strcmp(argv[i], "--voice-check") == 0) voice_check = 1;
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

    /* 配音覆盖检查：不需要声卡、不需要模型，只看 data/voice/ 里缺哪几句。
       改了台词（或加了新节点）之后跑一遍，就知道要补哪几句：
           gal.exe --voice-check
       退出码 0 = 全齐，1 = 有缺口。 */
    if (voice_check) {
        int total = 0, have = 0;
        char path[192];

        for (int i = 0; i < g_game.node_count; i++) {
            const Node *n = &g_game.nodes[i];
            for (int j = 0; j < n->line_count; j++) {
                const Line *ln = &n->lines[j];
                if (!ln->speaker[0]) continue;          /* 旁白不配音 */

                total++;
                snprintf(path, sizeof(path), "data/voice/%s_%d.ogg", n->id, j);
                if (FileExists(path)) { have++; continue; }
                snprintf(path, sizeof(path), "data/voice/%s_%d.wav", n->id, j);
                if (FileExists(path)) { have++; continue; }

                printf("缺配音: %s_%d  %s|%s\n", n->id, j, ln->speaker, ln->text);
            }
        }
        printf("配音覆盖: %d/%d 句%s\n", have, total,
               have == total ? "" : "（缺的用 python tools/gen_voice.py 补）");
        return have == total ? 0 : 1;
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
    audio_init();
    voice_init();
    ai_init();

    /* 调试模式（截图 / 快进模拟）默认不出声：省掉音频解码，也免得半句话卡在缓冲里。
       GAL_VOICE=1 可以强行保留，用来验证「这一行到底有没有配到音」。 */
    if (shot || skip_sim > 0.0f) {
        const char *force = getenv("GAL_VOICE");
        if (!(force && force[0] == '1')) voice_set_enabled(0);
    }

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
                    if (choice_visible(&cn->choices[i], g_game.affinity,
                                       game_seen_count(&g_game))) vis++;
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
            g_game.bond = shot_bond;
            game_enter_node(&g_game, shot_node ? shot_node : "start");
            g_game.state = STATE_GAME;
            start_line(&g_game);

            const Node *rn = game_current_node(&g_game);
            printf("node=%s  aff=%d  bond=%d  lines=%d  toast=%s\n",
                   rn ? rn->id : "?", g_game.affinity, g_game.bond, rn ? rn->line_count : 0,
                   (rn && rn->toast[0]) ? rn->toast : "-");
            if (rn && rn->toast[0]) toast(rn->toast);

            /* 调试：模拟点第 N 个选项（只支持普通分支），用来验证数值和跳转 */
            if (shot_pick >= 0 && rn && shot_pick < rn->choice_count) {
                const Choice *c = &rn->choices[shot_pick];
                int tgt = script_find(&g_game, c->target);
                if (tgt >= 0) {
                    apply_delta(&g_game, c->affinity);
                    game_enter_node(&g_game, c->target);
                    start_line(&g_game);
                    const Node *nn = game_current_node(&g_game);
                    printf("picked=%d -> node=%s aff=%d bond=%d\n",
                           shot_pick, nn ? nn->id : "?", g_game.affinity, g_game.bond);
                } else {
                    printf("picked=%d -> 目标 %s 不存在或不是普通分支\n", shot_pick, c->target);
                }
            }
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
                begin_ai_branch(&g_game, &sn->choices[picked], cid, rid, 0);

                /* 异步请求要等：以前这里只 poll 一次，网络稍慢就拿到空结果，
                   看起来像"接了 key 也没反应"。最多等 40 秒。 */
                for (int wait = 0; wait < 800; wait++) {
                    ai_poll();
                    if (ai_state() != AI_BUSY) break;
                    WaitTime(0.05);
                }

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
        } else if (strcmp(shot, "config") == 0) {
            menu_config_open();
            g_game.state = STATE_CONFIG;
        } else if (strcmp(shot, "input") == 0) {
            /* 自由输入界面：预填一句话，用来检查排版与折行 */
            game_restart(&g_game);
            game_enter_node(&g_game, "meet");
            g_game.state = STATE_GAME;
            start_line(&g_game);
            snprintf(g_free_input, sizeof(g_free_input), "%s",
                     "我把《成都通史》还了，顺便帮你也查了那本书在不在架上。");
            g_game.state = STATE_INPUT;
        } else if (strcmp(shot, "stream") == 0) {
            /* 模拟「正在生成」：对话框里一段长到一半的文本，检查是否还有全屏遮罩 */
            game_restart(&g_game);
            game_enter_node(&g_game, "meet");
            g_game.state = STATE_GAME;
            start_line(&g_game);

            static char demo[] =
                "沈知霁：……你查了哪本。\n"
                "旁白：她笔停了一下，抬眼看过来，手指还压";
            ui_set_stream(demo);
            g_ai_pending = 1;
        } else if (strcmp(shot, "free") == 0) {
            /* 走完整条自由输入链路：hub → 输入 → AI 生成 → 回 hub。
               用来在没有手动输入的情况下验证这条路通不通。 */
            game_restart(&g_game);
            g_game.affinity = (shot_aff > 0) ? shot_aff : 55;
            game_enter_node(&g_game, "hub");
            g_game.state = STATE_GAME;
            start_line(&g_game);

            const char *say = "我把《成都通史》还了，顺便帮你也查了那本书在不在架上。";
            printf("自由输入: %s\n", say);

            begin_ai_free(&g_game, say);
            for (int wait = 0; wait < 800; wait++) {
                ai_poll();
                if (ai_state() != AI_BUSY) break;
                WaitTime(0.05);
            }
            if (ai_state() == AI_DONE) finish_ai_branch(&g_game);

            printf("ai_state=%d  source=%d  lines=%d  aff=%d\n",
                   ai_state(), ai_from_cache(), g_game.ai_node.line_count, g_game.affinity);
            for (int k = 0; k < g_game.ai_node.line_count; k++)
                printf("  [%d] %s：%s\n", k,
                       g_game.ai_node.lines[k].speaker, g_game.ai_node.lines[k].text);
            printf("返回节点 index=%d（应为 hub）\n", g_game.ai_return_index);
        }

        ui_finish();
        ui_scene_finish();          /* 别把黑场拍进截图 */
        for (int i = 0; i < 2; i++) {
            BeginDrawing();
            fix_hidpi_viewport();

            if (g_game.state == STATE_TITLE)      menu_draw_title();
            else if (g_game.state == STATE_SAVE)  menu_draw_slots(&g_game, 1);
            else if (g_game.state == STATE_LOAD)  menu_draw_slots(&g_game, 0);
            else if (g_game.state == STATE_LOG)   menu_draw_log(&g_game);
            else if (g_game.state == STATE_CONFIG) menu_draw_config();
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
        voice_shutdown();          /* 必须先于 audio_shutdown 卸载音频 */
        audio_shutdown();
        sprite_shutdown();
        bg_shutdown();
        ui_shutdown();
        CloseWindow();
        return 0;
    }

    while (!WindowShouldClose() && !g_quit) {
        float dt = GetFrameTime();

        ai_poll();
        feed_ai_stream();      /* 已收到的部分喂给对话框正文 */

        /* 客户端超时兜底：curl 自己有 20 秒超时，但进程卡死、DNS 挂起之类
           的情况仍可能不返回。等超过 AI_WAIT_LIMIT 就主动放弃，恢复界面。 */
        if (g_ai_pending) {
            g_ai_wait += dt;
            if (g_ai_wait > AI_WAIT_LIMIT) {
                g_ai_pending = 0;
                ai_abandon();
                toast("生成超时，已跳过（可按 Esc 取消）");
                if (!resume_after_free(&g_game)) {
                    game_enter_index(&g_game, g_game.ai_return_index);
                    start_line(&g_game);
                }
            }
        }

        if (g_ai_pending && ai_state() == AI_DONE)   finish_ai_branch(&g_game);
        if (g_ai_pending && ai_state() == AI_FAILED) {
            g_ai_pending = 0;
            toast(ai_error());
            /* 失败也别白选：按剧本第四列结算，和离线兜底保持一致，
               否则同一个选项“联网成功按判定、断网 0 分”，节奏对不上。 */
            apply_affinity(&g_game, g_ai_base_aff);
            if (!resume_after_free(&g_game)) {
                game_enter_index(&g_game, g_game.ai_return_index);
                start_line(&g_game);
            }
        }

        switch (g_game.state) {
            case STATE_TITLE: update_title(); break;
            case STATE_GAME:  update_game(dt);  break;
            case STATE_SAVE:
            case STATE_LOAD:  update_slots(); break;
            case STATE_LOG:   update_log();   break;
            case STATE_CONFIG: update_config(); break;
            case STATE_INPUT:  update_input();  break;
            default: break;
        }

        int fast = (g_game.state == STATE_GAME) &&
                   (g_fast_force ||
                    IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
        ui_tick(dt, fast);

        audio_update();
        voice_update();
        ui_scene_tick(dt, g_game.bg);

        /* 进入新节点时显示节点提示；一次性事件代表新的一天。 */
        {
            static unsigned last_seq = 0;
            if (g_game.enter_seq != last_seq) {
                last_seq = g_game.enter_seq;
                const Node *cn = game_current_node(&g_game);
                if (cn && cn->toast[0]) toast(cn->toast);
                else if (cn && cn->once_id > 0) toast("又过了一天。");

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
            case STATE_CONFIG:
                menu_draw_config();
                break;
            case STATE_INPUT:
                ui_draw(&g_game);
                draw_input();
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
        ui_draw_scene_fade();       /* 场景切换的整屏淡入，盖在所有东西上面 */

        EndDrawing();
    }

    ai_shutdown();
    voice_shutdown();          /* 必须先于 audio_shutdown 卸载音频 */
    audio_shutdown();
    sprite_shutdown();
    bg_shutdown();
    ui_shutdown();
    CloseWindow();
    return 0;
}
