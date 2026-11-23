/* 存档 / 读档的离线单元测试。不依赖 raylib，可直接编译运行。
   编译: gcc tools/test_save.c src/script.c src/save.c src/util.c -Isrc -o test_save.exe

   注意：测试会占用（并先清空）当前目录的 save0~2.sav。要在 gal 根目录跑
   （它要读 data/main.gal），所以别在放着真实存档的目录里跑。 */

#include "game.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(const char *name, int cond)
{
    printf("%s  %s\n", cond ? "[ok]  " : "[FAIL]", name);
    if (!cond) failures++;
}

/* 槽位是磁盘上的共享状态：上一次跑留下的存档会让"空槽位应该失败"这类断言
   莫名其妙地红。每次开跑先清干净，测试就不再依赖环境。 */
static void clear_slots(void)
{
    for (int i = 0; i < SAVE_SLOTS; i++) {
        char path[32];
        snprintf(path, sizeof(path), "save%d.sav", i);
        remove(path);
    }
}

int main(void)
{
    static Game g;

    clear_slots();

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
    /* 用 event_id_of 反查下标，别手算 —— 之前这里写死 17/51 并标成 A1/C3，
       实际 A1=1、C3=35，注释和数字一起错了。 */
    g.seen[event_id_of("A1")] = 1;
    g.seen[event_id_of("C3")] = 1;

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
    check("事件触发标记还原", g.seen[event_id_of("A1")] == 1 &&
                              g.seen[event_id_of("C3")] == 1);

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

    /* ---- 带自动路由的节点：读档不能因为"当前好感度"被重新路由带走 ----
       hub 有 35 条 => 路由。存档时人在 hub（好感 50），读档时运行时的好感度是 0；
       如果读档走的是 game_enter_index，就会顺着 aff>=0 掉到保底事件 f1_seat，
       存档位置直接丢失。 */
    {
        static Game h, r;
        script_load(&h, "data/main.gal");
        int hub = script_find(&h, "hub");
        check("hub 节点存在", hub >= 0);

        if (hub >= 0) {
            h.affinity = 50;
            game_restore_index(&h, hub);
            h.line_index = 1;
            save_write(&h, 2);

            script_load(&r, "data/main.gal");     /* 好感度 0 的全新状态 */
            check("读档回到 hub 本身", save_read(&r, 2) == 1 && r.node_index == hub);
            check("读档后好感度还原成 50", r.affinity == 50);
            check("读档后行号还原成 1", r.line_index == 1);
        }
    }

    printf("\n%s\n", failures == 0 ? "全部通过" : "存在失败项");
    return failures == 0 ? 0 : 1;
}
