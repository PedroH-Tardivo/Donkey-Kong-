/*
 * Gameplay: the 25m barrel stage and the 100m rivet stage.
 *
 * Coordinates are screen pixels. Every actor position is the centre of its
 * feet (x = centre, y = bottom), stored in 24.8 fixed point.
 */
#include <genesis.h>
#include "game.h"
#include "gfx.h"
#include "sound.h"

#define MAXF        8
#define MAXL        16
#define MAXB        8
#define MAXFIRE     5
#define NRIVET      8
#define MAXPOP      4
#define MAXSMASH    3
#define NONE        (-1)

/* movement, in 1/256 pixel per frame */
#define WALK_V      SPD(256)
#define JUMP_V      SPD(512)
#define GRAVITY     SPD(32)
#define MAX_FALL    SPD(1024)
#define CLIMB_V     SPD(192)
#define FALL_DEATH  16          /* pixels: landing lower than this kills */
#define HAMMER_TIME 600
#define BARREL_GRAV SPD(40)
#define BARREL_LAD  SPD(192)
#define FIRE_CLIMB  SPD(128)

#define DRUM_X      (PF_X + 16)     /* barrels disappear in the oil drum here */
#define MARIO_MIN_X (PF_X + 5)
#define MARIO_MAX_X (PF_X + PF_W - 6)

#define ATTR(pal, fv, fh, t)   TILE_ATTR_FULL(pal, 0, fv, fh, t)
#define SZ16        SPRITE_SIZE(2, 2)
#define SZ32        SPRITE_SIZE(4, 4)

/* ------------------------------------------------------------------ */
/* Level data                                                           */
/* ------------------------------------------------------------------ */

typedef struct
{
    s16 x;          /* centre */
    u8 fb, ft;      /* bottom / top floor */
    u8 broken;      /* Mario and fireballs can't use it, barrels can */
    u8 noFire;      /* fireballs never use it */
} Ladder;

static u16 stage;
static s16 surf[MAXF][NCOL];    /* girder top y per column, NONE = empty */
static s8 floorDir[MAXF];       /* downhill direction of each floor */
static u16 nFloors;
static s16 goalFloor;
static Ladder lad[MAXL];
static u16 nLad;

static s16 floorAt(u16 f, s16 x)
{
    if (x < PF_X || x >= PF_X + PF_W) return NONE;
    return surf[f][(x - PF_X) >> 3];
}

static s16 colX(u16 c)
{
    return PF_X + c * 8 + 4;
}

static s16 ladTop(const Ladder* l) { return floorAt(l->ft, l->x); }
static s16 ladBot(const Ladder* l) { return floorAt(l->fb, l->x); }

/* first floor whose surface is crossed while moving down from oy to ny */
static s16 findLanding(s16 x, s16 oy, s16 ny, s16* outY)
{
    s16 best = NONE;
    s16 bestY = 0x7FFF;

    for (u16 f = 0; f < nFloors; f++)
    {
        s16 s = floorAt(f, x);
        if (s == NONE) continue;
        if (oy <= s + 2 && ny >= s && s < bestY)
        {
            best = f;
            bestY = s;
        }
    }
    *outY = bestY;
    return best;
}

static bool crossed(s16 ox, s16 nx, s16 x)
{
    return (ox < x && nx >= x) || (ox > x && nx <= x);
}

static u16 rnd(u16 n)
{
    return random() % n;
}

static s16 absv(s16 v)
{
    return (v < 0) ? -v : v;
}

/* boxes are given as centre x, bottom y, half width, height */
static bool overlap(s16 ax, s16 ay, s16 aw, s16 ah, s16 bx, s16 by, s16 bw, s16 bh)
{
    return absv(ax - bx) < aw + bw && ay - ah < by && by - bh < ay;
}

/* ------------------------------------------------------------------ */
/* Actors                                                               */
/* ------------------------------------------------------------------ */

enum { MS_WALK, MS_JUMP, MS_FALL, MS_CLIMB };

static struct
{
    s32 x, y, vx, vy;
    u8 state;
    u8 floor;
    s8 dir;
    u8 lad;
    u8 frame;
    u8 flipH, flipV;
    u8 animT;
    s16 fallStartY;
    u16 hammer;
    bool hidden;
} m;

enum { BS_ROLL, BS_FALL, BS_LADDER };

typedef struct
{
    u8 active;
    u8 state;
    u8 blue;
    u8 floor;
    s8 dir;
    u8 bounced;
    u8 jumped;
    u8 lad;
    s32 x, y, vx, vy;
} Barrel;

static Barrel bar[MAXB];

enum { FS_SPAWN, FS_WALK, FS_CLIMB };

typedef struct
{
    u8 active;
    u8 state;
    u8 floor;
    s8 dir;
    s8 vdir;
    u8 lad;
    u8 jumped;
    u16 t;
    s32 x, y, vy;
} Fire;

static Fire fire[MAXFIRE];

typedef struct { u8 active; s16 x, y; } Hammer;
static Hammer hammers[2];

typedef struct { u8 active; u8 type; s16 x, y; } Item;
static Item items[3];

typedef struct { u8 f, c, state; } Rivet;   /* state: 0 in place, 1 pulled (Mario on it), 2 hole */
static Rivet rivets[NRIVET];
static u16 rivetsLeft;

typedef struct { u8 active; u8 idx; u8 t; s16 x, y; } Popup;
static Popup pops[MAXPOP];

typedef struct { u8 active; u8 t; s16 x, y; } Smash;
static Smash smashes[MAXSMASH];

enum { K_IDLE, K_GRAB, K_HOLD, K_THROW, K_BEAT, K_WALK };

static struct
{
    s32 x;
    s16 y;
    u8 state;
    u16 t;
    u16 throwT;
    u8 frame;
    u8 flipV;
    s8 dir;
    u8 nextBlue;
    u8 firstThrow;
} kong;

static u8 drumLit;
static u16 fireSpawnT;
static u16 bonusT;
static u16 helpT;
static u16 stageT;
static bool heartShown;
static s16 heartX, heartY;
static bool dead;

static s32 barrelSpeed(void)
{
    s32 v = 256 + (level - 1) * 32;
    return SPD((v > 384) ? 384 : v);
}

static s32 fireSpeed(void)
{
    s32 v = 112 + level * 16;
    return SPD((v > 224) ? 224 : v);
}

static u16 maxFires(void)
{
    u16 n;

    if (stage == STAGE_BARRELS) n = 1 + level;
    else n = 2 + (level - 1) + (NRIVET - rivetsLeft) / 3;
    return (n > MAXFIRE) ? MAXFIRE : n;
}

static void popup(s16 x, s16 y, u16 idx)
{
    for (u16 i = 0; i < MAXPOP; i++)
    {
        if (!pops[i].active)
        {
            pops[i].active = TRUE;
            pops[i].idx = idx;
            pops[i].t = 60;
            pops[i].x = x;
            pops[i].y = y;
            return;
        }
    }
}

static void award(u32 pts, s16 x, s16 y)
{
    u16 idx = PTS_100;

    if (pts == 300) idx = PTS_300;
    else if (pts == 500) idx = PTS_500;
    else if (pts == 800) idx = PTS_800;
    addScore(pts);
    popup(x, y, idx);
}

/* ------------------------------------------------------------------ */
/* Stage construction                                                   */
/* ------------------------------------------------------------------ */

static void addLadder(u16 c, u8 fb, u8 ft, u8 broken, u8 noFire)
{
    Ladder* l = &lad[nLad++];

    l->x = colX(c);
    l->fb = fb;
    l->ft = ft;
    l->broken = broken;
    l->noFire = noFire;
}

static void computeFloorDirs(void)
{
    for (u16 f = 0; f < nFloors; f++)
    {
        s16 first = NONE, last = NONE;

        for (u16 c = 0; c < NCOL; c++)
        {
            if (surf[f][c] == NONE) continue;
            if (first == NONE) first = c;
            last = c;
        }
        floorDir[f] = 1;
        if (first != NONE && surf[f][last] < surf[f][first]) floorDir[f] = -1;
    }
}

static void buildBarrels(void)
{
    nFloors = 7;
    for (u16 c = 0; c < NCOL; c++)
    {
        surf[0][c] = (c < 14) ? 216 : 215 - ((c - 14) >> 2);
        if (c <= 25) surf[1][c] = 180 + (c >> 2);
        if (c >= 2) surf[2][c] = 150 + ((27 - c) >> 2);
        if (c <= 25) surf[3][c] = 120 + (c >> 2);
        if (c >= 2) surf[4][c] = 90 + ((27 - c) >> 2);
        if (c <= 25) surf[5][c] = (c < 12) ? 60 : 61 + ((c - 12) >> 2);
        if (c >= 12 && c <= 19) surf[6][c] = 32;
    }
    goalFloor = 6;

    addLadder(22, 0, 1, FALSE, FALSE);
    addLadder(10, 0, 1, TRUE, FALSE);
    addLadder(4, 1, 2, FALSE, FALSE);
    addLadder(13, 1, 2, FALSE, FALSE);
    addLadder(23, 2, 3, FALSE, FALSE);
    addLadder(9, 2, 3, TRUE, FALSE);
    addLadder(4, 3, 4, FALSE, FALSE);
    addLadder(16, 3, 4, TRUE, FALSE);
    addLadder(23, 4, 5, FALSE, FALSE);
    addLadder(11, 4, 5, TRUE, FALSE);
    addLadder(16, 5, 6, FALSE, TRUE);

    hammers[0].active = TRUE;
    hammers[0].x = colX(7);
    hammers[0].y = surf[1][7] - 18;
    hammers[1].active = TRUE;
    hammers[1].x = colX(18);
    hammers[1].y = surf[4][18] - 18;
}

static const u8 rivetDef[NRIVET][2] =
{
    { 1, 7 }, { 1, 20 }, { 2, 8 }, { 2, 19 }, { 3, 8 }, { 3, 19 }, { 4, 9 }, { 4, 18 }
};

static void buildRivets(bool fresh)
{
    static const s16 y[7] = { 216, 187, 158, 129, 100, 71, 31 };
    static const u8 c0[7] = { 0, 1, 2, 3, 4, 5, 10 };
    static const u8 c1[7] = { 27, 26, 25, 24, 23, 22, 17 };

    nFloors = 7;
    for (u16 f = 0; f < 7; f++)
        for (u16 c = c0[f]; c <= c1[f]; c++) surf[f][c] = y[f];
    goalFloor = NONE;

    addLadder(2, 0, 1, FALSE, FALSE);
    addLadder(13, 0, 1, FALSE, FALSE);
    addLadder(25, 0, 1, FALSE, FALSE);
    addLadder(3, 1, 2, FALSE, FALSE);
    addLadder(24, 1, 2, FALSE, FALSE);
    addLadder(4, 2, 3, FALSE, FALSE);
    addLadder(13, 2, 3, FALSE, FALSE);
    addLadder(23, 2, 3, FALSE, FALSE);
    addLadder(5, 3, 4, FALSE, FALSE);
    addLadder(22, 3, 4, FALSE, FALSE);

    rivetsLeft = 0;
    for (u16 i = 0; i < NRIVET; i++)
    {
        rivets[i].f = rivetDef[i][0];
        rivets[i].c = rivetDef[i][1];
        if (fresh || rivets[i].state == 0) rivets[i].state = 0;
        else rivets[i].state = 2;

        if (rivets[i].state == 0) rivetsLeft++;
        else surf[rivets[i].f][rivets[i].c] = NONE;
    }

    hammers[0].active = TRUE;
    hammers[0].x = colX(6);
    hammers[0].y = y[2] - 18;
    hammers[1].active = TRUE;
    hammers[1].x = colX(16);
    hammers[1].y = y[3] - 18;

    items[0] = (Item) { TRUE, 0, colX(17), y[1] };
    items[1] = (Item) { TRUE, 1, colX(21), y[2] };
    items[2] = (Item) { TRUE, 2, colX(14), y[4] };
}

static void drawGirderCell(u16 c, s16 y)
{
    u16 tx = PF_COL + c;
    u16 row = y >> 3;
    u16 o = y & 7;

    if (o == 0)
        VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL_BG, 0, 0, 0, tGirder), tx, row);
    else
    {
        VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL_BG, 0, 0, 0, tGirderTop + o), tx, row);
        VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL_BG, 0, 0, 0, tGirderBot + o), tx, row + 1);
    }
}

static void clearGirderCell(u16 c, s16 y)
{
    u16 tx = PF_COL + c;
    u16 row = y >> 3;

    VDP_setTileMapXY(BG_A, 0, tx, row);
    if (y & 7) VDP_setTileMapXY(BG_A, 0, tx, row + 1);
}

static void drawLadderTiles(s16 x, s16 yt, s16 yb, bool broken)
{
    u16 tx = x >> 3;
    s16 r0 = (yt + 7) >> 3;
    s16 r1 = (yb - 1) >> 3;
    u16 attr = TILE_ATTR_FULL(PAL_BG, 0, 0, 0, tLadder);

    if (r0 < 0) r0 = 0;
    for (s16 r = r0; r <= r1; r++)
    {
        if (broken && r > r0 + 1 && r < r1 - 1) continue;
        VDP_setTileMapXY(BG_B, attr, tx, r);
    }
}

static void drawStage(void)
{
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);

    for (u16 f = 0; f < nFloors; f++)
        for (u16 c = 0; c < NCOL; c++)
            if (surf[f][c] != NONE) drawGirderCell(c, surf[f][c]);

    /* rivet holes: the girder under a missing rivet is gone */
    if (stage == STAGE_RIVETS)
    {
        for (u16 i = 0; i < NRIVET; i++)
            if (rivets[i].state == 2) clearGirderCell(rivets[i].c, 187 - (rivets[i].f - 1) * 29);
    }

    for (u16 i = 0; i < nLad; i++)
        drawLadderTiles(lad[i].x - 4, ladTop(&lad[i]), ladBot(&lad[i]), lad[i].broken);

    if (stage == STAGE_BARRELS)
    {
        /* the two tall ladders Kong climbed */
        drawLadderTiles(colX(8) - 4, 0, surf[5][8], FALSE);
        drawLadderTiles(colX(10) - 4, 0, surf[5][10], FALSE);

        /* oil drum */
        VDP_fillTileMapRectInc(BG_A, TILE_ATTR_FULL(PAL_BG, 0, 0, 0, tDrum), PF_COL, 25, 1, 2);
        VDP_fillTileMapRectInc(BG_A, TILE_ATTR_FULL(PAL_BG, 0, 0, 0, tDrum + 2), PF_COL + 1, 25, 1, 2);
    }
}

static void resetActors(void)
{
    memset(bar, 0, sizeof(bar));
    memset(fire, 0, sizeof(fire));
    memset(pops, 0, sizeof(pops));
    memset(smashes, 0, sizeof(smashes));
    memset(hammers, 0, sizeof(hammers));
    memset(items, 0, sizeof(items));

    memset(&m, 0, sizeof(m));
    m.state = MS_WALK;
    m.floor = 0;
    m.dir = 1;
    m.frame = MF_STAND;

    memset(&kong, 0, sizeof(kong));
    drumLit = FALSE;
    heartShown = FALSE;
    dead = FALSE;
    fireSpawnT = 300;
    bonusT = 0;
    helpT = 0;
    stageT = 0;
}

static void setupStage(u16 type, bool fresh)
{
    stage = type;
    nLad = 0;
    for (u16 f = 0; f < MAXF; f++)
        for (u16 c = 0; c < NCOL; c++) surf[f][c] = NONE;

    resetActors();

    if (type == STAGE_BARRELS)
    {
        buildBarrels();
        m.x = FIX(PF_X + 36);
        kong.x = FIX(PF_X + 48);
        kong.y = 60;
        kong.throwT = 60;
        kong.firstThrow = TRUE;
    }
    else
    {
        buildRivets(fresh);
        m.x = FIX(PF_X + 16);
        kong.x = FIX(160);
        kong.y = 71;
        kong.dir = 1;
        kong.state = K_WALK;
        kong.t = 60;
    }
    m.y = FIX(floorAt(0, INT(m.x)));
    computeFloorDirs();

    bonus = 5000 + (level - 1) * 1000;
    if (bonus > 8000) bonus = 8000;

    drawStage();
    HUD_drawAll(type);
}

/* ------------------------------------------------------------------ */
/* Mario                                                                */
/* ------------------------------------------------------------------ */

static s16 findLadder(u16 floor, s16 x, bool up)
{
    for (u16 i = 0; i < nLad; i++)
    {
        const Ladder* l = &lad[i];

        if (l->broken) continue;
        if ((up ? l->fb : l->ft) != floor) continue;
        if (absv(l->x - x) <= 4) return i;
    }
    return NONE;
}

static void startFall(void)
{
    m.state = MS_FALL;
    m.vy = 0;
    m.vx = 0;
    m.fallStartY = INT(m.y);
}

static void landMario(u16 f, s16 y)
{
    m.y = FIX(y);
    m.floor = f;
    m.state = MS_WALK;
    m.vx = 0;
    m.vy = 0;
    if (y - m.fallStartY > FALL_DEATH) dead = TRUE;
    else SND_sfx(SFX_LAND);
}

static void checkJumpOver(void)
{
    s16 mx = INT(m.x), my = INT(m.y);

    for (u16 i = 0; i < MAXB; i++)
    {
        Barrel* b = &bar[i];
        if (!b->active || b->jumped) continue;
        s16 by = INT(b->y);
        if (absv(INT(b->x) - mx) < 8 && by > my && by - my < 30)
        {
            b->jumped = TRUE;
            award(100, INT(b->x), by - 20);
            SND_sfx(SFX_SCORE);
        }
    }
    for (u16 i = 0; i < MAXFIRE; i++)
    {
        Fire* f = &fire[i];
        if (!f->active || f->jumped || f->state == FS_SPAWN) continue;
        s16 fy = INT(f->y);
        if (absv(INT(f->x) - mx) < 8 && fy > my && fy - my < 30)
        {
            f->jumped = TRUE;
            award(100, INT(f->x), fy - 22);
            SND_sfx(SFX_SCORE);
        }
    }
}

static void clearJumpFlags(void)
{
    for (u16 i = 0; i < MAXB; i++) bar[i].jumped = FALSE;
    for (u16 i = 0; i < MAXFIRE; i++) fire[i].jumped = FALSE;
}

static void updateMario(void)
{
    s16 px = INT(m.x);
    s16 py = INT(m.y);

    switch (m.state)
    {
        case MS_WALK:
        {
            s8 in = HELD(BUTTON_RIGHT) ? 1 : (HELD(BUTTON_LEFT) ? -1 : 0);

            if (!m.hammer)
            {
                if (HELD(BUTTON_UP) || HELD(BUTTON_DOWN))
                {
                    bool up = HELD(BUTTON_UP) != 0;
                    s16 l = findLadder(m.floor, px, up);

                    if (l != NONE)
                    {
                        m.state = MS_CLIMB;
                        m.lad = l;
                        m.x = FIX(lad[l].x);
                        m.frame = MF_CLIMB;
                        m.animT = 0;
                        break;
                    }
                }
                if (PRESSED(BTN_JUMP))
                {
                    m.state = MS_JUMP;
                    m.vy = -JUMP_V;
                    m.vx = in * WALK_V;
                    if (in) m.dir = in;
                    m.fallStartY = py;
                    m.frame = MF_JUMP;
                    clearJumpFlags();
                    SND_sfx(SFX_JUMP);
                    break;
                }
            }

            if (in)
            {
                s16 minX = MARIO_MIN_X;

                if (stage == STAGE_BARRELS && m.floor == 0) minX = DRUM_X + 10;
                m.dir = in;
                m.x += in * WALK_V;
                if (INT(m.x) < minX) m.x = FIX(minX);
                if (INT(m.x) > MARIO_MAX_X) m.x = FIX(MARIO_MAX_X);

                s16 fy = floorAt(m.floor, INT(m.x));
                if (fy == NONE)
                {
                    startFall();
                    break;
                }
                m.y = FIX(fy);

                m.animT++;
                switch ((m.animT >> 3) & 3)
                {
                    case 0: m.frame = MF_WALK1; break;
                    case 1: m.frame = MF_STAND; break;
                    case 2: m.frame = MF_WALK2; break;
                    default: m.frame = MF_STAND; break;
                }
                if ((m.animT & 15) == 1) SND_sfx(SFX_WALK);
            }
            else
            {
                m.frame = MF_STAND;
                m.animT = 0;
            }
            m.flipH = (m.dir < 0);
            break;
        }

        case MS_JUMP:
        case MS_FALL:
        {
            m.vy += GRAVITY;
            if (m.vy > MAX_FALL) m.vy = MAX_FALL;

            m.x += m.vx;
            {
                /* the oil drum is a wall at the bottom left */
                s16 minX = (stage == STAGE_BARRELS && INT(m.y) > 184) ? DRUM_X + 10 : MARIO_MIN_X;

                if (INT(m.x) < minX)
                {
                    m.x = FIX(minX);
                    m.vx = -m.vx;
                    m.dir = -m.dir;
                }
            }
            if (INT(m.x) > MARIO_MAX_X)
            {
                m.x = FIX(MARIO_MAX_X);
                m.vx = -m.vx;
                m.dir = -m.dir;
            }
            m.flipH = (m.dir < 0);

            s16 oy = INT(m.y);
            m.y += m.vy;
            s16 ny = INT(m.y);

            if (m.state == MS_JUMP) checkJumpOver();

            if (m.vy > 0)
            {
                s16 sy;
                s16 f = findLanding(INT(m.x), oy, ny, &sy);
                if (f != NONE)
                {
                    landMario(f, sy);
                    m.frame = MF_STAND;
                    break;
                }
            }
            if (ny > 250) dead = TRUE;
            break;
        }

        case MS_CLIMB:
        {
            const Ladder* l = &lad[m.lad];
            s16 yt = ladTop(l);
            s16 yb = ladBot(l);

            if (HELD(BUTTON_UP))
            {
                m.y -= CLIMB_V;
                m.animT++;
                if (INT(m.y) <= yt)
                {
                    m.y = FIX(yt);
                    m.floor = l->ft;
                    m.state = MS_WALK;
                    m.frame = MF_STAND;
                    break;
                }
            }
            else if (HELD(BUTTON_DOWN))
            {
                m.y += CLIMB_V;
                m.animT++;
                if (INT(m.y) >= yb)
                {
                    m.y = FIX(yb);
                    m.floor = l->fb;
                    m.state = MS_WALK;
                    m.frame = MF_STAND;
                    break;
                }
            }
            m.frame = MF_CLIMB;
            m.flipH = (m.animT >> 3) & 1;
            break;
        }
    }

    /* hammer pick-up */
    for (u16 i = 0; i < 2; i++)
    {
        Hammer* h = &hammers[i];
        if (!h->active) continue;
        if (overlap(INT(m.x), INT(m.y), 6, 16, h->x, h->y, 5, 12))
        {
            h->active = FALSE;
            m.hammer = HAMMER_TIME;
            SND_music(MUS_HAMMER);
            SND_sfx(SFX_ITEM);
        }
    }

    if (m.hammer)
    {
        m.hammer--;
        if (m.hammer == 0) SND_music(stage == STAGE_BARRELS ? MUS_BARRELS : MUS_RIVETS);
    }

    /* items */
    for (u16 i = 0; i < 3; i++)
    {
        Item* it = &items[i];
        if (!it->active) continue;
        if (overlap(INT(m.x), INT(m.y), 5, 16, it->x, it->y, 6, 10))
        {
            it->active = FALSE;
            award((level == 1) ? 300 : (level == 2) ? 500 : 800, it->x, it->y - 20);
            SND_sfx(SFX_ITEM);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Rivets                                                               */
/* ------------------------------------------------------------------ */

static void updateRivets(void)
{
    s16 mx = INT(m.x), my = INT(m.y);
    s16 mc = (mx - PF_X) >> 3;

    for (u16 i = 0; i < NRIVET; i++)
    {
        Rivet* r = &rivets[i];
        s16 ry = 187 - (r->f - 1) * 29;

        /* Mario is on the rivet, or in the air right above it */
        bool near = (mc == r->c) &&
                    ((m.state == MS_WALK && m.floor == r->f) ||
                     ((m.state == MS_JUMP || m.state == MS_FALL) && my <= ry && ry - my < 32));

        if (r->state == 0)
        {
            if (near)
            {
                r->state = 1;
                rivetsLeft--;
                clearGirderCell(r->c, ry);
                award(100, colX(r->c), ry - 20);
                SND_sfx(SFX_RIVET);
            }
        }
        else if (r->state == 1)
        {
            /* the hole opens once Mario has left the spot */
            if (!near)
            {
                r->state = 2;
                surf[r->f][r->c] = NONE;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Barrels                                                              */
/* ------------------------------------------------------------------ */

static s16 freeBarrel(void)
{
    for (u16 i = 0; i < MAXB; i++)
        if (!bar[i].active) return i;
    return NONE;
}

static bool marioBelow(u16 floor)
{
    if (m.state == MS_CLIMB) return lad[m.lad].fb < floor;
    return m.floor < floor;
}

static bool barrelTakesLadder(const Ladder* l, u16 floor)
{
    u16 p;

    if (marioBelow(floor))
    {
        p = 5 + level * 10;
        if (p > 50) p = 50;
        if (absv(INT(m.x) - l->x) < 48) p += 10;
    }
    else p = 4;
    return rnd(100) < p;
}

static Fire* spawnFire(s16 x, s16 y, bool fromDrum);

static void updateBarrel(Barrel* b)
{
    switch (b->state)
    {
        case BS_ROLL:
        {
            s16 ox = INT(b->x);
            b->x += b->dir * barrelSpeed();
            s16 nx = INT(b->x);

            for (u16 i = 0; i < nLad; i++)
            {
                const Ladder* l = &lad[i];
                if (l->ft != b->floor) continue;
                if (!crossed(ox, nx, l->x)) continue;
                if (barrelTakesLadder(l, b->floor))
                {
                    b->state = BS_LADDER;
                    b->lad = i;
                    b->x = FIX(l->x);
                    return;
                }
            }

            if (stage == STAGE_BARRELS && b->floor == 0 && nx <= DRUM_X + 4)
            {
                b->active = FALSE;
                drumLit = TRUE;
                if (b->blue) spawnFire(DRUM_X - 4, 200, TRUE);
                return;
            }

            s16 fy = floorAt(b->floor, nx);
            if (fy == NONE)
            {
                b->state = BS_FALL;
                b->vx = b->dir * SPD(128);
                b->vy = 0;
                b->bounced = FALSE;
            }
            else b->y = FIX(fy);
            break;
        }

        case BS_FALL:
        {
            b->vy += BARREL_GRAV;
            if (b->vy > MAX_FALL) b->vy = MAX_FALL;
            b->x += b->vx;
            if (INT(b->x) < PF_X + 6)
            {
                b->x = FIX(PF_X + 6);
                b->vx = -b->vx;
            }
            if (INT(b->x) > PF_X + PF_W - 7)
            {
                b->x = FIX(PF_X + PF_W - 7);
                b->vx = -b->vx;
            }

            s16 oy = INT(b->y);
            b->y += b->vy;
            s16 ny = INT(b->y);

            if (b->vy > 0)
            {
                s16 sy;
                s16 f = findLanding(INT(b->x), oy, ny, &sy);
                if (f != NONE)
                {
                    b->y = FIX(sy);
                    b->floor = f;
                    if (!b->bounced)
                    {
                        b->bounced = TRUE;
                        b->vy = -SPD(256);
                    }
                    else
                    {
                        b->state = BS_ROLL;
                        b->dir = floorDir[f];
                    }
                }
            }
            if (ny > 250) b->active = FALSE;
            break;
        }

        case BS_LADDER:
        {
            const Ladder* l = &lad[b->lad];
            s16 yb = ladBot(l);

            b->y += BARREL_LAD;
            if (INT(b->y) >= yb)
            {
                b->y = FIX(yb);
                b->floor = l->fb;
                b->state = BS_ROLL;
                b->dir = floorDir[l->fb];
            }
            break;
        }
    }
}

static void throwBarrel(void)
{
    s16 i = freeBarrel();
    if (i == NONE) return;

    Barrel* b = &bar[i];
    memset(b, 0, sizeof(Barrel));
    b->active = TRUE;
    b->state = BS_ROLL;
    b->floor = 5;
    b->dir = 1;
    b->x = kong.x + FIX(22);
    b->y = FIX(floorAt(5, INT(b->x)));
    b->blue = kong.nextBlue;
    SND_sfx(SFX_THROW);
}

/* ------------------------------------------------------------------ */
/* Kong                                                                 */
/* ------------------------------------------------------------------ */

static u16 fireCount(void)
{
    u16 n = 0;
    for (u16 i = 0; i < MAXFIRE; i++)
        if (fire[i].active) n++;
    return n;
}

static void updateKongBarrels(void)
{
    switch (kong.state)
    {
        case K_IDLE:
            kong.frame = 0;
            if (kong.throwT) kong.throwT--;
            if (kong.throwT == 0)
            {
                if (freeBarrel() != NONE)
                {
                    kong.state = K_GRAB;
                    kong.t = 18;
                    kong.nextBlue = kong.firstThrow || (rnd(6) == 0 && fireCount() < maxFires());
                    kong.firstThrow = FALSE;
                }
                else kong.throwT = 20;
            }
            else if (kong.throwT > 100 && rnd(300) == 0)
            {
                kong.state = K_BEAT;
                kong.t = 64;
            }
            break;

        case K_GRAB:
            kong.frame = 0;
            if (--kong.t == 0)
            {
                kong.state = K_HOLD;
                kong.t = 16;
            }
            break;

        case K_HOLD:
            kong.frame = 1;
            if (--kong.t == 0)
            {
                kong.state = K_THROW;
                kong.t = 14;
            }
            break;

        case K_THROW:
            kong.frame = 0;
            if (--kong.t == 0)
            {
                u16 base;

                throwBarrel();
                kong.state = K_IDLE;
                base = (level * 15 < 90) ? 150 - level * 15 : 60;
                kong.throwT = base + rnd(60);
            }
            break;

        case K_BEAT:
            kong.frame = (kong.t >> 3) & 1;
            if (--kong.t == 0) kong.state = K_IDLE;
            break;
    }
}

static void updateKongRivets(void)
{
    if (kong.state == K_BEAT)
    {
        kong.frame = (kong.t >> 3) & 1;
        if (--kong.t == 0)
        {
            kong.state = K_WALK;
            kong.t = 60 + rnd(120);
        }
        return;
    }

    kong.frame = 0;
    kong.x += kong.dir * SPD(128);
    if (INT(kong.x) < 120) { kong.x = FIX(120); kong.dir = 1; }
    if (INT(kong.x) > 200) { kong.x = FIX(200); kong.dir = -1; }
    if (--kong.t == 0)
    {
        if (rnd(3) == 0)
        {
            kong.state = K_BEAT;
            kong.t = 48;
        }
        else
        {
            kong.dir = rnd(2) ? 1 : -1;
            kong.t = 40 + rnd(100);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Fireballs                                                            */
/* ------------------------------------------------------------------ */

static Fire* spawnFire(s16 x, s16 y, bool fromDrum)
{
    if (fireCount() >= maxFires()) return NULL;

    for (u16 i = 0; i < MAXFIRE; i++)
    {
        Fire* f = &fire[i];
        if (f->active) continue;

        memset(f, 0, sizeof(Fire));
        f->active = TRUE;
        f->state = FS_SPAWN;
        f->x = FIX(x);
        f->y = FIX(y);
        f->dir = 1;
        f->t = fromDrum ? 0 : 60;
        f->vy = fromDrum ? -SPD(448) : 0;
        return f;
    }
    return NULL;
}

static void spawnRivetFire(void)
{
    /* appear at the far end of a floor, away from Mario */
    for (u16 tries = 0; tries < 8; tries++)
    {
        u16 f = rnd(5);
        bool left = (INT(m.x) > 160);
        s16 c = left ? 0 : NCOL - 1;

        if (f == m.floor && m.state != MS_CLIMB) continue;
        while (c >= 0 && c < NCOL && surf[f][c] == NONE) c += left ? 1 : -1;
        if (c < 0 || c >= NCOL) continue;

        s16 x = colX(c);
        if (absv(x - INT(m.x)) < 48 && absv(surf[f][c] - INT(m.y)) < 40) continue;

        Fire* fb = spawnFire(x, surf[f][c], FALSE);
        if (fb)
        {
            fb->floor = f;
            fb->dir = left ? 1 : -1;
        }
        return;
    }
}

static bool fireBlocked(const Fire* f, s16 x)
{
    s16 ahead = x + f->dir * 6;

    if (ahead < PF_X + 4 || ahead > PF_X + PF_W - 5) return TRUE;
    if (floorAt(f->floor, ahead) == NONE) return TRUE;
    if (floorAt(f->floor, x) == NONE) return TRUE;
    if (stage == STAGE_BARRELS && f->floor == 0 && ahead < DRUM_X + 12) return TRUE;
    return FALSE;
}

static void fireWalk(Fire* f)
{
    if (f->t) f->t--;
    if (f->t == 0)
    {
        f->t = 40 + rnd(80);
        if (m.floor == f->floor && m.state != MS_CLIMB && rnd(100) < 65)
            f->dir = (INT(m.x) > INT(f->x)) ? 1 : -1;
        else
            f->dir = rnd(2) ? 1 : -1;
    }

    s16 ox = INT(f->x);

    /* the floor vanished under it (rivet hole): it drops out of play */
    if (floorAt(f->floor, ox) == NONE)
    {
        f->active = FALSE;
        return;
    }
    if (fireBlocked(f, ox))
    {
        f->dir = -f->dir;
        if (fireBlocked(f, ox)) return;
    }
    f->x += f->dir * fireSpeed();
    s16 nx = INT(f->x);

    s16 fy = floorAt(f->floor, nx);
    if (fy == NONE)
    {
        f->x = FIX(ox);
        f->dir = -f->dir;
        return;
    }
    f->y = FIX(fy);

    for (u16 i = 0; i < nLad; i++)
    {
        const Ladder* l = &lad[i];
        bool up;

        if (l->broken || l->noFire) continue;
        if (l->fb == f->floor) up = TRUE;
        else if (l->ft == f->floor) up = FALSE;
        else continue;
        if (!crossed(ox, nx, l->x)) continue;

        u16 p = 10;
        if (up && m.floor > f->floor) p = 45;
        if (!up && m.floor < f->floor) p = 45;
        if (rnd(100) < p)
        {
            f->state = FS_CLIMB;
            f->lad = i;
            f->vdir = up ? -1 : 1;
            f->x = FIX(l->x);
            return;
        }
    }
}

static void updateFire(Fire* f)
{
    switch (f->state)
    {
        case FS_SPAWN:
            if (f->t)
            {
                /* materialising at the side of the rivet stage */
                if (--f->t == 0)
                {
                    f->state = FS_WALK;
                    f->t = 30;
                }
            }
            else
            {
                /* jumping out of the oil drum */
                s16 oy = INT(f->y);
                f->vy += GRAVITY;
                f->x += SPD(96);
                f->y += f->vy;
                if (f->vy > 0)
                {
                    s16 sy;
                    s16 fl = findLanding(INT(f->x), oy, INT(f->y), &sy);
                    if (fl != NONE)
                    {
                        f->y = FIX(sy);
                        f->floor = fl;
                        f->state = FS_WALK;
                        f->dir = 1;
                        f->t = 30;
                    }
                }
                if (INT(f->y) > 250) f->active = FALSE;
            }
            break;

        case FS_WALK:
            fireWalk(f);
            break;

        case FS_CLIMB:
        {
            const Ladder* l = &lad[f->lad];
            f->y += f->vdir * FIRE_CLIMB;
            if (f->vdir < 0 && INT(f->y) <= ladTop(l))
            {
                f->y = FIX(ladTop(l));
                f->floor = l->ft;
                f->state = FS_WALK;
                f->t = 1;
            }
            else if (f->vdir > 0 && INT(f->y) >= ladBot(l))
            {
                f->y = FIX(ladBot(l));
                f->floor = l->fb;
                f->state = FS_WALK;
                f->t = 1;
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Collisions                                                           */
/* ------------------------------------------------------------------ */

static void smashAt(s16 x, s16 y)
{
    for (u16 i = 0; i < MAXSMASH; i++)
    {
        if (!smashes[i].active)
        {
            smashes[i].active = TRUE;
            smashes[i].t = 24;
            smashes[i].x = x;
            smashes[i].y = y;
            break;
        }
    }
    SND_sfx(SFX_SMASH);
}

static bool hammerHits(s16 x, s16 y, s16 hw, s16 h)
{
    s16 mx = INT(m.x), my = INT(m.y);

    if (!m.hammer || m.state == MS_CLIMB) return FALSE;
    if (((m.hammer >> 3) & 1) == 0)
        return overlap(mx + m.dir * 3, my - 6, 10, 26, x, y, hw, h);     /* raised */
    return overlap(mx + m.dir * 11, my, 8, 14, x, y, hw, h);             /* down in front */
}

static void checkCollisions(void)
{
    s16 mx = INT(m.x), my = INT(m.y);
    s16 bodyW = m.hammer ? 3 : 5;

    for (u16 i = 0; i < MAXB; i++)
    {
        Barrel* b = &bar[i];
        if (!b->active) continue;
        s16 bx = INT(b->x), by = INT(b->y);

        if (hammerHits(bx, by, 5, 10))
        {
            u32 pts = rnd(4) == 0 ? 800 : (b->blue ? 500 : 300);
            b->active = FALSE;
            smashAt(bx, by);
            award(pts, bx, by - 16);
            continue;
        }
        if (overlap(mx, my, bodyW, 14, bx, by, 5, 9)) dead = TRUE;
    }

    for (u16 i = 0; i < MAXFIRE; i++)
    {
        Fire* f = &fire[i];
        if (!f->active) continue;
        if (f->state == FS_SPAWN && f->t) continue;     /* still materialising */
        s16 fx = INT(f->x), fy = INT(f->y);

        if (hammerHits(fx, fy, 5, 11))
        {
            f->active = FALSE;
            smashAt(fx, fy);
            award(rnd(4) == 0 ? 800 : 500, fx, fy - 18);
            continue;
        }
        if (overlap(mx, my, bodyW, 14, fx, fy, 5, 11)) dead = TRUE;
    }

    if (overlap(mx, my, 5, 14, INT(kong.x), kong.y, 13, 30)) dead = TRUE;

    /* oil drum flames */
    if (stage == STAGE_BARRELS && drumLit && overlap(mx, my, 5, 14, DRUM_X - 8, 200, 7, 14)) dead = TRUE;

#ifdef CHEAT_INVINCIBLE   /* test builds only: -DCHEAT_INVINCIBLE */
    dead = FALSE;
#endif
}

/* ------------------------------------------------------------------ */
/* Drawing                                                              */
/* ------------------------------------------------------------------ */

static void drawMario(void)
{
    s16 mx = INT(m.x), my = INT(m.y);

    if (m.hidden) return;

    if (m.hammer && m.state != MS_CLIMB)
    {
        bool blink = (m.hammer < 120) && ((m.hammer >> 2) & 1);
        u16 pal = blink ? PAL_KONG : PAL_MARIO;

        if (((m.hammer >> 3) & 1) == 0)
            spr_put(mx - 8, my - 29, SZ16, ATTR(pal, 0, m.dir < 0, sHammer[0]));
        else if (m.dir > 0)
            spr_put(mx - 2, my - 12, SZ16, ATTR(pal, 0, 0, sHammer[1]));
        else
            spr_put(mx - 14, my - 12, SZ16, ATTR(pal, 0, 1, sHammer[1]));
    }
    spr_put(mx - 8, my - 16, SZ16, ATTR(PAL_MARIO, m.flipV, m.flipH, sMario[m.frame]));
}

static void drawScene(void)
{
    spr_begin();

    for (u16 i = 0; i < MAXPOP; i++)
        if (pops[i].active) spr_put(pops[i].x - 8, pops[i].y, SPRITE_SIZE(2, 1), ATTR(PAL_MISC, 0, 0, sPoints[pops[i].idx]));

    for (u16 i = 0; i < MAXSMASH; i++)
        if (smashes[i].active)
            spr_put(smashes[i].x - 8, smashes[i].y - 14, SZ16, ATTR(PAL_MISC, 0, 0, sSmash[(smashes[i].t >> 3) & 1]));

    if (heartShown) spr_put(heartX - 8, heartY - 16, SZ16, ATTR(PAL_MISC, 0, 0, sHeart));

    drawMario();

    for (u16 i = 0; i < MAXFIRE; i++)
    {
        Fire* f = &fire[i];
        if (!f->active) continue;
        if (f->state == FS_SPAWN && f->t && (f->t & 4)) continue;   /* blink while appearing */
        s16 bob = ((frameCount >> 3) & 1);
        spr_put(INT(f->x) - 8, INT(f->y) - 14 - bob, SZ16, ATTR(PAL_MISC, 0, f->dir < 0, sFire[(frameCount >> 2) & 1]));
    }

    for (u16 i = 0; i < MAXB; i++)
    {
        Barrel* b = &bar[i];
        if (!b->active) continue;
        u16 pal = b->blue ? PAL_MISC : PAL_KONG;
        s16 bx = INT(b->x), by = INT(b->y);
        u16 tile;

        if (b->state == BS_LADDER)
            tile = sBarrelSide[(by >> 2) & 1];
        else
        {
            u16 fr = (bx >> 2) & 3;
            if (b->dir < 0) fr = 3 - fr;
            tile = sBarrel[fr];
        }
        spr_put(bx - 8, by - 16, SZ16, ATTR(pal, 0, 0, tile));
    }

    /* Kong and the barrel in his hands */
    {
        s16 kx = INT(kong.x), ky = kong.y;
        u16 bpal = kong.nextBlue ? PAL_MISC : PAL_KONG;

        if (stage == STAGE_BARRELS)
        {
            if (kong.state == K_GRAB)
                spr_put(kx - 30, ky - 16, SZ16, ATTR(bpal, 0, 0, sBarrelSide[0]));
            else if (kong.state == K_HOLD)
                spr_put(kx - 8, ky - 44, SZ16, ATTR(bpal, 0, 0, sBarrelSide[0]));
            else if (kong.state == K_THROW)
                spr_put(kx + 14, ky - 16, SZ16, ATTR(bpal, 0, 0, sBarrel[0]));
        }
        spr_put(kx - 16, ky - 32, SZ32, ATTR(PAL_KONG, kong.flipV, 0, sKong[kong.frame]));
    }

    if (stage == STAGE_BARRELS)
    {
        /* barrel pile next to Kong */
        spr_put(PF_X, 28, SZ16, ATTR(PAL_BG, 0, 0, tPile));
        spr_put(PF_X + 16, 28, SZ16, ATTR(PAL_BG, 0, 0, tPile));
        spr_put(PF_X, 44, SZ16, ATTR(PAL_BG, 0, 0, tPile));
        spr_put(PF_X + 16, 44, SZ16, ATTR(PAL_BG, 0, 0, tPile));
    }

    /* Pauline */
    {
        s16 py = (stage == STAGE_BARRELS) ? 32 : 31;
        bool help = (helpT & 255) < 64;
        spr_put(160 - 8, py - 22, SPRITE_SIZE(2, 3), ATTR(PAL_MARIO, 0, 0, sPauline[help ? ((helpT >> 3) & 1) : 0]));
    }

    for (u16 i = 0; i < 3; i++)
        if (items[i].active) spr_put(items[i].x - 8, items[i].y - 13, SZ16, ATTR(PAL_MISC, 0, 0, sItem[items[i].type]));

    for (u16 i = 0; i < 2; i++)
        if (hammers[i].active) spr_put(hammers[i].x - 8, hammers[i].y - 13, SZ16, ATTR(PAL_MARIO, 0, 0, sHammer[0]));

    if (stage == STAGE_RIVETS)
    {
        for (u16 i = 0; i < NRIVET; i++)
            if (rivets[i].state == 0)
                spr_put(PF_X + rivets[i].c * 8, 187 - (rivets[i].f - 1) * 29 - 3, SPRITE_SIZE(1, 2), ATTR(PAL_MISC, 0, 0, sRivet));
    }

    if (stage == STAGE_BARRELS && drumLit)
        spr_put(PF_X, 186, SZ16, ATTR(PAL_MISC, 0, (frameCount >> 4) & 1, sFlame[(frameCount >> 3) & 1]));

    spr_end();
}

static void updateEffects(void)
{
    for (u16 i = 0; i < MAXPOP; i++)
        if (pops[i].active && --pops[i].t == 0) pops[i].active = FALSE;
    for (u16 i = 0; i < MAXSMASH; i++)
        if (smashes[i].active && --smashes[i].t == 0) smashes[i].active = FALSE;
}

static void updateHelp(void)
{
    helpT++;
    if ((helpT & 255) == 0) text("HELP!", 22, 1, PAL_MISC);
    else if ((helpT & 255) == 64) text("     ", 22, 1, PAL_MISC);
}

/* ------------------------------------------------------------------ */
/* Sequences                                                            */
/* ------------------------------------------------------------------ */

/* one frame of a cut-scene: effects keep animating, nothing else moves */
static void idleFrame(void)
{
    updateEffects();
    drawScene();
    frameEnd();
}

static void waitFrames(u16 n)
{
    while (n--) idleFrame();
}

static void deathSequence(void)
{
    SND_stopAll();
    m.hammer = 0;
    waitFrames(40);

    SND_music(MUS_DEATH);
    /* spin */
    for (u16 i = 0; i < 64; i++)
    {
        switch ((i >> 2) & 3)
        {
            case 0: m.frame = MF_STAND; m.flipH = 0; m.flipV = 0; break;
            case 1: m.frame = MF_CLIMB; m.flipH = 0; m.flipV = 0; break;
            case 2: m.frame = MF_STAND; m.flipH = 1; m.flipV = 0; break;
            default: m.frame = MF_STAND; m.flipH = 0; m.flipV = 1; break;
        }
        idleFrame();
    }
    m.frame = MF_DEAD;
    m.flipH = 0;
    m.flipV = 0;
    for (u16 i = 0; i < 240 && !SND_musicDone(); i++) idleFrame();
    waitFrames(40);
    SND_stopAll();
}

static void tallyBonus(void)
{
    HUD_message("BONUS", PAL_MISC);
    waitFrames(30);
    while (bonus)
    {
        u16 step = (bonus >= 100) ? 100 : bonus;
        bonus -= step;
        addScore(step);
        HUD_drawBonus();
        SND_sfx(SFX_BONUS);
        waitFrames(3);
    }
    waitFrames(60);
    HUD_message("     ", PAL_MISC);
}

static void clearEnemies(void)
{
    memset(bar, 0, sizeof(bar));
    memset(fire, 0, sizeof(fire));
    memset(smashes, 0, sizeof(smashes));
    m.hammer = 0;
    kong.state = K_IDLE;
    kong.frame = 0;
}

static void winBarrels(void)
{
    SND_stopAll();
    SND_music(MUS_CLEAR);
    clearEnemies();
    m.frame = MF_STAND;
    m.dir = -1;
    m.flipH = 1;
    heartShown = TRUE;
    heartX = (INT(m.x) + 160) / 2;
    heartY = 16;
    text("     ", 22, 1, PAL_MISC);

    for (u16 i = 0; i < 180; i++)
    {
        kong.frame = (i >> 3) & 1;
        idleFrame();
    }
    kong.frame = 0;
    tallyBonus();
}

static void winRivets(void)
{
    SND_stopAll();
    clearEnemies();
    text("     ", 22, 1, PAL_MISC);

    /* the floors give way: Kong falls to the bottom */
    for (u16 i = 0; i < 60; i++)
    {
        kong.frame = (i >> 3) & 1;
        idleFrame();
    }
    for (u16 c = 6; c < 22; c++) clearGirderCell(c, 71);
    SND_sfx(SFX_FALL);
    kong.flipV = TRUE;
    kong.frame = 1;
    while (kong.y < 216)
    {
        kong.y += 2;
        idleFrame();
    }
    SND_sfx(SFX_SMASH);

    /* Mario is reunited with Pauline */
    m.state = MS_WALK;
    m.x = FIX(138);
    m.y = FIX(31);
    m.dir = 1;
    m.flipH = 0;
    m.frame = MF_STAND;
    heartShown = TRUE;
    heartX = 149;
    heartY = 16;
    SND_music(MUS_CLEAR);
    waitFrames(200);
    tallyBonus();
}

static void pauseGame(void)
{
    u16 mus = SND_currentMusic();

    SND_stopAll();
    HUD_message("PAUSE", PAL_KONG);
    do
    {
        frameEnd();
    } while (!PRESSED(BUTTON_START));
    HUD_message("     ", PAL_KONG);
    SND_music(mus);
}

/* ------------------------------------------------------------------ */
/* Main stage loop                                                      */
/* ------------------------------------------------------------------ */

u16 GAME_playStage(u16 type, bool fresh)
{
    setupStage(type, fresh);

    /* short freeze before the action starts */
    waitFrames(40);
    SND_music(type == STAGE_BARRELS ? MUS_BARRELS : MUS_RIVETS);

    while (TRUE)
    {
        if (PRESSED(BUTTON_START)) pauseGame();

        stageT++;
        updateHelp();

        /* bonus timer */
        if (++bonusT >= 120)
        {
            bonusT = 0;
            if (bonus >= 100) bonus -= 100;
            else bonus = 0;
            HUD_drawBonus();
            if (bonus && bonus <= 1000) SND_sfx(SFX_WARN);
            if (bonus == 0) dead = TRUE;
        }

        updateMario();

        if (stage == STAGE_BARRELS)
            updateKongBarrels();
        else
        {
            updateKongRivets();
            updateRivets();
            if (fireSpawnT) fireSpawnT--;
            if (fireSpawnT == 0)
            {
                u16 base = (level * 20 < 120) ? 240 - level * 20 : 120;
                spawnRivetFire();
                fireSpawnT = base + rnd(120);
            }
        }

        for (u16 i = 0; i < MAXB; i++)
            if (bar[i].active) updateBarrel(&bar[i]);
        for (u16 i = 0; i < MAXFIRE; i++)
            if (fire[i].active) updateFire(&fire[i]);

        checkCollisions();
        updateEffects();

        drawScene();
        frameEnd();

        if (dead)
        {
            deathSequence();
            return RES_DEAD;
        }
        if (stage == STAGE_BARRELS && m.state == MS_WALK && goalFloor != NONE && m.floor == goalFloor)
        {
            winBarrels();
            return RES_CLEAR;
        }
        if (stage == STAGE_RIVETS && rivetsLeft == 0 && m.state == MS_WALK)
        {
            winRivets();
            return RES_CLEAR;
        }
    }
}
