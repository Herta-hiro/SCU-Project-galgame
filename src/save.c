#include "game.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* 存档实现说明：
 * 只序列化"可存档状态"——节点指针、行指针、好感度、已读位图，
 * 外加时间戳和一句台词预览，供存读档界面显示。
 * 瞬时状态（打字机进度、是否等待选择）不入档，读档后从该行开头显示。
 * 字段逐个写，不整体 fwrite 结构体，避免编译器对齐差异。 */

static void slot_path(int slot, char *buf, int size)
{
    snprintf(buf, (size_t)size, "save%d.sav", slot);
}

int save_exists(int slot)
{
    if (slot < 0 || slot >= SAVE_SLOTS) return 0;
    char path[64];
    slot_path(slot, path, (int)sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
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
    if (slot < 0 || slot >= SAVE_SLOTS) return 0;

    char path[64];
    slot_path(slot, path, (int)sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    unsigned int magic = 0, ver = 0;
    if (fread(&magic, sizeof(magic), 1, f) != 1 ||
        fread(&ver,   sizeof(ver),   1, f) != 1 ||
        magic != SAVE_MAGIC || ver < 1 || ver > SAVE_VERSION) {
        fclose(f);
        return 0;
    }

    int node_index = 0, line_index = 0, affinity = 0;
    if (fread(&node_index, sizeof(int), 1, f) != 1 ||
        fread(&line_index, sizeof(int), 1, f) != 1 ||
        fread(&affinity,   sizeof(int), 1, f) != 1 ||
        fread(g->read_bits, sizeof(int), MAX_NODES, f) != MAX_NODES) {
        fclose(f);
        return 0;
    }

    /* v2 起才有时间戳和预览，旧档跳过即可 */
    if (ver >= 2) {
        long long ts = 0;
        char preview[96];
        if (fread(&ts, sizeof(ts), 1, f) != 1) { fclose(f); return 0; }
        if (fread(preview, 1, sizeof(preview), f) != sizeof(preview)) {
            fclose(f);
            return 0;
        }
    }

    /* v3 起多一个背景图名；旧档没有，靠节点自己的 @bg 补 */
    char saved_bg[32];
    saved_bg[0] = '\0';
    if (ver >= 3) {
        if (fread(saved_bg, 1, sizeof(saved_bg), f) != sizeof(saved_bg)) {
            fclose(f);
            return 0;
        }
    }
    /* v4 起多一个“已触发事件”位图；旧档按 0 处理 */
    unsigned char saved_seen[416];
    memset(saved_seen, 0, sizeof(saved_seen));
    if (ver >= 4) {
        if (fread(saved_seen, 1, sizeof(saved_seen), f) != sizeof(saved_seen)) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    if (node_index < 0 || node_index >= g->node_count) return 0;
    if (line_index < 0 || line_index >= g->nodes[node_index].line_count) line_index = 0;

    game_enter_index(g, node_index);
    g->line_index = line_index;
    g->affinity   = affinity;

    /* 节点自己带 @bg 就以剧本为准（可能后来换过图），否则用存档里那张 */
    if (!g->bg[0] && saved_bg[0])
        snprintf(g->bg, sizeof(g->bg), "%s", saved_bg);

    /* 事件触发记录：并上存档里的，避免读档后重演同一事件 */
    for (int k = 0; k < 416; k++) g->seen[k] |= saved_seen[k];
    return 1;
}

int save_slot_info(int slot, SaveSlotInfo *info)
{
    memset(info, 0, sizeof(*info));
    if (slot < 0 || slot >= SAVE_SLOTS) return 0;

    char path[64];
    slot_path(slot, path, (int)sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    unsigned int magic = 0, ver = 0;
    if (fread(&magic, sizeof(magic), 1, f) != 1 ||
        fread(&ver,   sizeof(ver),   1, f) != 1 ||
        magic != SAVE_MAGIC || ver < 1 || ver > SAVE_VERSION) {
        fclose(f);
        return 0;
    }

    if (fread(&info->node_index, sizeof(int), 1, f) != 1 ||
        fread(&info->line_index, sizeof(int), 1, f) != 1 ||
        fread(&info->affinity,   sizeof(int), 1, f) != 1) {
        fclose(f);
        return 0;
    }

    /* 跳过已读位图，再取时间戳和预览 */
    fseek(f, (long)(sizeof(int) * MAX_NODES), SEEK_CUR);

    if (ver >= 2) {
        if (fread(&info->timestamp, sizeof(long long), 1, f) != 1) {
            info->timestamp = 0;
        }
        if (fread(info->preview, 1, sizeof(info->preview), f) != sizeof(info->preview)) {
            info->preview[0] = '\0';
        }
    }

    fclose(f);
    info->exists = 1;
    return 1;
}
