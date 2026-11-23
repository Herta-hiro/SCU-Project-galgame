/* 剧本解析的离线单元测试。不依赖 raylib，可直接编译运行。
   编译: gcc tools/test_script.c src/script.c src/save.c src/util.c -Isrc -o test_script

   这里断言的是「剧本 DSL 的契约」——前四列对所有分支同义，
   场景只有 AI 分支会读。以前没有这一层，18 个选项的好感度增减
   静默失效了几个月，而校验器一直报 0 错误。
*/

#include "game.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(const char *name, int cond)
{
    printf("%s  %s\n", cond ? "[ok]  " : "[FAIL]", name);
    if (!cond) failures++;
}

/* 解析一行，失败时把原因打出来 */
static Node parse1(const char *s)
{
    static Node n;
    char err[256];

    memset(&n, 0, sizeof(n));
    ScriptLineKind k = script_parse_line(s, &n, err, (int)sizeof(err));
    if (k == SL_ERROR)
        fprintf(stderr, "      解析失败: %s\n      原因: %s\n", s, err);
    return n;
}

static int parse_kind(const char *s)
{
    Node n;
    char err[256];
    memset(&n, 0, sizeof(n));
    return (int)script_parse_line(s, &n, err, (int)sizeof(err));
}

int main(void)
{
    /* ---- 选项：前四列对所有分支同义 ---- */
    check("4 列 +2 是好感",        parse1("-> 好|b|说|+2").choices[0].affinity == 2);
    check("4 列 -3 是好感",        parse1("-> 好|b|说|-3").choices[0].affinity == -3);
    check("4 列 0 是好感",         parse1("-> 好|b|说|0").choices[0].affinity == 0);
    check("3 列缺省 +1",           parse1("-> 好|b|说").choices[0].affinity == 1);
    check("2 列缺省 +1",           parse1("-> 好|b").choices[0].affinity == 1);
    check("2 列 prompt 为空",      parse1("-> 好|b").choices[0].prompt[0] == '\0');
    check("第三列进 prompt",       strcmp(parse1("-> 好|b|我说的话").choices[0].prompt,
                                          "我说的话") == 0);
    check("目标两侧空格被吃掉",     strcmp(parse1("-> 好| b ").choices[0].target, "b") == 0);

    /* ---- 选项：场景只在 AI 分支上出现，且在第五列 ---- */
    check("AI 分支 5 列：好感在第四列",
          parse1("-> 好|@x:y|说|6").choices[0].affinity == 6);
    check("AI 分支 5 列：场景在第五列",
          strcmp(parse1("-> 好|@x:y|说|6|成都秋雨天").choices[0].scene, "成都秋雨天") == 0);
    check("AI 分支 6 列：条件是第六列",
          parse1("-> 好|@x:y|说|6|成都|aff>=46").choices[0].cond_min == 46);
    check("AI 分支 6 列：场景仍对",
          strcmp(parse1("-> 好|@x:y|说|6|成都|aff>=46").choices[0].scene, "成都") == 0);

    /* ---- 选项：条件 ---- */
    check("条件 aff<=20",          parse1("-> 好|b|说|2||aff<=20").choices[0].cond_max == 20);
    check("条件只写 >=46",         parse1("-> 好|b|说|2||>=46").choices[0].cond_min == 46);
    check("条件 aff==0",           parse1("-> 好|b|说|2||aff==0").choices[0].cond_max == 0);
    check("无条件时全显示",         parse1("-> 好|b|说|2").choices[0].cond_min == 0 &&
                                   parse1("-> 好|b|说|2").choices[0].cond_max == 100);

    /* ---- 选项：该报错的都要报错，不能静默 ---- */
    check("缺目标报错",            parse_kind("-> 好") == SL_ERROR);
    check("目标为空报错",          parse_kind("-> 好|") == SL_ERROR);
    check("第四列不是数字报错",     parse_kind("-> 好|b|说|很多") == SL_ERROR);
    check("超过 6 列报错",         parse_kind("-> a|b|c|1|2|3|4") == SL_ERROR);
    check("普通分支写场景报错",     parse_kind("-> 好|b|说|1|成都") == SL_ERROR);
    check("6 列 AI 分支合法",       parse_kind("-> a|@x:y|c|1|成都|aff>=20") == SL_CHOICE);
    {
        Node n;
        char err[256];
        memset(&n, 0, sizeof(n));
        for (int i = 0; i < 4; i++) script_parse_line("-> a|b", &n, err, (int)sizeof(err));
        check("第 5 个选项报错", script_parse_line("-> a|b", &n, err, (int)sizeof(err)) == SL_ERROR);
    }

    /* ---- 台词 / 旁白 ---- */
    check("说话人|台词",           strcmp(parse1("沈知霁|椅子没人。").lines[0].speaker, "沈知霁") == 0 &&
                                   strcmp(parse1("沈知霁|椅子没人。").lines[0].text, "椅子没人。") == 0);
    check("|开头是旁白",           parse1("|她把书收了收。").lines[0].speaker[0] == '\0');
    check("没有竖线当旁白",         strcmp(parse1("成都的雨下了三天。").lines[0].text,
                                          "成都的雨下了三天。") == 0);

    /* ---- @ 指令 ---- */
    check("@bg",                   strcmp(parse1("@bg bg_lib_window").bg, "bg_lib_window") == 0);
    check("@char",                 strcmp(parse1("@char shenzhiji:soft").spr, "shenzhiji:soft") == 0);
    check("@char off",             strcmp(parse1("@char off").spr, "off") == 0);
    check("@loc",                  strcmp(parse1("@loc 省图三楼").loc, "省图三楼") == 0);
    check("@toast",                strcmp(parse1("@toast （钟走过一格。）").toast,
                                          "（钟走过一格。）") == 0);
    check("@once A1 -> 1",         parse1("@once A1").once_id == 1);
    check("@once B1 -> 17",        parse1("@once B1").once_id == 17);
    check("@once Z15 -> 415",      parse1("@once Z15").once_id == 415);
    check("@once A16 报错",        parse_kind("@once A16") == SL_ERROR);
    check("未知 @ 指令报错",        parse_kind("@bgg bg_x") == SL_ERROR);

    {
        Node n = parse1("@offline 第一行");
        char err[256];
        script_parse_line("@offline 第二行", &n, err, (int)sizeof(err));
        check("@offline 多行拼接",    strcmp(n.offline, "第一行\n第二行") == 0);
    }

    /* ---- 路由 ---- */
    check("=> aff>=46",            parse1("=> aff>=46 hub").routes[0].cond_min == 46);
    check("=> 路由目标",            strcmp(parse1("=> aff>=46 hub").routes[0].target, "hub") == 0);
    check("=> aff<=20",            parse1("=> aff<=20 x").routes[0].cond_max == 20);
    check("=> 没有比较符号报错",     parse_kind("=> 46 hub") == SL_ERROR);
    check("=> 缺目标报错",          parse_kind("=> aff>=46") == SL_ERROR);

    /* ---- 其它 ---- */
    check("空行跳过",              parse_kind("") == SL_SKIP);
    check("注释跳过",              parse_kind("# 注释") == SL_SKIP);
    check(":: 节点",               strcmp(parse1(":: start").id, "start") == 0);

    /* ---- 模型回复：状态栏里的判定要取出来，台词不能被误吞 ---- */
    {
        static Node n;
        int v = 0;

        memset(&n, 0, sizeof(n));
        check("3 行台词 + 状态栏",
              parse_reply("沈知霁：嗯。\n"
                          "旁白：她把书往自己那边收了收，笔尖没停。\n"
                          "沈知霁：椅子没人。\n"
                          "【好感：55/100｜阶段：2｜记忆：玩家问了索引｜判定：+2，资料相关】\n",
                          &n, &v) == 1 && n.line_count == 3 && v == 2);

        memset(&n, 0, sizeof(n));
        check("没有状态栏时返回 0（离线兜底台词就是这种）",
              parse_reply("沈知霁：嗯。\n", &n, &v) == 0 && n.line_count == 1);

        memset(&n, 0, sizeof(n));
        check("判定 -3", parse_reply("【好感：10｜阶段：0｜判定：-3，越界】\n", &n, &v) == 1 && v == -3);

        memset(&n, 0, sizeof(n));
        check("判定全角 ＋2", parse_reply("【好感：10｜阶段：0｜判定：＋2】\n", &n, &v) == 1 && v == 2);

        memset(&n, 0, sizeof(n));
        check("判定全角 －3", parse_reply("【好感：10｜阶段：0｜判定：－3】\n", &n, &v) == 1 && v == -3);

        memset(&n, 0, sizeof(n));
        check("判定带空格 + 2", parse_reply("【好感：10｜阶段：0｜判定： + 2】\n", &n, &v) == 1 && v == 2);

        memset(&n, 0, sizeof(n));
        check("判定用半角冒号", parse_reply("【好感：10｜阶段：0｜判定:+4】\n", &n, &v) == 1 && v == 4);

        memset(&n, 0, sizeof(n));
        check("判定写得不像数字时不冒充 0",
              parse_reply("【好感：10｜阶段：0｜判定：很好】\n", &n, &v) == 0 && v == 0);

        /* 以前任何含「判定：」的行都会被整行吞掉，包括正常旁白 */
        memset(&n, 0, sizeof(n));
        parse_reply("旁白：她判定：他撒谎了。\n", &n, &v);
        check("台词里的「判定：」不被吞", n.line_count == 1 && v == 0);

        memset(&n, 0, sizeof(n));
        check("空回复不出行", parse_reply("", &n, &v) == 0 && n.line_count == 0);
    }

    /* ---- 真实数据：整份剧本必须 0 错误 ---- */
    {
        static Game g;
        int ok = script_load(&g, "data/main.gal");
        printf("\n---- 集成 ----\n");
        printf("节点数 = %d，解析错误 = %d\n", g.node_count, script_error_count());

        check("main.gal 能加载",        ok == 1);
        check("main.gal 解析 0 错误",   script_error_count() == 0);
        check("节点数 72",             g.node_count == 72);

        int start = script_find(&g, "start");
        check("start 节点存在",        start >= 0);
        if (start >= 0) {
            const Node *n = &g.nodes[start];
            check("start 第一个选项 +1",  n->choices[0].affinity == 1);
            check("start 第二个选项是 0，不是旧写法的 +1",
                  n->choices[1].affinity == 0 && n->choices[1].scene[0] == '\0');
        }

        int a1 = script_find(&g, "a1_third_day");
        if (a1 >= 0) {
            check("a1_third_day 第三个选项 -4", g.nodes[a1].choices[2].affinity == -4);
        } else {
            check("a1_third_day 节点存在", 0);
        }

        int f8 = script_find(&g, "f8_bump");
        if (f8 >= 0) {
            check("f8_bump 第二个选项 -2", g.nodes[f8].choices[1].affinity == -2);
        } else {
            check("f8_bump 节点存在", 0);
        }

        /* 数据级不变量：普通分支没有场景，说明没人还在写旧的 5 列格式。
           这一条是那次事故的直接回归测试。 */
        int plain_with_scene = 0;
        for (int i = 0; i < g.node_count; i++)
            for (int c = 0; c < g.nodes[i].choice_count; c++) {
                const Choice *ch = &g.nodes[i].choices[c];
                if (!choice_is_ai(ch) && ch->scene[0]) plain_with_scene++;
            }
        check("没有普通分支带场景（旧的 5 列写法已清零）", plain_with_scene == 0);
        if (plain_with_scene)
            printf("      有 %d 个普通分支的第五列被当成场景\n", plain_with_scene);
    }

    printf("\n%s\n", failures == 0 ? "全部通过" : "存在失败项");
    return failures == 0 ? 0 : 1;
}
