
#ifndef ZELDA3_SNES_PPU_H_
#define ZELDA3_SNES_PPU_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "snes/saveload.h"
typedef struct Ppu Ppu;

#include "src/types.h"

typedef struct BgLayer {
  uint16_t hScroll;
  uint16_t vScroll;
  // -- snapshot starts here
  bool tilemapWider;
  bool tilemapHigher;
  uint16_t tilemapAdr;
  // -- snapshot ends here
  uint16_t tileAdr;
  bool bigTiles;   // ct-recomp: BGMODE bits 4-7, 16x16 characters
} BgLayer;

enum {
  kPpuXPixels = 256 + kPpuExtraLeftRight * 2,
};

typedef uint16_t PpuZbufType;

// port: provenance of one output pixel (Ppu.provBuffer): which character
// and palette the main screen's winning pixel came from, for texture
// replacement. 12 bytes.
typedef struct PpuProv {
  // BG: VRAM word address of the 8x8 character drawn. OBJ: the sprite's
  // name table base (OBSEL's first or second table); its characters are
  // name, name+1 .. (16x16 page wrap, see ppu_evaluateSprites).
  uint16_t chr;
  uint8_t pal;     // CGRAM index of the palette's color 0
  uint8_t cidx;    // CGRAM index drawn (before brightness and color math)
  // bits 0-1: bit depth (1: 2bpp, 2: 4bpp, 3: 8bpp; 0: nothing to
  // replace: backdrop, mode 7, forced blank), 2: H flip, 3: V flip,
  // 4: OBJ
  uint8_t flags;
  // The texel drawn, as stored (unflipped): BG 0-7 within the character,
  // OBJ 0 .. size-1 within the whole sprite.
  uint8_t u, v;
  uint8_t index;   // OBJ: OAM index 0-127; BG: the layer 0-3
  uint8_t name;    // OBJ: character number (OAM byte 2)
  uint8_t size;    // OBJ: width and height in pixels; BG: 8
  uint8_t layer;   // layer byte as Ppu.layerBuffer writes it
  uint8_t reserved;
} PpuProv;

enum { kPpuProvBpp = 3, kPpuProvHFlip = 4, kPpuProvVFlip = 8, kPpuProvObj = 16 };

typedef struct PpuPixelPrioBufs {
  // This holds the prio in the upper 8 bits and the color in the lower 8 bits.
  PpuZbufType data[kPpuXPixels];
} PpuPixelPrioBufs;

enum {
  kPpuRenderFlags_NewRenderer = 1,
  // Render mode7 upsampled by 4x4
  kPpuRenderFlags_4x4Mode7 = 2,
  // Use 240 height instead of 224
  kPpuRenderFlags_Height240 = 4,
  // Disable sprite render limits
  kPpuRenderFlags_NoSpriteLimits = 8,
};


struct Ppu {
  bool lineHasSprites;
  uint8_t lastBrightnessMult;
  uint8_t lastMosaicModulo;
  uint8_t renderFlags;
  int drawLine, drawX;   // ct-recomp: line being drawn, next pixel (256: done)
  uint32_t renderPitch;
  uint8_t *renderBuffer;
  // port: optional per-pixel layer G-buffer (NULL: off), one byte
  // per output pixel: z level << 4 | math << 3 | layer. z level is the
  // whole-line renderer's priority scale (0 backdrop .. 15 front), layer
  // is 0-3 BG1-4, 4/6 OBJ (palettes 4-7 / 0-3), 5 backdrop; math is set
  // when color math changed the pixel. Used by the HD-2D renderer.
  uint8_t *layerBuffer;
  uint32_t layerPitch;
  // port: optional "behind" G-buffer (NULL behindColor: off). Per output
  // pixel, the runner-up: the best layer that lost to the winner there
  // (the backdrop when none did). behindColor is B G R x like the frame
  // (its CGRAM color with brightness applied, before color math and
  // clipping); behindLayer is z level << 4 | layer, as layerBuffer, with
  // the math bit clear. Main screen only.
  uint8_t *behindColor, *behindLayer;
  uint32_t behindColorPitch, behindLayerPitch;
  // port: optional provenance buffer (NULL: off), one PpuProv per output
  // pixel (provPitch in PpuProv units). Per-pixel renderer only; the
  // whole-line renderer clears its rows.
  PpuProv *provBuffer;
  uint32_t provPitch;
  uint8_t extraLeftCur, extraRightCur, extraLeftRight, extraBottomCur;
  float mode7PerspectiveLow, mode7PerspectiveHigh;

  // TMW / TSW etc
  uint8 screenEnabled[2];
  uint8 screenWindowed[2];
  uint8 mosaicEnabled;
  uint8 mosaicSize;
  // object/sprites
  uint16_t objTileAdr1;
  uint16_t objTileAdr2;
  uint8_t objSize;
  // Window
  uint8_t window1left;
  uint8_t window1right;
  uint8_t window2left;
  uint8_t window2right;
  uint32_t windowsel;

  // color math
  uint8_t clipMode;
  uint8_t preventMathMode;
  bool addSubscreen;
  bool subtractColor;
  bool halfColor;
  uint8 mathEnabled;
  uint8_t fixedColorR, fixedColorG, fixedColorB;
  // settings
  bool forcedBlank;
  uint8_t brightness;
  uint8_t mode;
  bool bg3Priority;  // ct-recomp: BGMODE bit 3, was hardcoded true for mode 1

  // vram access
  uint16_t vramPointer;
  uint16_t vramIncrement;
  bool vramIncrementOnHigh;
  // cgram access
  uint8_t cgramPointer;
  bool cgramSecondWrite;
  uint8_t cgramBuffer;
  // oam access
  uint16_t oamAdr;
  bool oamSecondWrite;
  uint8_t oamBuffer;

  // background layers
  BgLayer bgLayer[4];
  uint8_t scrollPrev;
  uint8_t scrollPrev2;
  
  // mode 7
  int16_t m7matrix[8]; // a, b, c, d, x, y, h, v
  uint8_t m7prev;
  bool m7largeField;
  bool m7charFill;
  bool m7xFlip;
  bool m7yFlip;
  bool m7extBg_always_zero;
  // mode 7 internal
  int32_t m7startX;
  int32_t m7startY;

  uint16_t oam[0x110];
  
  // store 31 extra entries to remove the need for clamp
  uint8_t brightnessMult[32 + 31];
  uint8_t brightnessMultHalf[32 * 2];
  uint16_t cgram[0x100];
  uint8_t mosaicModulo[kPpuXPixels];
  uint32_t colorMapRgb[256];
  PpuPixelPrioBufs bgBuffers[2];
  PpuPixelPrioBufs objBuffer;
  PpuPixelPrioBufs behindBuf;   // port: whole-line renderer's runner-ups
  // port: with provBuffer on, which sprite texel objBuffer holds per x:
  // bits 0-5 u, 6-11 v, 12-18 OAM index, 19 H flip, 20 V flip, 21-22
  // size (8 << n), 23 second name table, 24-31 name
  uint32_t objProv[kPpuXPixels];
  uint16_t vram[0x8000];
};

Ppu* ppu_init();
void ppu_free(Ppu* ppu);
void ppu_reset(Ppu* ppu);
void ppu_handleVblank(Ppu* ppu);
void ppu_runLine(Ppu* ppu, int line);
void ppu_drawTo(Ppu *ppu, int x);   // ct-recomp: draw the current line up to pixel x (exclusive)
uint8_t ppu_read(Ppu* ppu, uint8_t adr);
void ppu_write(Ppu* ppu, uint8_t adr, uint8_t val);
void ppu_saveload(Ppu *ppu, SaveLoadFunc *func, void *ctx);
void PpuBeginDrawing(Ppu *ppu, uint8_t *buffer, size_t pitch, uint32_t render_flags);
void PpuSetLayerBuffer(Ppu *ppu, uint8_t *buffer, size_t pitch);   // port
// port: see Ppu.behindColor; color NULL turns it off.
void PpuSetBehindBuffer(Ppu *ppu, uint8_t *color, size_t color_pitch,
                        uint8_t *layer, size_t layer_pitch);

// port: see Ppu.provBuffer; NULL turns it off. pitch in PpuProv units.
void PpuSetProvBuffer(Ppu *ppu, PpuProv *buffer, size_t pitch);

// Returns the current render scale, 1x = 256px, 2x=512px, 4x=1024px
int PpuGetCurrentRenderScale(Ppu *ppu, uint32_t render_flags);

void PpuSetMode7PerspectiveCorrection(Ppu *ppu, int low, int high);
void PpuSetExtraSideSpace(Ppu *ppu, int left, int right, int bottom);

#endif  // ZELDA3_SNES_PPU_H_
