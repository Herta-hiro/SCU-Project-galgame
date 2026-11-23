#include "game.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* 剧本格式（UTF-8，扩展名 .gal）
 *
 *   :: 节点id                       开一个新节点
 *   说话人|台词                      一行台词
 *   |台词                           旁白（说话人为空）
 *   -> 选项文字|目标|玩家这句话|好感|本分支场景|出现条件
 *                                   节点末的分支选项（前四列对普通分支和
 *                                   AI 分支同义，场景只有 AI 分支会读）
 *   => aff>=46 目标节点              进节点时按好感度自动跳
 *   @bg / @char / @loc / @event / @offline / @once / @toast
 *   # 注释
 *
 * 台词里不允许出现半角竖线，需要时用全角｜。
 *
 * 这一层只做「一行文本 → 结构体字段」，不碰文件、不改全局状态。
 * script_load() 负责 IO 和行号，校验器与单元测试都直接调 script_parse_line()。
 */

static int g_err_count;

static void report(int line_no, const char *msg)
{
    g_err_count++;
    fprintf(stderr, "ERROR: 第 %d 行: %s\n", line_no, msg);
}

static void rtrim(char *s)
{
    int n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = '\0';
}

static char *ltrim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void copy_str(char *dst, int size, const char *src)
{
    size_t n;
    if (size <= 0) return;
    n = strlen(src);
    if (n >= (size_t)size) n = (size_t)size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* 好感条件解析。支持 aff>=46 / aff>45 / aff<=20 / aff<21 / aff==0，
   也接受只写 >=46。语法层面的错由调用方先行拦截。 */
static void parse_aff_cond(const char *s, int *pmin, int *pmax)
{
    *pmin = 0;
    *pmax = 100;
    if (!s || !*s) return;

    const char *p = s;
    while (*p == ' ') p++;
    if (strncmp(p, "aff", 3) == 0) p += 3;
    while (*p == ' ') p++;

    char op[3] = { 0 };
    int  n = 0;
    while (*p && (*p == '>' || *p == '<' || *p == '=') && n < 2) op[n++] = *p++;

    int v = 0;
    if (sscanf(p, "%d", &v) != 1) return;

    if      (strcmp(op, ">=") == 0) *pmin = v;
    else if (strcmp(op, ">")  == 0) *pmin = v + 1;
    else if (strcmp(op, "<=") == 0) *pmax = v;
    else if (strcmp(op, "<")  == 0) *pmax = v - 1;
    else if (strcmp(op, "==") == 0 || strcmp(op, "=") == 0) {
        *pmin = v;
        *pmax = v;
    }

    if (*pmin < 0)   *pmin = 0;
    if (*pmax > 100) *pmax = 100;
}

int choice_visible(const Choice *c, int affinity)
{
    if (!c) return 0;
    if (affinity < c->cond_min) return 0;
    if (affinity > c->cond_max) return 0;
    return 1;
}

/* 事件编号：A1=1、B1=17、G6=102……字母下标（A 从 0 起）× 16 + 1~15 的数字。
   用 16 进制分段，最多 EVENT_SLOTS 个，正好对应 Game.seen 数组。 */
int event_id_of(const char *s)
{
    if (!s || !s[0] || !s[1]) return 0;
    int l = toupper((unsigned char)s[0]) - 'A';
    if (l < 0 || l > 25) return 0;

    int n = atoi(s + 1);
    if (n <= 0 || n > 15) return 0;

    int id = l * 16 + n;
    return (id > 0 && id < EVENT_SLOTS) ? id : 0;
}

static void append_line(char *dst, int size, const char *src)
{
    if (!src) return;
    int used = (int)strlen(dst);
    if (used && used < size - 2) {
        dst[used++] = '\n';
        dst[used] = '\0';
    }
    int n = (int)strlen(src);
    if (used + n >= size) n = size - used - 1;
    if (n <= 0) return;
    memcpy(dst + used, src, (size_t)n);
    dst[used + n] = '\0';
}

static int is_int_field(const char *s)
{
    if (*s == '+' || *s == '-') s++;
    if (!*s) return 0;
    for (; *s; s++)
        if (!isdigit((unsigned char)*s)) return 0;
    return 1;
}

/* 按 | 切列，最多 max 列。返回列数；超出的部分留在最后一列里，
   由调用方检测（选项最多 6 列）。 */
static int split_fields(char *body, char *f[], int max)
{
    int n = 0;
    f[n++] = body;
    char *p = body;
    while (n < max) {
        char *bar = strchr(p, '|');
        if (!bar) break;
        *bar = '\0';
        p = bar + 1;
        f[n++] = p;
    }
    return n;
}

static ScriptLineKind parse_choice(char *body, Node *n, char *err, int err_size)
{
    char *f[6] = { 0 };
    int   nf = split_fields(body, f, 6);

    if (nf == 6 && strchr(f[5], '|')) {
        snprintf(err, (size_t)err_size, "选项最多 6 列，多出来的会被吞掉");
        return SL_ERROR;
    }
    if (nf < 2) {
        snprintf(err, (size_t)err_size, "选项至少要写「文字|目标节点」");
        return SL_ERROR;
    }

    char *text   = f[0];
    char *target = ltrim(f[1]);
    rtrim(target);
    rtrim(text);

    if (!target[0]) {
        snprintf(err, (size_t)err_size, "选项没写目标节点");
        return SL_ERROR;
    }
    if (strlen(target) >= MAX_ID) {
        snprintf(err, (size_t)err_size, "目标「%s」超过 %d 字节", target, MAX_ID - 1);
        return SL_ERROR;
    }

    int aff = 1;
    if (nf >= 4) {
        char *a = ltrim(f[3]);
        if (*a) {
            if (!is_int_field(a)) {
                snprintf(err, (size_t)err_size,
                         "第四列是好感度增减，要写成 +2 / -3，实际是「%s」", a);
                return SL_ERROR;
            }
            aff = atoi(a);
        }
    }

    if (n->choice_count >= MAX_CHOICES) {
        snprintf(err, (size_t)err_size, "选项超过 %d 个，多的被丢弃", MAX_CHOICES);
        return SL_ERROR;
    }

    Choice *c = &n->choices[n->choice_count];
    memset(c, 0, sizeof(*c));
    copy_str(c->text,   (int)sizeof(c->text), text);
    copy_str(c->target, MAX_ID, target);
    if (nf >= 3) {
        char *p = f[2];
        rtrim(p);
        copy_str(c->prompt, (int)sizeof(c->prompt), p);
    }
    c->affinity = aff;
    if (nf >= 5) {
        char *s = f[4];
        rtrim(s);
        if (s[0] && target[0] != '@') {
            /* 普通分支没有场景可覆盖。这一列写了内容，多半是把好感写错了位置
               （历史上真发生过：18 个选项的 +2/-3 全落在这里，被当成场景名丢掉）。 */
            snprintf(err, (size_t)err_size,
                     "普通分支的第五列要留空（好感写在第四列），实际是「%s」", s);
            return SL_ERROR;
        }
        copy_str(c->scene, (int)sizeof(c->scene), s);
    }
    parse_aff_cond(nf >= 6 ? ltrim(f[5]) : NULL, &c->cond_min, &c->cond_max);

    n->choice_count++;
    return SL_CHOICE;
}

ScriptLineKind script_parse_line(const char *raw, Node *n, char *err, int err_size)
{
    char buf[MAX_TEXT + 128];

    if (err && err_size > 0) err[0] = '\0';
    if (!raw || !n) {
        if (err && err_size > 0) snprintf(err, (size_t)err_size, "内部错误：空指针");
        return SL_ERROR;
    }
    if (strlen(raw) >= sizeof(buf)) {
        snprintf(err, (size_t)err_size, "这一行超过 %d 字节，请拆行", (int)sizeof(buf) - 1);
        return SL_ERROR;
    }
    snprintf(buf, sizeof(buf), "%s", raw);

    char *line = ltrim(buf);
    rtrim(line);

    if (!line[0] || line[0] == '#') return SL_SKIP;

    /* :: 节点 */
    if (line[0] == ':' && line[1] == ':') {
        char *id = ltrim(line + 2);
        rtrim(id);
        if (!id[0]) {
            snprintf(err, (size_t)err_size, "节点 id 是空的");
            return SL_ERROR;
        }
        if (strlen(id) >= MAX_ID) {
            snprintf(err, (size_t)err_size, "节点 id「%s」超过 %d 字节", id, MAX_ID - 1);
            return SL_ERROR;
        }
        copy_str(n->id, MAX_ID, id);
        return SL_NODE;
    }

    /* => 自动路由：`=> aff>=46 目标节点` */
    if (line[0] == '=' && line[1] == '>') {
        char *body = ltrim(line + 2);
        char *sp = body;
        while (*sp && *sp != ' ' && *sp != '\t') sp++;
        if (!*sp) {
            snprintf(err, (size_t)err_size, "路由要写成「=> aff>=46 目标节点」");
            return SL_ERROR;
        }
        *sp = '\0';
        char *target = ltrim(sp + 1);
        rtrim(target);

        if (!target[0]) {
            snprintf(err, (size_t)err_size, "路由没写目标节点");
            return SL_ERROR;
        }
        if (strlen(target) >= MAX_ID) {
            snprintf(err, (size_t)err_size, "路由目标「%s」超过 %d 字节", target, MAX_ID - 1);
            return SL_ERROR;
        }
        if (!strpbrk(body, "<>=")) {
            snprintf(err, (size_t)err_size, "路由条件「%s」里没有比较符号", body);
            return SL_ERROR;
        }
        if (n->route_count >= MAX_ROUTES) {
            snprintf(err, (size_t)err_size, "路由超过 %d 条，多的被丢弃", MAX_ROUTES);
            return SL_ERROR;
        }

        Route *r = &n->routes[n->route_count++];
        parse_aff_cond(body, &r->cond_min, &r->cond_max);
        copy_str(r->target, MAX_ID, target);
        return SL_ROUTE;
    }

    /* -> 选项 */
    if (line[0] == '-' && line[1] == '>') {
        char *body = ltrim(line + 2);
        return parse_choice(body, n, err, err_size);
    }

    /* @ 指令：按第一个词精确匹配，`@bgg` 这种拼错会报错而不是被当成 @bg */
    if (line[0] == '@') {
        char *body = ltrim(line + 1);
        char *key  = body;
        char *sp   = key;
        while (*sp && *sp != ' ' && *sp != '\t') sp++;

        char *val = sp;
        if (*sp) {
            *sp  = '\0';
            val  = ltrim(sp + 1);
            rtrim(val);
        }

        if (strcmp(key, "bg") == 0) {
            if (!val[0]) {
                snprintf(err, (size_t)err_size, "@bg 后面要写背景名");
                return SL_ERROR;
            }
            if (strlen(val) >= sizeof(n->bg)) {
                snprintf(err, (size_t)err_size, "@bg 名字超过 %d 字节", (int)sizeof(n->bg) - 1);
                return SL_ERROR;
            }
            copy_str(n->bg, (int)sizeof(n->bg), val);
            return SL_META;
        }
        if (strcmp(key, "char") == 0) {
            if (!val[0]) {
                snprintf(err, (size_t)err_size, "@char 后面要写「角色id:表情」或 off");
                return SL_ERROR;
            }
            if (strlen(val) >= sizeof(n->spr)) {
                snprintf(err, (size_t)err_size, "@char 写法超过 %d 字节", (int)sizeof(n->spr) - 1);
                return SL_ERROR;
            }
            copy_str(n->spr, (int)sizeof(n->spr), val);
            return SL_META;
        }
        if (strcmp(key, "loc") == 0) {
            if (!val[0]) {
                snprintf(err, (size_t)err_size, "@loc 后面要写地点");
                return SL_ERROR;
            }
            if (strlen(val) >= sizeof(n->loc)) {
                snprintf(err, (size_t)err_size, "@loc 超过 %d 字节", (int)sizeof(n->loc) - 1);
                return SL_ERROR;
            }
            copy_str(n->loc, (int)sizeof(n->loc), val);
            return SL_META;
        }
        if (strcmp(key, "event") == 0) {
            if (!val[0]) {
                snprintf(err, (size_t)err_size, "@event 后面要写事件描述");
                return SL_ERROR;
            }
            if (strlen(val) >= sizeof(n->event)) {
                snprintf(err, (size_t)err_size, "@event 超过 %d 字节", (int)sizeof(n->event) - 1);
                return SL_ERROR;
            }
            copy_str(n->event, (int)sizeof(n->event), val);
            return SL_META;
        }
        if (strcmp(key, "offline") == 0) {
            append_line(n->offline, (int)sizeof(n->offline), val);
            return SL_META;
        }
        if (strcmp(key, "once") == 0) {
            int id = event_id_of(val);
            if (!id) {
                snprintf(err, (size_t)err_size,
                         "@once 要写成 A1 / B3 / G6（字母 A-Z + 1-15），实际是「%s」", val);
                return SL_ERROR;
            }
            n->once_id = id;
            return SL_META;
        }
        if (strcmp(key, "toast") == 0) {
            if (!val[0]) {
                snprintf(err, (size_t)err_size, "@toast 后面要写提示文字");
                return SL_ERROR;
            }
            if (strlen(val) >= sizeof(n->toast)) {
                snprintf(err, (size_t)err_size, "@toast 超过 %d 字节", (int)sizeof(n->toast) - 1);
                return SL_ERROR;
            }
            copy_str(n->toast, (int)sizeof(n->toast), val);
            return SL_META;
        }

        snprintf(err, (size_t)err_size,
                 "未知指令 @%s（只有 bg/char/loc/event/offline/once/toast）", key);
        return SL_ERROR;
    }

    /* 台词或旁白 */
    if (n->line_count >= MAX_LINES) {
        snprintf(err, (size_t)err_size, "节点里超过 %d 行，多的被丢弃", MAX_LINES);
        return SL_ERROR;
    }

    char *bar = strchr(line, '|');
    Line *ln  = &n->lines[n->line_count];
    memset(ln, 0, sizeof(*ln));

    const char *speaker = "";
    const char *text    = line;
    if (bar) {
        *bar = '\0';
        speaker = line;
        text    = ltrim(bar + 1);
    }
    if (strlen(speaker) >= sizeof(ln->speaker)) {
        snprintf(err, (size_t)err_size, "说话人超过 %d 字节", (int)sizeof(ln->speaker) - 1);
        return SL_ERROR;
    }
    if (strlen(text) >= MAX_TEXT) {
        snprintf(err, (size_t)err_size, "台词 %d 字节，上限 %d，会被截断",
                 (int)strlen(text), MAX_TEXT - 1);
        return SL_ERROR;
    }

    copy_str(ln->speaker, (int)sizeof(ln->speaker), speaker);
    copy_str(ln->text, MAX_TEXT, text);
    n->line_count++;
    return SL_LINE;
}

int script_load(Game *g, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    memset(g, 0, sizeof(*g));
    g_err_count = 0;

    char buf[MAX_TEXT + 128];
    char err[192];
    int  cur     = -1;          /* 当前正在写入的节点下标 */
    int  first   = 1;
    int  line_no = 0;

    while (fgets(buf, (int)sizeof(buf), f)) {
        char *line = buf;
        line_no++;

        /* 跳过 UTF-8 BOM */
        if (first) {
            unsigned char *u = (unsigned char *)line;
            if (u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) line += 3;
            first = 0;
        }

        line = ltrim(line);

        /* 节点声明要先开槽位再交给解析器填 id */
        if (line[0] == ':' && line[1] == ':') {
            if (g->node_count >= MAX_NODES) {
                report(line_no, "节点数超过上限，后面的全部丢弃");
                break;
            }
            cur = g->node_count++;
            memset(&g->nodes[cur], 0, sizeof(Node));
            if (script_parse_line(line, &g->nodes[cur], err, (int)sizeof(err)) == SL_ERROR)
                report(line_no, err);
            continue;
        }

        if (cur < 0) continue;   /* 节点声明之前的行忽略 */

        if (script_parse_line(line, &g->nodes[cur], err, (int)sizeof(err)) == SL_ERROR)
            report(line_no, err);
    }

    fclose(f);
    return g->node_count > 0;
}

int script_error_count(void) { return g_err_count; }

int script_find(const Game *g, const char *id)
{
    for (int i = 0; i < g->node_count; i++)
        if (strcmp(g->nodes[i].id, id) == 0) return i;
    return -1;
}

/* 把节点状态切开应用一次（不处理路由），供 game_enter_index 复用 */
static void apply_node(Game *g, int index)
{
    g->node_index = index;
    g->line_index = 0;
    g->waiting_choice = 0;
    g->finished = 0;

    /* 节点带背景就切过去；不带就沿用上一张（连续场景不用重复写） */
    if (g->nodes[index].bg[0])
        snprintf(g->bg, sizeof(g->bg), "%s", g->nodes[index].bg);

    /* 立绘同理；写 `@char off` 表示这个节点不出现人 */
    if (g->nodes[index].spr[0])
        snprintf(g->spr, sizeof(g->spr), "%s", g->nodes[index].spr);

    /* 地点与事件：都会进提示词，让模型知道“现在在哪、刚发生了什么” */
    if (g->nodes[index].loc[0])
        snprintf(g->location, sizeof(g->location), "%s", g->nodes[index].loc);
    if (g->nodes[index].event[0]) {
        snprintf(g->last_event, sizeof(g->last_event), "%s", g->nodes[index].event);

        /* 事件清单：滚动保留，喂给提示词的“已经发生的事”，让她记得你帮过什么 */
        int used = (int)strlen(g->event_history);
        if (used + (int)strlen(g->nodes[index].event) + 4 < (int)sizeof(g->event_history)) {
            if (used) {
                strcat(g->event_history, "；");   /* 全角分号是 3 字节，别写成多字节字符常量 */
                used = (int)strlen(g->event_history);
            }
            snprintf(g->event_history + used,
                     sizeof(g->event_history) - (size_t)used, "%s", g->nodes[index].event);
        }
    }

    /* @once：进过一次就记下来，之后 hub 的路由会跳过它 */
    if (g->nodes[index].once_id > 0)
        g->seen[g->nodes[index].once_id] = 1;

    /* 每次进节点都 +1：main 靠它判断“又进了一次同一个节点”，好重放 @toast */
    g->enter_seq++;

    /* 记下进这一场时的好感度，给“越界扣分但不倒退”兜底 */
    g->event_entry_aff = g->affinity;
}

void game_enter_index(Game *g, int index)
{
    if (index < 0 || index >= g->node_count) return;

    apply_node(g, index);

    /* 自动路由：一路跳到底，最多 32 跳，防手滑写出环 */
    for (int guard = 0; guard < 32; guard++) {
        const Node *n = &g->nodes[g->node_index];
        int next = -1;

        for (int i = 0; i < n->route_count; i++) {
            const Route *r = &n->routes[i];
            if (g->affinity < r->cond_min || g->affinity > r->cond_max) continue;

            int t = script_find(g, r->target);
            if (t < 0) continue;

            /* 演过一次的事件不再进：本档事件演过就停在 hub，不往低档退回 */
            int once = g->nodes[t].once_id;
            if (once > 0 && g->seen[once]) {
                next = -1;
                break;
            }

            next = t;
            break;               /* 从上往下取第一条命中的 */
        }

        if (next < 0 || next == g->node_index) break;
        apply_node(g, next);
    }
}

void game_enter_node(Game *g, const char *id)
{
    int idx = script_find(g, id);
    if (idx >= 0) game_enter_index(g, idx);
}

/* 读档专用：铺开节点但不跑路由。存档里记的节点已经是路由之后的落点，
   再跑一遍会被"当前"好感度带去别的分支（从标题读档时好感度是 0，
   会一路掉到最低档的保底事件，位置就丢了）。 */
void game_restore_index(Game *g, int index)
{
    if (index < 0 || index >= g->node_count) return;
    apply_node(g, index);
}

const Node *game_current_node(const Game *g)
{
    /* AI 支线播放期间，当前节点是临时节点，不在 nodes 数组里 */
    if (g->ai_active) return &g->ai_node;
    if (g->node_index < 0 || g->node_index >= g->node_count) return NULL;
    return &g->nodes[g->node_index];
}

const Line *game_current_line(const Game *g)
{
    const Node *n = game_current_node(g);
    if (!n || g->line_index < 0 || g->line_index >= n->line_count) return NULL;
    return &n->lines[g->line_index];
}

/* 推进一行。返回 0 表示停在当前行（已到末尾/等待选择），1 表示前进了一行。 */
int game_advance(Game *g)
{
    const Node *n = game_current_node(g);
    if (!n) return 0;

    if (g->line_index + 1 < n->line_count) {
        g->line_index++;
        return 1;
    }

    int visible = 0;
    for (int i = 0; i < n->choice_count; i++)
        if (choice_visible(&n->choices[i], g->affinity)) visible++;

    if (visible > 0) {
        g->waiting_choice = 1;
        return 0;
    }

    g->finished = 1;
    return 0;
}

/* 回到起点：节点、好感度、已读位、历史全部重置 */
void game_restart(Game *g)
{
    g->node_index     = 0;
    g->line_index     = 0;
    g->affinity       = 0;
    g->waiting_choice = 0;
    g->finished       = 0;
    g->ai_active      = 0;
    g->ai_return_index = -1;
    g->log.count      = 0;
    g->ai_memory[0]   = '\0';
    g->bg[0]          = '\0';
    g->spr[0]         = '\0';
    g->location[0]    = '\0';
    g->last_event[0]  = '\0';
    g->event_history[0] = '\0';
    g->event_entry_aff = 0;
    memset(g->seen, 0, sizeof(g->seen));
    memset(g->read_bits, 0, sizeof(g->read_bits));

    /* 关键：重置后要真正进入第一个节点，把它的 @bg / @char 应用到
       g->bg / g->spr。否则新游戏开场前几页 g->bg 为空，退回纯深色渐变，
       要等玩家点完第一个选项、走 game_enter_index 才有背景。 */
    game_enter_index(g, 0);
}

int choice_is_ai(const Choice *c)
{
    return c && c->target[0] == '@';
}

/* 把 "@linxia:after_talk" 拆成角色 id 和返回节点 id。
   没写冒号时返回节点留空，由调用方决定默认值。 */
void choice_split_ai(const Choice *c, char *char_id, int char_size,
                     char *ret_id, int ret_size)
{
    char_id[0] = '\0';
    ret_id[0]  = '\0';
    if (!choice_is_ai(c)) return;

    const char *p = c->target + 1;
    const char *colon = strchr(p, ':');

    if (colon) {
        int n = (int)(colon - p);
        if (n >= char_size) n = char_size - 1;
        memcpy(char_id, p, (size_t)n);
        char_id[n] = '\0';
        copy_str(ret_id, ret_size, colon + 1);
    } else {
        copy_str(char_id, char_size, p);
    }
}

/* ---------------- 模型回复 ---------------- */

/* 状态栏长这样：
     【好感：55/100｜阶段：2｜记忆：玩家问了索引｜判定：+2，资料相关】
   判定值要取出来给引擎用（以前是直接丢掉，等于模型白判）。
   认三种开头：以【起行、含「｜判定」、或同时含「判定：」和「好感」。 */
static int is_status_line(const char *s)
{
    if (strncmp(s, "【", 3) == 0) return 1;
    if (strstr(s, "｜判定")) return 1;
    if (strstr(s, "判定：") && strstr(s, "好感")) return 1;
    return 0;
}

/* 从状态栏取判定增减。取不到返回 0 且 *found = 0。
   模型会写 +2 / 2 / ＋2 / −X / + 2 这些变体，都认。 */
static int parse_judgement(const char *s, int *found)
{
    const char *p;
    *found = 0;

    p = strstr(s, "判定：");
    if (p) {
        p += 9;                       /* 「判定：」= 2 汉字 + 全角冒号 */
    } else {
        p = strstr(s, "判定:");
        if (!p) return 0;
        p += 7;                       /* 「判定:」= 2 汉字 + 半角冒号 */
    }

    while (*p == ' ' || *p == '\t') p++;

    int sign = 1;
    if (*p == '+') {
        p++;
    } else if (*p == '-') {
        sign = -1; p++;
    } else if (strncmp(p, "＋", 3) == 0) {      /* U+FF0B */
        p += 3;
    } else if (strncmp(p, "－", 3) == 0) {      /* U+FF0D */
        sign = -1; p += 3;
    } else if (strncmp(p, "−", 3) == 0) {       /* U+2212 减号 */
        sign = -1; p += 3;
    }

    while (*p == ' ' || *p == '\t') p++;
    if (*p < '0' || *p > '9') return 0;

    int v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        if (v > 999) { v = 999; break; }
        p++;
    }

    *found = 1;
    return sign * v;
}

int parse_reply(const char *reply, Node *out, int *judgement)
{
    int found = 0;

    if (judgement) *judgement = 0;
    if (!reply || !out) return 0;

    out->line_count = 0;
    const char *p = reply;

    while (*p && out->line_count < MAX_LINES) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);

        if (len > 0) {
            char buf[MAX_TEXT];
            if (len >= MAX_TEXT) len = MAX_TEXT - 1;
            memcpy(buf, p, (size_t)len);
            buf[len] = '\0';

            int end = (int)strlen(buf);
            while (end > 0 && (buf[end - 1] == '\r' || buf[end - 1] == ' '))
                buf[--end] = '\0';

            if (buf[0]) {
                if (is_status_line(buf)) {
                    int f = 0;
                    int v = parse_judgement(buf, &f);
                    if (f) {
                        found = 1;
                        if (judgement) *judgement = v;
                    }
                    goto next_line;      /* 状态栏只给引擎看 */
                }

                Line *ln = &out->lines[out->line_count++];
                char *colon = strstr(buf, "：");      /* 全角冒号占 3 字节 */
                int   cw    = 3;

                if (!colon) { colon = strchr(buf, ':'); cw = 1; }

                if (colon) {
                    *colon = '\0';
                    copy_str(ln->speaker, (int)sizeof(ln->speaker), buf);
                    copy_str(ln->text, (int)sizeof(ln->text), colon + cw);
                } else {
                    ln->speaker[0] = '\0';
                    copy_str(ln->text, (int)sizeof(ln->text), buf);
                }
            }
        }

    next_line:
        if (!nl) break;
        p = nl + 1;
    }

    return found;
}
