/* 存档 / 读档的离线单元测试。不依赖 raylib，可直接编译运行。
   编译: gcc tools/test_save.c src/script.c src/save.c src/util.c -Isrc -o test_save.exe */

#include "game.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(const char *name, int cond)
{
    printf("%s  %s\n", cond ? "[ok]  " : "[FAIL]", name);
    if (!cond) failures++;
}

int main(void)
{
    static Game g;

    if (!script_load(&g, "data/main.gal")) {
        printf("剧本加载失败\n");
        return 1;
    }
    printf("节点数 = %d\n\n", g.node_count);

    /* ---- 正常往返 ---- */
    g.node_index  = 3;
    g.line_index  = 0;
    g.affinity    = 7;
    g.read_bits[3] = 0x5;
    g.seen[17] = 1;   /* A1 */
    g.seen[51] = 1;   /* C3 */

    check("保存返回成功", save_write(&g, 2) == 1);
    check("存档文件存在", save_exists(2) == 1);

    memset(&g, 0, sizeof(g));
    script_load(&g, "data/main.gal");

    check("读取返回成功", save_read(&g, 2) == 1);
    check("节点指针还原", g.node_index == 3);
    check("行指针还原", g.line_index == 0);
    check("好感度还原", g.affinity == 7);
    check("已读位图还原", g.read_bits[3] == 0x5);
    check("背景随节点还原", strcmp(g.bg, "bg_lib_window") == 0);
    check("事件触发标记还原", g.seen[17] == 1 && g.seen[51] == 1);

    /* ---- 槽位信息（供存读档界面显示） ---- */
    SaveSlotInfo si;
    check("槽位信息读取成功", save_slot_info(2, &si) == 1);
    check("槽位信息标记为存在", si.exists == 1);
    check("槽位信息带时间戳", si.timestamp > 0);
    check("槽位预览非空", si.preview[0] != '\0');
    check("空槽位信息读取失败", save_slot_info(1, &si) == 0);

    /* ---- 越界行号应被夹回 0 ---- */
    g.node_index = 3;
    g.line_index = 9;          /* honest 节点只有 1 行 */
    save_write(&g, 2);
    save_read(&g, 2);
    check("越界行号夹回 0", g.line_index == 0);

    /* ---- 空槽位读取应失败 ---- */
    check("空槽位读取返回失败", save_read(&g, 1) == 0);

    /* ---- 越界槽位应被拒绝 ---- */
    check("非法槽位写拒绝", save_write(&g, 9) == 0);

    /* ---- 破坏 magic 后应拒绝读取 ---- */
    g.node_index = 1;
    g.line_index = 0;
    save_write(&g, 2);
    {
        FILE *f = fopen("save2.sav", "r+b");
        if (f) {
            unsigned int bad = 0xDEADBEEF;
            fwrite(&bad, sizeof(bad), 1, f);
            fclose(f);
        }
        check("magic 损坏后拒绝读取", save_read(&g, 2) == 0);
    }

    printf("\n%s\n", failures == 0 ? "全部通过" : "存在失败项");
    return failures == 0 ? 0 : 1;
}
