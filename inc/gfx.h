#ifndef GFX_H
#define GFX_H

#include <genesis.h>

/* Palettes */
#define PAL_BG      PAL0    /* girders, ladders, oil drum, white text  */
#define PAL_MARIO   PAL1    /* Mario, Pauline, hammer, red text        */
#define PAL_KONG    PAL2    /* Kong, brown barrels, yellow text        */
#define PAL_MISC    PAL3    /* blue barrel, fire, heart, items, cyan   */

extern const u16 palettes[64];

/* Background tile indices (filled by GFX_init) */
extern u16 tGirder;         /* aligned girder tile                         */
extern u16 tGirderTop;      /* +o (1..7): upper part of girder offset by o  */
extern u16 tGirderBot;      /* +o (1..7): lower part of girder offset by o  */
extern u16 tLadder;
extern u16 tDrum;           /* 2x2, column-major                           */
extern u16 tPile;           /* 2x2 upright barrel, column-major            */
extern u16 tLife;
extern u16 tBlock;          /* title logo block                            */

/* Sprite tile indices */
extern u16 sMario[6];       /* 16x16: stand, walk1, walk2, climb, dead, jump */
extern u16 sHammer[2];      /* 16x16: up, side                             */
extern u16 sBarrel[4];      /* 16x16 rolling frames                        */
extern u16 sBarrelSide[2];  /* 16x16 on ladder                             */
extern u16 sKong[2];        /* 32x32: front, arms up                       */
extern u16 sPauline[2];     /* 16x24                                       */
extern u16 sFire[2];        /* 16x16 fireball                              */
extern u16 sFlame[2];       /* 16x16 oil drum flame                        */
extern u16 sHeart;          /* 16x16                                       */
extern u16 sRivet;          /* 8x16                                        */
extern u16 sItem[3];        /* 16x16: purse, hat, umbrella                 */
extern u16 sSmash[2];       /* 16x16                                       */
extern u16 sPoints[4];      /* 16x8: 100, 300, 500, 800                    */

enum { MF_STAND, MF_WALK1, MF_WALK2, MF_CLIMB, MF_DEAD, MF_JUMP };
enum { PTS_100, PTS_300, PTS_500, PTS_800 };

void GFX_init(void);

#endif
