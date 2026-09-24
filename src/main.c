/*
 * DONKEY KONG - Mega Drive / Genesis fan remake
 *
 * Game flow: title screen, "how high can you get" intermission, stages,
 * game over, initials entry. The top 5 scores are kept in battery SRAM.
 */
#include <genesis.h>
#include "game.h"
#include "gfx.h"
#include "sound.h"

u16 joy, joyPrev;
u16 frameCount;
u32 score, hiscore;
u16 lives, level;
u16 bonus;

static bool extraGiven;

#ifndef START_LIVES
#define START_LIVES     3
#endif
#define EXTRA_LIFE_AT   10000

/* ------------------------------------------------------------------ */
/* Frame / input / sprites                                              */
/* ------------------------------------------------------------------ */

void frameEnd(void)
{
    SND_update();
    SYS_doVBlankProcess();
    joyPrev = joy;
    joy = JOY_readJoypad(JOY_1);
    frameCount++;
}

static u16 sprN;

void spr_begin(void)
{
    sprN = 0;
}

void spr_put(s16 x, s16 y, u8 size, u16 attr)
{
    if (sprN >= 80) return;
    if (x <= -32 || x >= 320 || y <= -32 || y >= 224) return;
    VDP_setSpriteFull(sprN, x, y, size, attr, sprN + 1);
    sprN++;
}

void spr_end(void)
{
    if (sprN == 0)
    {
        VDP_setSpriteFull(0, 0, -32, SPRITE_SIZE(1, 1), 0, 0);
        sprN = 1;
    }
    else VDP_setSpriteLink(sprN - 1, 0);
    VDP_updateSprites(sprN, DMA_QUEUE);
}

static void hideSprites(void)
{
    spr_begin();
    spr_end();
}

void text(const char* s, u16 x, u16 y, u16 pal)
{
    VDP_setTextPalette(pal);
    VDP_drawText(s, x, y);
}

static void clearScreen(void)
{
    hideSprites();
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);
}

/* ------------------------------------------------------------------ */
/* High scores (battery backed SRAM)                                    */
/* ------------------------------------------------------------------ */

typedef struct
{
    u32 score;
    char name[4];
} HiEntry;

#define NHI 5
static HiEntry hiTable[NHI];

static const HiEntry defaultTable[NHI] =
{
    { 7650, "MIO" },
    { 6100, "DKG" },
    { 5950, "PAU" },
    { 5050, "JMP" },
    { 4300, "SGA" },
};

#define SRAM_SIZE (4 + NHI * 7 + 1)

static void hiDefaults(void)
{
    memcpy(hiTable, defaultTable, sizeof(hiTable));
}

static void hiLoad(void)
{
    u8 buf[SRAM_SIZE];
    u8 sum = 0;

    SRAM_enable();
    for (u16 i = 0; i < SRAM_SIZE; i++) buf[i] = SRAM_readByte(i);
    SRAM_disable();

    for (u16 i = 0; i < SRAM_SIZE - 1; i++) sum += buf[i];
    if (buf[0] != 'D' || buf[1] != 'K' || buf[2] != 'M' || buf[3] != 'D' || sum != buf[SRAM_SIZE - 1])
    {
        hiDefaults();
        return;
    }
    for (u16 e = 0; e < NHI; e++)
    {
        const u8* p = &buf[4 + e * 7];
        u32 s = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];

        if (s > 999999) s = 0;
        hiTable[e].score = s;
        for (u16 k = 0; k < 3; k++)
        {
            char c = p[4 + k];
            hiTable[e].name[k] = (c >= ' ' && c <= 'Z') ? c : ' ';
        }
        hiTable[e].name[3] = 0;
    }
}

static void hiSave(void)
{
    u8 buf[SRAM_SIZE];
    u8 sum = 0;

    buf[0] = 'D'; buf[1] = 'K'; buf[2] = 'M'; buf[3] = 'D';
    for (u16 e = 0; e < NHI; e++)
    {
        u8* p = &buf[4 + e * 7];
        u32 s = hiTable[e].score;

        p[0] = s >> 24; p[1] = s >> 16; p[2] = s >> 8; p[3] = s;
        for (u16 k = 0; k < 3; k++) p[4 + k] = hiTable[e].name[k];
    }
    for (u16 i = 0; i < SRAM_SIZE - 1; i++) sum += buf[i];
    buf[SRAM_SIZE - 1] = sum;

    SRAM_enable();
    for (u16 i = 0; i < SRAM_SIZE; i++) SRAM_writeByte(i, buf[i]);
    SRAM_disable();
}

/* ------------------------------------------------------------------ */
/* HUD (left and right of the play field)                              */
/* ------------------------------------------------------------------ */

static void drawNumber(u32 v, u16 digits, u16 x, u16 y, u16 pal)
{
    char s[12];

    uintToStr(v, s, digits);
    text(s, x, y, pal);
}

void HUD_drawScore(void)
{
    drawNumber(score, 6, 0, 2, PAL_BG);
    drawNumber(hiscore, 6, 0, 5, PAL_BG);
}

void HUD_drawLives(void)
{
    char s[4];
    u16 l = (lives > 99) ? 99 : lives;

    VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL_MARIO, 0, 0, 0, tLife), 1, 8);
    s[0] = 'x';
    s[1] = '0' + l / 10;
    s[2] = '0' + l % 10;
    s[3] = 0;
    if (l < 10)
    {
        s[1] = s[2];
        s[2] = ' ';
    }
    text(s, 2, 8, PAL_BG);
}

void HUD_drawBonus(void)
{
    drawNumber(bonus, 4, 35, 2, (bonus <= 1000) ? PAL_MARIO : PAL_BG);
}

void HUD_message(const char* s, u16 pal)
{
    text(s, 34, 13, pal);
}

void HUD_drawAll(u16 stageType)
{
    char s[8];

    text("1UP", 1, 1, PAL_MARIO);
    text("HIGH", 1, 4, PAL_MARIO);
    HUD_drawScore();
    HUD_drawLives();

    text("LEVEL", 0, 11, PAL_MISC);
    uintToStr(level, s, 2);
    text(s, 2, 12, PAL_BG);

    text("BONUS", 34, 1, PAL_MISC);
    HUD_drawBonus();
    text(stageType == STAGE_BARRELS ? " 25M" : "100M", 35, 5, PAL_KONG);
}

void addScore(u32 pts)
{
    score += pts;
    if (score > 999999) score = 999999;
    if (!extraGiven && score >= EXTRA_LIFE_AT)
    {
        extraGiven = TRUE;
        lives++;
        HUD_drawLives();
        SND_sfx(SFX_LIFE);
    }
    if (score > hiscore) hiscore = score;
    HUD_drawScore();
}

/* ------------------------------------------------------------------ */
/* Title                                                                */
/* ------------------------------------------------------------------ */

static const char* const logoGlyph[7][5] =
{
    { "1111.", "1...1", "1...1", "1...1", "1111." },   /* D */
    { ".111.", "1...1", "1...1", "1...1", ".111." },   /* O */
    { "1...1", "11..1", "1.1.1", "1..11", "1...1" },   /* N */
    { "1...1", "1..1.", "111..", "1..1.", "1...1" },   /* K */
    { "11111", "1....", "1111.", "1....", "11111" },   /* E */
    { "1...1", ".1.1.", "..1..", "..1..", "..1.." },   /* Y */
    { ".1111", "1....", "1..11", "1...1", ".1111" },   /* G */
};

static void drawLogoWord(const char* word, u16 x, u16 y)
{
    static const char letters[] = "DONKEYG";

    for (; *word; word++, x += 6)
    {
        u16 g = 0;
        while (letters[g] && letters[g] != *word) g++;
        if (!letters[g]) continue;

        for (u16 r = 0; r < 5; r++)
            for (u16 c = 0; c < 5; c++)
                if (logoGlyph[g][r][c] == '1')
                    VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL_BG, 0, 0, 0, tBlock), x + c, y + r);
    }
}

static void drawHiTable(u16 y)
{
    static const char* const rank[NHI] = { "1ST", "2ND", "3RD", "4TH", "5TH" };

    text("RANK  SCORE  NAME", 11, y, PAL_MISC);
    for (u16 i = 0; i < NHI; i++)
    {
        text(rank[i], 11, y + 1 + i, PAL_MARIO);
        drawNumber(hiTable[i].score, 6, 16, y + 1 + i, PAL_BG);
        text(hiTable[i].name, 24, y + 1 + i, PAL_KONG);
    }
}

static void titleScreen(void)
{
    clearScreen();
    drawLogoWord("DONKEY", 2, 1);
    drawLogoWord("KONG", 8, 7);
    drawHiTable(20);
    text("FAN GAME - SEGA MEGA DRIVE", 7, 27, PAL_MISC);

    SND_music(MUS_TITLE);
    for (u16 t = 0; ; t++)
    {
        text(((t >> 5) & 1) ? "           " : "PRESS START", 14, 18, PAL_BG);

        spr_begin();
        spr_put(144, 102, SPRITE_SIZE(4, 4), TILE_ATTR_FULL(PAL_KONG, 0, 0, 0, sKong[(t >> 4) & 1]));
        spr_put(112, 118, SPRITE_SIZE(2, 2), TILE_ATTR_FULL(PAL_KONG, 0, 0, 0, sBarrel[(t >> 3) & 3]));
        spr_put(192, 118, SPRITE_SIZE(2, 2), TILE_ATTR_FULL(PAL_MISC, 0, 0, 0, sBarrel[3 - ((t >> 3) & 3)]));
        spr_end();

        frameEnd();

        if (t > 20 && PRESSED(BUTTON_START | BTN_JUMP)) break;
    }
    SND_stopAll();
    SND_sfx(SFX_SELECT);
    setRandomSeed(frameCount ^ 0x5A3C);
}

/* ------------------------------------------------------------------ */
/* "How high can you get?"                                              */
/* ------------------------------------------------------------------ */

static void intermission(u16 stageType)
{
    u16 n = (stageType == STAGE_BARRELS) ? 1 : 4;

    clearScreen();
    text("HOW HIGH CAN YOU GET ?", 9, 3, PAL_BG);
    for (u16 i = 0; i < n; i++)
    {
        char s[8];
        s16 y = 176 - i * 36;

        uintToStr((i + 1) * 25, s, 1);
        strcat(s, " m");
        text(s, 24, (y + 14) / 8, PAL_BG);
    }

    SND_music(MUS_START);
    for (u16 t = 0; t < 170; t++)
    {
        spr_begin();
        for (u16 i = 0; i < n; i++)
            spr_put(136, 176 - i * 36 - 16, SPRITE_SIZE(4, 4), TILE_ATTR_FULL(PAL_KONG, 0, 0, 0, sKong[(i == n - 1) ? ((t >> 4) & 1) : 0]));
        spr_end();
        frameEnd();
    }
    SND_stopAll();
    clearScreen();
}

/* ------------------------------------------------------------------ */
/* Game over and initials entry                                         */
/* ------------------------------------------------------------------ */

static void gameOverScreen(void)
{
    hideSprites();
    VDP_clearTileMapRect(BG_A, 12, 11, 16, 5);
    VDP_clearTileMapRect(BG_B, 12, 11, 16, 5);
    text("GAME  OVER", 15, 13, PAL_MARIO);

    SND_stopAll();
    SND_music(MUS_GAMEOVER);
    for (u16 t = 0; t < 300; t++)
    {
        frameEnd();
        if (t > 60 && SND_musicDone()) break;
    }
    SND_stopAll();
}

static const char nameChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-! ";

static u16 charIndex(char c)
{
    for (u16 i = 0; nameChars[i]; i++)
        if (nameChars[i] == c) return i;
    return 0;
}

static void enterName(u16 rank)
{
    char name[4] = "AAA";
    u16 pos = 0;
    u16 hold = 0;
    u16 n = strlen(nameChars);

    /* shift lower scores down */
    for (s16 i = NHI - 1; i > (s16)rank; i--) hiTable[i] = hiTable[i - 1];
    hiTable[rank].score = score;
    strcpy(hiTable[rank].name, "   ");

    clearScreen();
    text("CONGRATULATIONS !", 11, 3, PAL_KONG);
    text("YOU GOT A HIGH SCORE", 10, 5, PAL_BG);
    text("ENTER YOUR INITIALS", 10, 8, PAL_MISC);
    text("UP/DOWN : CHANGE LETTER", 8, 17, PAL_BG);
    text("A/B/C   : NEXT", 8, 18, PAL_BG);
    text("LEFT    : BACK", 8, 19, PAL_BG);
    text("START   : DONE", 8, 20, PAL_BG);
    drawNumber(score, 6, 17, 11, PAL_BG);

    SND_music(MUS_HISCORE);
    for (u16 t = 0; t < 60 * 45; t++)
    {
        u16 ci = charIndex(name[pos]);
        bool up = PRESSED(BUTTON_UP), down = PRESSED(BUTTON_DOWN);

        /* auto repeat */
        if (HELD(BUTTON_UP | BUTTON_DOWN))
        {
            if (++hold > 20 && (hold & 3) == 0)
            {
                up = HELD(BUTTON_UP);
                down = HELD(BUTTON_DOWN);
            }
        }
        else hold = 0;

        if (up) name[pos] = nameChars[(ci + 1) % n];
        if (down) name[pos] = nameChars[(ci + n - 1) % n];
        if (up || down) SND_sfx(SFX_BONUS);

        if (PRESSED(BTN_JUMP | BUTTON_RIGHT))
        {
            SND_sfx(SFX_SELECT);
            if (pos < 2) pos++;
            else if (PRESSED(BTN_JUMP)) break;
        }
        if (PRESSED(BUTTON_LEFT) && pos > 0) pos--;
        if (PRESSED(BUTTON_START)) break;

        for (u16 i = 0; i < 3; i++)
        {
            char s[2] = { name[i], 0 };
            if (i == pos && ((t >> 3) & 1)) s[0] = '_';
            else if (s[0] == ' ') s[0] = '.';
            text(s, 18 + i * 2, 13, (i == pos) ? PAL_KONG : PAL_BG);
        }
        frameEnd();
    }
    memcpy(hiTable[rank].name, name, 3);
    hiTable[rank].name[3] = 0;
    hiSave();
    SND_stopAll();

    clearScreen();
    drawHiTable(10);
    for (u16 t = 0; t < 180; t++) frameEnd();
}

/* ------------------------------------------------------------------ */
/* Game                                                                 */
/* ------------------------------------------------------------------ */

static const u16 stageOrder[] = { STAGE_BARRELS, STAGE_RIVETS };
#define NSTAGES (sizeof(stageOrder) / sizeof(stageOrder[0]))

#ifndef START_STAGE
#define START_STAGE 0
#endif

static void playGame(void)
{
    u16 idx = START_STAGE;
    bool fresh = TRUE;

    score = 0;
    lives = START_LIVES;
    level = 1;
    extraGiven = FALSE;

    while (TRUE)
    {
        u16 st = stageOrder[idx];

        intermission(st);
        if (GAME_playStage(st, fresh) == RES_CLEAR)
        {
            fresh = TRUE;
            if (++idx >= NSTAGES)
            {
                idx = 0;
                if (level < 99) level++;
            }
        }
        else
        {
            fresh = FALSE;
            if (--lives == 0) break;
        }
    }

    HUD_drawLives();
    gameOverScreen();

    for (u16 r = 0; r < NHI; r++)
    {
        if (score > hiTable[r].score)
        {
            enterName(r);
            break;
        }
    }
    hiscore = hiTable[0].score;
}

int main(bool hardReset)
{
    VDP_setScreenWidth320();
    VDP_setBackgroundColor(0);
    VDP_setTextPlane(BG_A);
    PAL_setColors(0, palettes, 64, CPU);

    GFX_init();
    SND_init();
    hiLoad();
    hiscore = hiTable[0].score;

    JOY_init();
    joy = joyPrev = 0;

    while (TRUE)
    {
        titleScreen();
        playGame();
    }
    return 0;
}
