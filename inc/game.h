#ifndef GAME_H
#define GAME_H

#include <genesis.h>

/* 16.16 fixed point: the integer part is the (aligned) high word, which
 * keeps the compiler from generating odd address word reads on the 68000 */
#define FP          16
#define FIX(n)      ((s32)(n) << FP)
#define INT(v)      ((s16)((v) >> FP))
/* speeds are written in 1/256 pixel units */
#define SPD(n)      ((s32)(n) << (FP - 8))

/* play field: 28 columns of 8 pixels, centred on the 320 pixel screen */
#define PF_X        48
#define PF_W        224
#define NCOL        28
#define PF_COL      (PF_X / 8)

#define BTN_JUMP    (BUTTON_A | BUTTON_B | BUTTON_C | BUTTON_X | BUTTON_Y | BUTTON_Z)

#define PRESSED(m)  ((joy & ~joyPrev) & (m))
#define HELD(m)     (joy & (m))

enum { STAGE_BARRELS, STAGE_RIVETS };
enum { RES_CLEAR, RES_DEAD };

extern u16 joy, joyPrev;
extern u16 frameCount;

extern u32 score, hiscore;
extern u16 lives, level;
extern u16 bonus;

/* main.c */
void frameEnd(void);
void text(const char* s, u16 x, u16 y, u16 pal);
void addScore(u32 pts);
void HUD_drawAll(u16 stageType);
void HUD_drawScore(void);
void HUD_drawLives(void);
void HUD_drawBonus(void);
void HUD_message(const char* s, u16 pal);

/* sprites */
void spr_begin(void);
void spr_put(s16 x, s16 y, u8 size, u16 attr);
void spr_end(void);

/* game.c */
u16 GAME_playStage(u16 stageType, bool fresh);

#endif
