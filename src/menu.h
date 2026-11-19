#ifndef MENU_H
#define MENU_H

#include "game.h"

/* 标题界面动作码 */
#define TITLE_NONE       0
#define TITLE_NEW        1
#define TITLE_CONTINUE   2
#define TITLE_QUIT       3

/* 槽位界面动作码 */
#define SLOT_NONE       -1
#define SLOT_CLOSE       3

int  menu_update_title(void);
void menu_draw_title(void);

int  menu_update_slots(void);
void menu_draw_slots(Game *g, int saving);

int  menu_update_log(Game *g);
void menu_draw_log(Game *g);

#endif
