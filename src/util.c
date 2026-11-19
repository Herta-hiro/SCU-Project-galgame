#include "game.h"
#include <string.h>

/* 返回 UTF-8 首字节对应的序列长度；非法字节按 1 处理。 */
static int utf8_len(unsigned char c)
{
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x06) return 2;
    if ((c >> 4) == 0x0E) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

/* 按码点计数，不是按字节。中文一字算一个。 */
int utf8_char_count(const char *s)
{
    int n = 0;
    while (*s) {
        s += utf8_len((unsigned char)*s);
        n++;
    }
    return n;
}

/* 把 src 的前 max_chars 个码点复制到 dst，保证不会切在字节中间。
   返回实际复制的码点数。打字机效果靠这个做，不能按字节截断。 */
int utf8_truncate(char *dst, int dst_size, const char *src, int max_chars)
{
    int chars = 0;
    int pos = 0;

    while (*src && chars < max_chars) {
        int len = utf8_len((unsigned char)*src);
        if (pos + len >= dst_size) break;
        memcpy(dst + pos, src, (size_t)len);
        pos += len;
        src += len;
        chars++;
    }
    dst[pos] = '\0';
    return chars;
}

/* 追加一条对话历史。满了就整体前移一格丢掉最旧的，
   MAX_LOG 只有 128，一次 memmove 约 70KB，每行只做一次，可以接受。 */
void log_push(Game *g, const char *speaker, const char *text)
{
    if (g->log.count >= MAX_LOG) {
        memmove(&g->log.entries[0], &g->log.entries[1],
                sizeof(Line) * (size_t)(MAX_LOG - 1));
        g->log.count = MAX_LOG - 1;
    }

    Line *ln = &g->log.entries[g->log.count++];
    strncpy(ln->speaker, speaker ? speaker : "", sizeof(ln->speaker) - 1);
    ln->speaker[sizeof(ln->speaker) - 1] = '\0';
    strncpy(ln->text, text ? text : "", sizeof(ln->text) - 1);
    ln->text[sizeof(ln->text) - 1] = '\0';
}
