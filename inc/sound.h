#ifndef SOUND_H
#define SOUND_H

#include <genesis.h>

enum
{
    MUS_NONE,
    MUS_TITLE,
    MUS_START,
    MUS_BARRELS,
    MUS_RIVETS,
    MUS_HAMMER,
    MUS_DEATH,
    MUS_CLEAR,
    MUS_GAMEOVER,
    MUS_HISCORE,
};

enum
{
    SFX_JUMP,
    SFX_SCORE,
    SFX_ITEM,
    SFX_WALK,
    SFX_SMASH,
    SFX_THROW,
    SFX_RIVET,
    SFX_WARN,
    SFX_LIFE,
    SFX_LAND,
    SFX_BONUS,
    SFX_FALL,
    SFX_SELECT,
};

void SND_init(void);
void SND_update(void);
void SND_music(u16 track);
u16  SND_currentMusic(void);
bool SND_musicDone(void);
void SND_sfx(u16 id);
void SND_stopAll(void);

#endif
