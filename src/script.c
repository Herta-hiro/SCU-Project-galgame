#include "game.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* 剧本格式（UTF-8，扩展名 .gal）
 *
 *   :: 节点id                 开一个新节点
 *   说话人|台词                一行台词
 *   |台词                     旁白（说话人为空）
 *   -> 选项文字|目标节点id      节点末的分支选项
 *   # 注释
 *
 * 台词里不允许出现半角竖线，需要时用全角｜。
 */

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
    if (size <= 0) return;
    strncpy(dst, src, (size_t)(size - 1));
    dst[size - 1] = '\0';
}

/* 第六列 / 路由行：好感条件。支持 aff>=46 / aff>45 / aff<=20 / aff<21 / aff==0 */
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

static void parse_condition(Choice *c, const char *s)
{
    parse_aff_cond(s, &c->cond_min, &c->cond_max);
}

int choice_visible(const Choice *c, int affinity)
{
    if (!c) return 0;
    if (affinity < c->cond_min) return 0;
    if (affinity > c->cond_max) return 0;
    return 1;
}

/* 事件编号：A1=17、B3=35、F15=95……字母 A-F + 1~15 的数字。
   用 16 进制分段，最多 96 个，正好对应 Game.seen 数组。 */
int event_id_of(const char *s)
{
    if (!s || !s[0] || !s[1]) return 0;
    int l = toupper((unsigned char)s[0]) - 'A';
    if (l < 0 || l > 25) return 0;

    int n = atoi(s + 1);
    if (n <= 0 || n > 15) return 0;

    int id = l * 16 + n;
    return (id > 0 && id < 416) ? id : 0;
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

int script_load(Game *g, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    memset(g, 0, sizeof(*g));

    char   buf[MAX_TEXT + 128];
    int    cur = -1;          /* 当前正在写入的节点下标 */
    int    first = 1;

    while (fgets(buf, (int)sizeof(buf), f)) {
        /* 跳过 UTF-8 BOM */
        char *line = buf;
        if (first) {
            unsigned char *u = (unsigned char *)line;
            if (u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) line += 3;
            first = 0;
        }

        line = ltrim(line);
        rtrim(line);

        if (line[0] == '\0' || line[0] == '#') continue;

        /* :: 节点 */
        if (line[0] == ':' && line[1] == ':') {
            if (g->node_count >= MAX_NODES) break;
            cur = g->node_count++;
            memset(&g->nodes[cur], 0, sizeof(Node));
            copy_str(g->nodes[cur].id, MAX_ID, ltrim(line + 2));
            continue;
        }

        if (cur < 0) continue;   /* 节点声明之前的行忽略 */

        /* => 自动路由：`=> aff>=46 目标节点`，进节点时按好感度跳，不需要玩家点 */
        if (line[0] == '=' && line[1] == '>') {
            char *body = ltrim(line + 2);
            char *sp = body;
            while (*sp && *sp != ' ' && *sp != '\t') sp++;
            if (!*sp) continue;
            *sp = '\0';
            char *target = ltrim(sp + 1);
            rtrim(target);
            if (!target[0]) continue;
            if (g->nodes[cur].route_count >= MAX_ROUTES) continue;

            Route *r = &g->nodes[cur].routes[g->nodes[cur].route_count++];
            parse_aff_cond(body, &r->cond_min, &r->cond_max);
            copy_str(r->target, MAX_ID, target);
            continue;
        }

        /* -> 选项：最多六列，后四列可省略 */
        if (line[0] == '-' && line[1] == '>') {
            char *body = ltrim(line + 2);
            if (g->nodes[cur].choice_count >= MAX_CHOICES) continue;

            char *field[6] = { 0 };
            int   nf = 0;
            field[nf++] = body;

            char *p = body;
            while (nf < 6) {
                char *bar = strchr(p, '|');
                if (!bar) break;
                *bar = '\0';
                p = bar + 1;
                field[nf++] = p;
            }
            if (nf < 2) continue;          /* 至少要有 选项文字|目标 */

            Choice *c = &g->nodes[cur].choices[g->nodes[cur].choice_count++];
            copy_str(c->text, (int)sizeof(c->text), field[0]);
            copy_str(c->target, MAX_ID, ltrim(field[1]));
            if (field[2]) copy_str(c->prompt, (int)sizeof(c->prompt), field[2]);
            if (field[3]) copy_str(c->scene,  (int)sizeof(c->scene),  field[3]);
            c->affinity = field[4] ? atoi(ltrim(field[4])) : 1;
            parse_condition(c, field[5] ? ltrim(field[5]) : NULL);

            rtrim(c->text);
            rtrim(c->target);
            rtrim(c->prompt);
            rtrim(c->scene);
            continue;
        }

        /* @bg 背景图名，形如 `@bg lib_window`，同名文件在 data/backgrounds/
           @char 立绘，形如 `@char shenzhiji:soft`，文件在 data/sprites/ */
        if (line[0] == '@') {
            char *body = ltrim(line + 1);
            if (strncmp(body, "bg", 2) == 0) {
                body = ltrim(body + 2);
                copy_str(g->nodes[cur].bg, (int)sizeof(g->nodes[cur].bg), body);
                rtrim(g->nodes[cur].bg);
            } else if (strncmp(body, "char", 4) == 0) {
                body = ltrim(body + 4);
                copy_str(g->nodes[cur].spr, (int)sizeof(g->nodes[cur].spr), body);
                rtrim(g->nodes[cur].spr);
            } else if (strncmp(body, "loc", 3) == 0) {
                body = ltrim(body + 3);
                copy_str(g->nodes[cur].loc, (int)sizeof(g->nodes[cur].loc), body);
                rtrim(g->nodes[cur].loc);
            } else if (strncmp(body, "event", 5) == 0) {
                body = ltrim(body + 5);
                copy_str(g->nodes[cur].event, (int)sizeof(g->nodes[cur].event), body);
                rtrim(g->nodes[cur].event);
            } else if (strncmp(body, "offline", 7) == 0) {
                body = ltrim(body + 7);
                rtrim(body);
                append_line(g->nodes[cur].offline, (int)sizeof(g->nodes[cur].offline), body);
            } else if (strncmp(body, "once", 4) == 0) {
                body = ltrim(body + 4);
                g->nodes[cur].once_id = event_id_of(body);
            } else if (strncmp(body, "toast", 5) == 0) {
                body = ltrim(body + 5);
                copy_str(g->nodes[cur].toast, (int)sizeof(g->nodes[cur].toast), body);
                rtrim(g->nodes[cur].toast);
            }
            continue;
        }

        /* 台词或旁白 */
        if (g->nodes[cur].line_count >= MAX_LINES) continue;

        char *bar = strchr(line, '|');
        Line *ln = &g->nodes[cur].lines[g->nodes[cur].line_count++];

        if (bar) {
            *bar = '\0';
            copy_str(ln->speaker, (int)sizeof(ln->speaker), line);
            copy_str(ln->text, MAX_TEXT, ltrim(bar + 1));
        } else {
            ln->speaker[0] = '\0';
            copy_str(ln->text, MAX_TEXT, line);
        }
    }

    fclose(f);
    return g->node_count > 0;
}

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
