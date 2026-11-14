/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Music for audio packs (runtime/module.h ct_music_hooks): which song the
 * game asks its sound program for, and the fades and stops it asks for,
 * from the commands the CPU sends through the APU ports. Linked into the
 * game module (the ct_game_music export) and into the static programs.
 * Reads only. docs/ASSET-PACKS.md has the protocol.
 *
 * The game's sound commands go through a block of four bytes at $7E1E00
 * (command, then three arguments) and a JSL to $C70004
 * (Audio_ProcessCommand, $C70140). The routine sends a command to the
 * SPC700 by writing $2143, $2142, $2141 and then $2140 with those four
 * bytes ($C7061C; a song through $C70A39), so a write to port 0 whose four
 * ports equal $1E00-$1E03 is a command:
 *
 *   $10 $11 $14 $15 SONG  load and play song SONG; 0 is silence. The CPU
 *                         then uploads the song's data for up to about 50
 *                         frames and clears $1E00 when it is done, which is
 *                         when the sound program starts the song.
 *   $18-$2F ID ...        sound effects (on voices the program sets aside:
 *                         ARAM $86 holds them as a bit mask)
 *   $81 TIME VOL          the music's volume to VOL (0-255) in TIME steps
 *                         of about 2.06 frames each (measured: 32 steps are
 *                         66 frames); TIME 0 at once
 *   $F0                   stop the music
 *
 * The other commands ($70, $82, $83 and more) change nothing a replaced
 * song needs. */
#include <string.h>

#include "bus.h"
#include "module.h"

extern const ct_music_hooks ct_game_music;

enum {
    CMD = 0x1E00,         /* WRAM: command, then 3 arguments */
    SFX_VOICES = 0x86,    /* ARAM: voices playing sound effects, bit n = voice n */
};

static ct_music_event queue[32];
static int queued;
static int loading;       /* a song is being uploaded */
static uint8_t loading_song;

static void push(ct_music_event e)
{
    /* The routine sends a command again when the SPC700 doesn't answer
       in time: one event for it. */
    if (queued && !memcmp(&queue[queued - 1], &e, sizeof e))
        return;
    if (queued < (int)(sizeof queue / sizeof queue[0]))
        queue[queued++] = e;
}

static void port_write(unsigned port, uint8_t v, const uint8_t ports[4])
{
    if (port != 0)
        return;
    const uint8_t *w = bus_wram();
    if (memcmp(ports, w + CMD, 4))
        return;   /* not a command block (an upload handshake, say) */
    switch (v) {
    case 0x10:
    case 0x11:
    case 0x14:
    case 0x15:
        loading = 1;
        loading_song = ports[1];
        break;
    case 0x81:
        push((ct_music_event){.kind = CT_MUSIC_FADE, .volume = ports[2],
                              .frames = (uint16_t)((ports[1] * 33 + 8) / 16)});
        break;
    case 0xF0:
        loading = 0;
        push((ct_music_event){.kind = CT_MUSIC_STOP});
        break;
    default:
        break;
    }
}

static int events(ct_music_event *out, int max)
{
    /* The upload is over when the command byte is cleared ($C70194). */
    if (loading && bus_wram()[CMD] == 0) {
        loading = 0;
        if (loading_song)
            push((ct_music_event){.kind = CT_MUSIC_PLAY, .song = loading_song});
        else
            push((ct_music_event){.kind = CT_MUSIC_STOP});
    }
    int n = queued < max ? queued : max;
    memcpy(out, queue, (size_t)n * sizeof *out);
    memmove(queue, queue + n, (size_t)(queued - n) * sizeof *queue);
    queued -= n;
    return n;
}

const ct_music_hooks ct_game_music = {port_write, events, SFX_VOICES};
