#ifndef SPRITE_H
#define SPRITE_H

/* 立绘（角色半身图）。
 *
 * 文件：data/sprites/<角色id>_<表情>.png，比如 shenzhiji_soft.png。
 * 剧本里写 `@char shenzhiji:soft` 切人；`@char off` 收人。
 * 只写 `@char shenzhiji` 时用 _normal 表情；找不到该表情会自动退回
 * <角色id>.png，再不行就 _normal，都没有就什么都不画（不报错）。
 *
 * 透明底 PNG，建议 800×1200（半身、脚部出画），引擎按屏幕高度缩放、
 * 底部对齐、靠右摆放。
 */

void sprite_init(void);
void sprite_shutdown(void);

/* 画立绘。spec 形如 "shenzhiji:soft" / "shenzhiji" / "off" / ""。
   返回 1 = 画了，0 = 没有可画的东西。 */
int  sprite_draw(const char *spec);

#endif