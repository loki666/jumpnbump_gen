#include <genesis.h>
#include <libres.h>
#include "resources.h"

#include "bank.h"
#include "global.h"
#include "menu.h"
#include "game.h"
#include "rabbits.h"
#include "objects.h"
#include "sfx.h"

#define GAME_STATE_BOOT 0
#define GAME_STATE_MENU 1
#define GAME_STATE_GAME 2
#define GAME_STATE_END  3

static int game_state = GAME_STATE_BOOT;

int ai[JNB_MAX_PLAYERS];

//Index of the last tile that has been placed, useful to avoid overlapping
u16 VDPTilesFilled = TILE_USER_INDEX;
u16 palette[64];
player_anim_t player_anims[7];
player_t player[JNB_MAX_PLAYERS];

int endscore_reached;

// SAT pointer
VDPSprite* vdpSprite;
// VDP sprite index (for link field)
u8 vdpSpriteInd;

const u16 (*ban_map)[32];

static void initProgram() {
    int player_anim_data[] = {
        1, 0, 0, 0x7fff, 0, 0, 0, 0, 0, 0,
        4, 0, 0, 4, 1, 4, 2, 4, 3, 4,
        1, 0, 4, 0x7fff, 0, 0, 0, 0, 0, 0,
        4, 2, 5, 8, 6, 10, 7, 3, 6, 3,
        1, 0, 6, 0x7fff, 0, 0, 0, 0, 0, 0,
        2, 1, 5, 8, 4, 0x7fff, 0, 0, 0, 0,
        1, 0, 8, 5, 0, 0, 0, 0, 0, 0
    };

    for (u32 c1 = 0; c1 < 7; c1++) {
        player_anims[c1].num_frames = player_anim_data[c1 * 10];
        player_anims[c1].restart_frame = player_anim_data[c1 * 10 + 1];
        for (u32 c2 = 0; c2 < 4; c2++) {
            player_anims[c1].frame[c2].image = player_anim_data[c1 * 10 + c2 * 2 + 2];
            player_anims[c1].frame[c2].ticks = player_anim_data[c1 * 10 + c2 * 2 + 3];
        }
    }

    for(u32 c1 = 0; c1 < JNB_MAX_PLAYERS; c1++)		// reset player values
    {
        ai[c1] = 0;
    }

    init_objects();
    initKeyb();
}

static void handleInput();
static void displayLogo();
static void displaySgdkLogo();

int main(bool resetType) {
    //Soft resets don't clear RAM, this can bring some bugs so we hard reset every time we detect a soft reset
    if (!resetType)
        SYS_hardReset();

    // initialization
    VDP_setScreenWidth320();

    if (JOY_getPortType(PORT_1) == PORT_TYPE_TEAMPLAYER) {
        JOY_setSupport(PORT_1, JOY_SUPPORT_TEAMPLAYER);
    }
    if (JOY_getPortType(PORT_2) == PORT_TYPE_TEAMPLAYER) {
        JOY_setSupport(PORT_2, JOY_SUPPORT_TEAMPLAYER);
    }

    displayLogo();
    displaySgdkLogo();

    // set all palette to black
    PAL_setColors(0, (u16*) &palette_black[0], 64, DMA);
    memcpy(&palette[0], &palette_black[0], 64 * 2);

    // init SFX
    SFX_init();

    initProgram();

    DMA_setBufferSize(10000);
    DMA_setMaxTransferSize(10000);

    loadRabbits();
    load_objects_sprites();

    SYS_doVBlankProcess();

    // can restore default DMA buffer size
    DMA_setBufferSizeToDefault();
    DMA_setMaxTransferSizeToDefault();

    VDP_setTextPalette(PAL1);

    while(TRUE)
    {
        vdpSprite = vdpSpriteCache;
        vdpSpriteInd = 1;

        handleInput();

        if (game_state == GAME_STATE_BOOT) {
            loadMenu();
            initMenu();
            PAL_fadeInAll(palette, 30, TRUE);

            // start music
            if (SYS_isNTSC()) {
                XGM_startPlay(jump_music_ntsc);
            } else {
                XGM_startPlay(jump_music_pal);
            }

            game_state = GAME_STATE_MENU;
        } else if (game_state == GAME_STATE_MENU) {
            u32 ret = menuFrame();

            if (ret == 1) {
                menuFrame();
                PAL_fadeOutAll(30, FALSE);
                unloadMenu();
                XGM_stopPlay();

                loadLevel();
                initLevel();
                PAL_fadeInAll(palette, 30, TRUE);
                endscore_reached = 0;

                game_state = GAME_STATE_GAME;
            }
        } else if (game_state == GAME_STATE_GAME) {
            gameFrame();
        }

        // remove 1 to get number of hard sprite used
        vdpSpriteInd--;
        if (vdpSpriteInd > 0) {
            // get back to last sprite
            vdpSprite--;
            // mark as end
            vdpSprite->link = 0;
            // send sprites to VRAM using DMA queue
            DMA_queueDmaFast(DMA_VRAM, vdpSpriteCache, VDP_SPRITE_TABLE, vdpSpriteInd * (sizeof(VDPSprite) / 2), 2);
        } else {
            // set 1st sprite off screen and mark as end
            vdpSprite->y = 0;
            vdpSprite->link = 0;
            // send sprites to VRAM using DMA queue
            DMA_queueDmaFast(DMA_VRAM, vdpSpriteCache, VDP_SPRITE_TABLE, 1 * (sizeof(VDPSprite) / 2), 2);
        }
        
        // sync frame and do vblank process
        SYS_doVBlankProcess();
    }

    return 0;
}

static void handleInput() {
    if (JOY_getPortType(PORT_1) == PORT_TYPE_TEAMPLAYER) {
        if (!ai[0]) {
            u16 value = JOY_readJoypad(JOY_1);
            addkey(((KEY_PL1_JUMP) & 0x7fff) | ((value & BUTTON_A) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_LEFT) & 0x7fff) | ((value & BUTTON_LEFT) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_RIGHT) & 0x7fff) | ((value & BUTTON_RIGHT) ? 0x0 : 0x8000));
        }
        if (!ai[1]) {
            u16 value = JOY_readJoypad(JOY_3);
            addkey(((KEY_PL1_JUMP + 0x10) & 0x7fff) | ((value & BUTTON_A) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_LEFT + 0x10) & 0x7fff) | ((value & BUTTON_LEFT) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_RIGHT + 0x10) & 0x7fff) | ((value & BUTTON_RIGHT) ? 0x0 : 0x8000));
        }
        if (!ai[2]) {
            u16 value = JOY_readJoypad(JOY_4);
            addkey(((KEY_PL1_JUMP + 0x20) & 0x7fff) | ((value & BUTTON_A) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_LEFT + 0x20) & 0x7fff) | ((value & BUTTON_LEFT) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_RIGHT + 0x20) & 0x7fff) | ((value & BUTTON_RIGHT) ? 0x0 : 0x8000));
        }
        if (!ai[2]) {
            u16 value = JOY_readJoypad(JOY_5);
            addkey(((KEY_PL1_JUMP + 0x30) & 0x7fff) | ((value & BUTTON_A) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_LEFT + 0x30) & 0x7fff) | ((value & BUTTON_LEFT) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_RIGHT + 0x30) & 0x7fff) | ((value & BUTTON_RIGHT) ? 0x0 : 0x8000));
        }
    } else if (JOY_getPortType(PORT_2) == PORT_TYPE_TEAMPLAYER) {
        if (!ai[0]) {
            u16 value = JOY_readJoypad(JOY_2);
            addkey(((KEY_PL1_JUMP) & 0x7fff) | ((value & BUTTON_A) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_LEFT) & 0x7fff) | ((value & BUTTON_LEFT) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_RIGHT) & 0x7fff) | ((value & BUTTON_RIGHT) ? 0x0 : 0x8000));
        }
        if (!ai[1]) {
            u16 value = JOY_readJoypad(JOY_6);
            addkey(((KEY_PL1_JUMP + 0x10) & 0x7fff) | ((value & BUTTON_A) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_LEFT + 0x10) & 0x7fff) | ((value & BUTTON_LEFT) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_RIGHT + 0x10) & 0x7fff) | ((value & BUTTON_RIGHT) ? 0x0 : 0x8000));
        }
        if (!ai[2]) {
            u16 value = JOY_readJoypad(JOY_7);
            addkey(((KEY_PL1_JUMP + 0x20) & 0x7fff) | ((value & BUTTON_A) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_LEFT + 0x20) & 0x7fff) | ((value & BUTTON_LEFT) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_RIGHT + 0x20) & 0x7fff) | ((value & BUTTON_RIGHT) ? 0x0 : 0x8000));
        }
        if (!ai[2]) {
            u16 value = JOY_readJoypad(JOY_8);
            addkey(((KEY_PL1_JUMP + 0x30) & 0x7fff) | ((value & BUTTON_A) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_LEFT + 0x30) & 0x7fff) | ((value & BUTTON_LEFT) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_RIGHT + 0x30) & 0x7fff) | ((value & BUTTON_RIGHT) ? 0x0 : 0x8000));
        }
    } else {
        if (!ai[0]) {
            u16 value = JOY_readJoypad(JOY_1);
            addkey(((KEY_PL1_JUMP) & 0x7fff) | ((value & BUTTON_A) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_LEFT) & 0x7fff) | ((value & BUTTON_LEFT) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_RIGHT) & 0x7fff) | ((value & BUTTON_RIGHT) ? 0x0 : 0x8000));
        }
        if (!ai[1]) {
            u16 value = JOY_readJoypad(JOY_2);
            addkey(((KEY_PL1_JUMP + 0x10) & 0x7fff) | ((value & BUTTON_A) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_LEFT + 0x10) & 0x7fff) | ((value & BUTTON_LEFT) ? 0x0 : 0x8000));
            addkey(((KEY_PL1_RIGHT + 0x10) & 0x7fff) | ((value & BUTTON_RIGHT) ? 0x0 : 0x8000));
        }
    }
}

static void displaySgdkLogo() {
    VDP_clearPlane(BG_B, TRUE);

    PAL_setColors(0, (u16*) palette_black, 64, DMA);
    memcpy(&palette[0], sgdk_logo.palette->data, sgdk_logo.palette->length * 2);

    VDP_drawBitmapEx(BG_B, &sgdk_logo, TILE_ATTR(PAL0, FALSE, FALSE, FALSE), 15, 9, FALSE);

    // can restore default DMA buffer size
    DMA_setBufferSizeToDefault();
    DMA_setMaxTransferSizeToDefault();

    PAL_fadeIn(0, 63, palette, 30, FALSE);
    JOY_waitPressTime(JOY_ALL, BUTTON_ALL, 5000);
    PAL_fadeOut(0, 63, 30, FALSE);

    VDP_clearPlane(BG_B, TRUE);
}

static void displayLogo() {
    VDP_clearPlane(BG_B, TRUE);

    PAL_setColors(0, (u16*) palette_black, 64, DMA);
    memcpy(&palette[0], image_logo.palette->data, 64 * 2);

    VDP_loadTileSet(image_logo.tileset, VDPTilesFilled, DMA);
    VDP_setTileMapEx(BG_B, image_logo.tilemap, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, VDPTilesFilled), 11, 6, 0, 0, 18, 16, DMA);

    PAL_fadeIn(0, 63, palette, 30, FALSE);
    JOY_waitPressTime(JOY_ALL, BUTTON_ALL, 5000);
    PAL_fadeOut(0, 63, 30, FALSE);

    VDP_clearPlane(BG_B, TRUE);
}

u16 rnd(const u16 max) {
    u32 op1 = random();
    return modu(op1, max);
}
