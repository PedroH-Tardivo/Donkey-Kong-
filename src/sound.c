/*
 * Sound: everything is played on the PSG (SN76489) straight from the 68000,
 * no Z80 driver needed.
 *
 *  channel 0 : music melody
 *  channel 1 : music bass
 *  channel 2 : tone sound effects
 *  channel 3 : noise sound effects
 */
#include "sound.h"

typedef struct
{
    u16 hz;     /* 0 = rest */
    u8 len;     /* frames, 0 = end of list */
} Note;

/* note frequencies (Hz) - the PSG can't go below ~110 Hz */
#define A2  110
#define B2  123
#define C3  131
#define D3  147
#define E3  165
#define F3  175
#define G3  196
#define A3  220
#define As3 233
#define B3  247
#define C4  262
#define Cs4 277
#define D4  294
#define Ds4 311
#define E4  330
#define F4  349
#define Fs4 370
#define G4  392
#define Gs4 415
#define A4  440
#define As4 466
#define B4  494
#define C5  523
#define Cs5 554
#define D5  587
#define Ds5 622
#define E5  659
#define F5  698
#define Fs5 740
#define G5  784
#define Gs5 831
#define A5  880
#define As5 932
#define B5  988
#define C6  1047
#define D6  1175
#define E6  1319
#define G6  1568
#define END { 0, 0 }

/* ------------------------------------------------------------------ */
/* Music                                                                */
/* ------------------------------------------------------------------ */

static const Note titleMel[] =
{
    { C4, 8 }, { E4, 8 }, { G4, 8 }, { C5, 16 }, { 0, 4 }, { G4, 8 }, { C5, 24 }, { 0, 8 },
    { A4, 8 }, { C5, 8 }, { F5, 16 }, { E5, 8 }, { D5, 8 }, { C5, 8 }, { D5, 8 }, { G5, 32 },
    END
};
static const Note titleBass[] =
{
    { C3, 16 }, { G3, 16 }, { C3, 16 }, { G3, 20 }, { F3, 16 }, { A3, 16 }, { G3, 16 }, { G3, 32 },
    END
};

static const Note startMel[] =
{
    { G4, 6 }, { 0, 2 }, { G4, 6 }, { 0, 2 }, { A4, 8 }, { B4, 8 }, { D5, 8 }, { 0, 8 },
    { B4, 8 }, { D5, 8 }, { G5, 24 },
    END
};
static const Note startBass[] =
{
    { G3, 16 }, { D3, 16 }, { G3, 16 }, { D3, 16 }, { G3, 24 },
    END
};

static const Note barrelsBass[] =
{
    { C3, 10 }, { 0, 14 }, { C3, 10 }, { 0, 14 }, { G3, 10 }, { 0, 14 }, { C3, 10 }, { 0, 14 },
    { A2, 10 }, { 0, 14 }, { A2, 10 }, { 0, 14 }, { E3, 10 }, { 0, 14 }, { G3, 10 }, { 0, 14 },
    END
};

static const Note rivetsMel[] =
{
    { 0, 96 }, { E5, 6 }, { 0, 6 }, { D5, 6 }, { 0, 6 }, { C5, 12 }, { 0, 66 },
    END
};
static const Note rivetsBass[] =
{
    { A3, 8 }, { 0, 8 }, { E3, 8 }, { 0, 8 }, { A3, 8 }, { 0, 8 }, { C4, 8 }, { 0, 8 },
    { B3, 8 }, { 0, 8 }, { E3, 8 }, { 0, 8 }, { Gs4, 8 }, { 0, 8 }, { B3, 8 }, { 0, 8 },
    END
};

static const Note hammerMel[] =
{
    { C5, 5 }, { E5, 5 }, { G5, 5 }, { C6, 5 }, { G5, 5 }, { E5, 5 },
    { D5, 5 }, { F5, 5 }, { A5, 5 }, { D6, 5 }, { A5, 5 }, { F5, 5 },
    { E5, 5 }, { G5, 5 }, { B5, 5 }, { E6, 5 }, { B5, 5 }, { G5, 5 },
    { F5, 5 }, { A5, 5 }, { C6, 5 }, { A5, 5 }, { G5, 5 }, { B5, 5 },
    END
};
static const Note hammerBass[] =
{
    { C3, 10 }, { G3, 10 }, { C3, 10 }, { D3, 10 }, { A3, 10 }, { D3, 10 },
    { E3, 10 }, { B3, 10 }, { E3, 10 }, { F3, 10 }, { C4, 10 }, { G3, 10 },
    END
};

static const Note deathMel[] =
{
    { C6, 5 }, { B5, 5 }, { As5, 5 }, { A5, 5 }, { Gs5, 5 }, { G5, 5 }, { Fs5, 5 }, { F5, 5 },
    { E5, 5 }, { Ds5, 5 }, { D5, 5 }, { Cs5, 5 }, { C5, 10 }, { 0, 30 },
    { C4, 8 }, { 0, 6 }, { C4, 8 }, { 0, 6 }, { G4, 8 }, { 0, 6 }, { C4, 24 },
    END
};

static const Note clearMel[] =
{
    { G4, 6 }, { C5, 6 }, { E5, 6 }, { G5, 12 }, { E5, 6 }, { G5, 30 }, { 0, 10 },
    { A5, 6 }, { G5, 6 }, { E5, 6 }, { C5, 6 }, { D5, 6 }, { E5, 6 }, { C5, 40 },
    END
};
static const Note clearBass[] =
{
    { C4, 12 }, { G3, 12 }, { C4, 12 }, { G3, 30 }, { 0, 6 },
    { F3, 12 }, { A3, 12 }, { G3, 12 }, { C3, 40 },
    END
};

static const Note gameoverMel[] =
{
    { G4, 16 }, { 0, 4 }, { E4, 16 }, { 0, 4 }, { C4, 16 }, { 0, 4 }, { D4, 12 }, { E4, 12 },
    { D4, 12 }, { C4, 12 }, { B3, 12 }, { C4, 40 },
    END
};
static const Note gameoverBass[] =
{
    { C3, 20 }, { G3, 20 }, { E3, 20 }, { F3, 24 }, { G3, 24 }, { C3, 52 },
    END
};

static const Note hiscoreMel[] =
{
    { E5, 8 }, { G5, 8 }, { E5, 8 }, { C5, 8 }, { D5, 8 }, { F5, 8 }, { D5, 8 }, { B4, 8 },
    { C5, 8 }, { E5, 8 }, { G5, 8 }, { C6, 8 }, { B5, 16 }, { G5, 16 },
    END
};
static const Note hiscoreBass[] =
{
    { C3, 16 }, { G3, 16 }, { G3, 16 }, { D3, 16 }, { C3, 16 }, { E3, 16 }, { G3, 32 },
    END
};

typedef struct
{
    const Note* mel;
    const Note* bass;
    u8 melVol;      /* attenuation 0 (loud) .. 15 (off) */
    u8 bassVol;
    u8 loop;
} Track;

static const Track tracks[] =
{
    [MUS_NONE]     = { NULL,        NULL,         15, 15, FALSE },
    [MUS_TITLE]    = { titleMel,    titleBass,     2,  4, FALSE },
    [MUS_START]    = { startMel,    startBass,     2,  4, FALSE },
    [MUS_BARRELS]  = { NULL,        barrelsBass,  15,  5, TRUE  },
    [MUS_RIVETS]   = { rivetsMel,   rivetsBass,    6,  6, TRUE  },
    [MUS_HAMMER]   = { hammerMel,   hammerBass,    4,  5, TRUE  },
    [MUS_DEATH]    = { deathMel,    NULL,          1, 15, FALSE },
    [MUS_CLEAR]    = { clearMel,    clearBass,     1,  3, FALSE },
    [MUS_GAMEOVER] = { gameoverMel, gameoverBass,  2,  4, FALSE },
    [MUS_HISCORE]  = { hiscoreMel,  hiscoreBass,   3,  5, TRUE  },
};

typedef struct
{
    const Note* start;
    const Note* cur;
    u8 left;
    u8 age;
    u8 vol;
    u8 active;
} Voice;

static Voice voice[2];
static u16 curTrack;
static u8 trackLoop;

/* ------------------------------------------------------------------ */
/* Sound effects                                                        */
/* ------------------------------------------------------------------ */

typedef struct
{
    u8 type;        /* 0 none, 1 sweep, 2 notes */
    u16 from, to;
    u8 frames, pos;
    u8 vol;
    const Note* notes;
    u8 left;
    u8 prio;
} Sfx;

static Sfx sfx;

static struct
{
    u8 frames;
    u8 vol;
    u8 decay;
    u8 pos;
} noise;

static const Note sfxScore[] = { { E6, 3 }, { G6, 5 }, END };
static const Note sfxItem[] = { { C6, 3 }, { E6, 3 }, { G6, 3 }, { C6, 3 }, { E6, 3 }, { G6, 6 }, END };
static const Note sfxRivet[] = { { G5, 2 }, { C6, 2 }, { G6, 4 }, END };
static const Note sfxWarn[] = { { A5, 6 }, { 0, 4 }, { A5, 6 }, END };
static const Note sfxLife[] = { { C5, 4 }, { E5, 4 }, { G5, 4 }, { C6, 4 }, { E6, 4 }, { G6, 4 }, { C6, 4 }, { G6, 10 }, END };
static const Note sfxBonus[] = { { C6, 2 }, END };
static const Note sfxSelect[] = { { G5, 3 }, { C6, 4 }, END };

static void psgOff(u8 ch)
{
    PSG_setEnvelope(ch, 15);
}

void SND_init(void)
{
    PSG_reset();
    for (u8 ch = 0; ch < 4; ch++) psgOff(ch);
    memset(voice, 0, sizeof(voice));
    memset(&sfx, 0, sizeof(sfx));
    memset(&noise, 0, sizeof(noise));
    curTrack = MUS_NONE;
}

void SND_music(u16 track)
{
    const Track* t = &tracks[track];

    curTrack = track;
    trackLoop = t->loop;

    voice[0].start = voice[0].cur = t->mel;
    voice[0].vol = t->melVol;
    voice[0].active = (t->mel != NULL);
    voice[0].left = 0;

    voice[1].start = voice[1].cur = t->bass;
    voice[1].vol = t->bassVol;
    voice[1].active = (t->bass != NULL);
    voice[1].left = 0;

    psgOff(0);
    psgOff(1);
}

u16 SND_currentMusic(void)
{
    return curTrack;
}

bool SND_musicDone(void)
{
    return !voice[0].active && !voice[1].active;
}

void SND_stopAll(void)
{
    SND_music(MUS_NONE);
    sfx.type = 0;
    noise.frames = 0;
    for (u8 ch = 0; ch < 4; ch++) psgOff(ch);
}

static void startNotes(const Note* n, u8 vol, u8 prio)
{
    if (sfx.type && sfx.prio > prio) return;
    sfx.type = 2;
    sfx.notes = n;
    sfx.left = 0;
    sfx.vol = vol;
    sfx.prio = prio;
}

static void startSweep(u16 from, u16 to, u8 frames, u8 vol, u8 prio)
{
    if (sfx.type && sfx.prio > prio) return;
    sfx.type = 1;
    sfx.from = from;
    sfx.to = to;
    sfx.frames = frames;
    sfx.pos = 0;
    sfx.vol = vol;
    sfx.prio = prio;
}

static void startNoise(u8 rate, u8 vol, u8 frames, u8 decay)
{
    PSG_setNoise(1, rate);
    noise.vol = vol;
    noise.frames = frames;
    noise.decay = decay;
    noise.pos = 0;
}

void SND_sfx(u16 id)
{
    switch (id)
    {
        case SFX_JUMP:   startSweep(300, 900, 10, 2, 1); break;
        case SFX_SCORE:  startNotes(sfxScore, 1, 2); break;
        case SFX_ITEM:   startNotes(sfxItem, 1, 3); break;
        case SFX_WALK:   if (!noise.frames) startNoise(2, 11, 2, 0); break;
        case SFX_SMASH:  startNoise(1, 1, 14, 1); startSweep(1200, 200, 12, 2, 2); break;
        case SFX_THROW:  startNoise(0, 3, 10, 1); break;
        case SFX_RIVET:  startNotes(sfxRivet, 1, 2); break;
        case SFX_WARN:   startNotes(sfxWarn, 3, 1); break;
        case SFX_LIFE:   startNotes(sfxLife, 1, 4); break;
        case SFX_LAND:   startNoise(1, 6, 3, 2); break;
        case SFX_BONUS:  startNotes(sfxBonus, 3, 1); break;
        case SFX_FALL:   startSweep(1200, 300, 30, 2, 3); break;
        case SFX_SELECT: startNotes(sfxSelect, 2, 2); break;
    }
}

static void updateVoice(u8 ch, Voice* v)
{
    if (!v->active) return;

    if (v->left == 0)
    {
        if (v->cur->len == 0)
        {
            if (trackLoop)
                v->cur = v->start;
            else
            {
                v->active = FALSE;
                psgOff(ch);
                return;
            }
        }
        v->left = v->cur->len;
        v->age = 0;
        if (v->cur->hz)
        {
            PSG_setFrequency(ch, v->cur->hz);
            PSG_setEnvelope(ch, v->vol);
        }
        else psgOff(ch);
        v->cur++;
    }
    else
    {
        /* simple decay envelope */
        const Note* n = v->cur - 1;

        v->age++;
        if (n->hz)
        {
            u16 att = v->vol + (v->age >> 3);
            if (att > 15) att = 15;
            PSG_setEnvelope(ch, att);
        }
    }
    v->left--;
}

static void updateSfx(void)
{
    if (sfx.type == 1)
    {
        if (sfx.pos >= sfx.frames)
        {
            sfx.type = 0;
            psgOff(2);
            return;
        }
        s32 f = sfx.from + ((s32)(sfx.to - sfx.from) * sfx.pos) / sfx.frames;
        PSG_setFrequency(2, f);
        PSG_setEnvelope(2, sfx.vol);
        sfx.pos++;
    }
    else if (sfx.type == 2)
    {
        if (sfx.left == 0)
        {
            if (sfx.notes->len == 0)
            {
                sfx.type = 0;
                psgOff(2);
                return;
            }
            sfx.left = sfx.notes->len;
            if (sfx.notes->hz)
            {
                PSG_setFrequency(2, sfx.notes->hz);
                PSG_setEnvelope(2, sfx.vol);
            }
            else psgOff(2);
            sfx.notes++;
        }
        sfx.left--;
    }

    if (noise.frames)
    {
        u16 att = noise.vol + (noise.decay ? (noise.pos / noise.decay) : 0);
        if (att > 15) att = 15;
        PSG_setEnvelope(3, att);
        noise.pos++;
        noise.frames--;
        if (!noise.frames) psgOff(3);
    }
}

void SND_update(void)
{
    updateVoice(0, &voice[0]);
    updateVoice(1, &voice[1]);
    updateSfx();
}
