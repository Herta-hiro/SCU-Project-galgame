#include "game.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* 存档实现说明
 *
 * 只序列化"可存档状态"：节点指针、行指针、好感度、已读位图、背景名、
 * 事件触发位，外加时间戳和一句台词预览（供存读档界面显示）。
 * 瞬时状态（打字机进度、是否等待选择）不入档，读档后从该行开头显示。
 *
 * 关键：三个入口函数共用同一个 save_read_file()。
 * 以前 save_exists 只 fopen 一下、save_slot_info 宽容短读、save_read 要求
 * 精确字节数，于是一个旧档会同时表现为「存在」「有列表信息」「读不出来」。
 * 现在只有一种"什么算合法存档"的判断。
 *
 * 字段逐个写，不整体 fwrite 结构体，避免编译器对齐差异。
 * 只认 SAVE_VERSION 这一版；旧档一律当成没有存档，不做迁移。
 */

/* 一份存档的全部可持久化字段。读的时候先落到这里，再决定给谁用。 */
typedef struct {
    int           node_index;
    int           line_index;
    int           affinity;
    int           bond;
    int           read_bits[MAX_NODES];
    long long     timestamp;
    char          preview[96];
    char          bg[32];
    unsigned char seen[EVENT_SLOTS];
} SaveData;

static void slot_path(int slot, char *buf, int size)
{
    snprintf(buf, (size_t)size, "save%d.sav", slot);
}

/* 取当前行前若干码点作为预览，避免槽位列表里出现半截汉字 */
static void make_preview(const Game *g, char *out, int size)
{
    out[0] = '\0';
    const Line *ln = game_current_line(g);
    if (!ln) return;

    if (ln->speaker[0]) {
        snprintf(out, (size_t)size, "%s：", ln->speaker);
        int used = (int)strlen(out);
        utf8_truncate(out + used, size - used, ln->text, 20);
    } else {
        utf8_truncate(out, size, ln->text, 24);
    }
}

/* 把一份存档完整读进 d。成功返回 1。
   任何一处对不上（magic、版本、字节数）都返回 0，调用方不需要再判断别的。 */
static int save_read_file(int slot, SaveData *d)
{
    if (slot < 0 || slot >= SAVE_SLOTS) return 0;

    char path[64];
    slot_path(slot, path, (int)sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    unsigned int magic = 0, ver = 0;
    int ok = 0;

    memset(d, 0, sizeof(*d));

    if (fread(&magic, sizeof(magic), 1, f) != 1) goto done;
    if (fread(&ver,   sizeof(ver),   1, f) != 1) goto done;
    if (magic != SAVE_MAGIC || ver != SAVE_VERSION) goto done;

    if (fread(&d->node_index, sizeof(int), 1, f) != 1) goto done;
    if (fread(&d->line_index, sizeof(int), 1, f) != 1) goto done;
    if (fread(&d->affinity,   sizeof(int), 1, f) != 1) goto done;
    if (fread(&d->bond,       sizeof(int), 1, f) != 1) goto done;
    if (fread(d->read_bits, sizeof(int), MAX_NODES, f) != MAX_NODES) goto done;
    if (fread(&d->timestamp, sizeof(long long), 1, f) != 1) goto done;
    if (fread(d->preview, 1, sizeof(d->preview), f) != sizeof(d->preview)) goto done;
    if (fread(d->bg, 1, sizeof(d->bg), f) != sizeof(d->bg)) goto done;
    if (fread(d->seen, 1, sizeof(d->seen), f) != sizeof(d->seen)) goto done;

    ok = 1;

done:
    fclose(f);
    return ok;
}

int save_exists(int slot)
{
    SaveData d;
    return save_read_file(slot, &d);
}

int save_write(const Game *g, int slot)
{
    if (slot < 0 || slot >= SAVE_SLOTS) return 0;

    char path[64];
    slot_path(slot, path, (int)sizeof(path));

    FILE *f = fopen(path, "wb");
    if (!f) return 0;

    unsigned int magic = SAVE_MAGIC;
    unsigned int ver   = SAVE_VERSION;
    long long    ts    = (long long)time(NULL);
    char         preview[96];
    make_preview(g, preview, (int)sizeof(preview));

    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&ver,   sizeof(ver),   1, f);
    fwrite(&g->node_index, sizeof(int), 1, f);
    fwrite(&g->line_index, sizeof(int), 1, f);
    fwrite(&g->affinity,   sizeof(int), 1, f);
    fwrite(&g->bond,       sizeof(int), 1, f);
    fwrite(g->read_bits,   sizeof(int), MAX_NODES, f);
    fwrite(&ts, sizeof(ts), 1, f);
    fwrite(preview, 1, sizeof(preview), f);
    fwrite(g->bg, 1, sizeof(g->bg), f);
    fwrite(g->seen, 1, sizeof(g->seen), f);

    fclose(f);
    return 1;
}

int save_read(Game *g, int slot)
{
    SaveData d;
    if (!save_read_file(slot, &d)) return 0;

    if (d.node_index < 0 || d.node_index >= g->node_count) return 0;
    if (d.line_index < 0 || d.line_index >= g->nodes[d.node_index].line_count)
        d.line_index = 0;

    /* 先恢复好感度和事件位，再铺开节点。
       用 game_restore_index 而不是 game_enter_index：后者会跑自动路由，
       而存档里记的已经是路由之后的落点，重跑一次会被当前好感度带去别的分支。
       顺序也有讲究——先设 affinity，再恢复节点，确保后续演出状态一致。

       事件位是**覆盖**不是取并集。取并集会把这局后来演过的事件写进旧档，
       读回 D4 之前时 D4 的"已演"标记还在，hub 的路由命中已演事件就整条停住，
       玩家只剩「安静坐下」——一条死路。存档本来就该是"那一刻的完整状态"，
       读档之后那些事件回到未演，路由才能重新触发。 */
    g->affinity = d.affinity;
    g->bond     = d.bond;
    memcpy(g->seen, d.seen, sizeof(g->seen));
    memcpy(g->read_bits, d.read_bits, sizeof(g->read_bits));

    game_restore_index(g, d.node_index);
    g->line_index = d.line_index;

    /* 节点自己带 @bg 就以剧本为准（可能后来换过图），否则用存档里那张 */
    if (!g->bg[0] && d.bg[0])
        snprintf(g->bg, sizeof(g->bg), "%s", d.bg);

    return 1;
}

int save_slot_info(int slot, SaveSlotInfo *info)
{
    SaveData d;
    memset(info, 0, sizeof(*info));

    if (!save_read_file(slot, &d)) return 0;

    info->exists     = 1;
    info->timestamp  = d.timestamp;
    info->node_index = d.node_index;
    info->line_index = d.line_index;
    info->affinity   = d.affinity;
    memcpy(info->preview, d.preview, sizeof(info->preview));
    info->preview[sizeof(info->preview) - 1] = '\0';

    return 1;
}
