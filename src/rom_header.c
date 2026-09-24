#include <genesis.h>

/* Declares 64 KB of battery backed SRAM (odd bytes) for the high score table */
__attribute__((externally_visible))
const ROMHeader rom_header = {
    "SEGA MEGA DRIVE ",
    "(C)FAN 2026.SEP ",
    "DONKEY KONG                                     ",
    "DONKEY KONG                                     ",
    "GM 00000000-00",
    0x000,
    "J               ",
    0x00000000,
    0x000FFFFF,
    0xE0FF0000,
    0xE0FFFFFF,
    "RA",
    0xF820,
    0x00200001,
    0x0020FFFF,
    "            ",
    "DONKEY KONG FAN REMAKE FOR MEGA DRIVE   ",
    "JUE             "
};
