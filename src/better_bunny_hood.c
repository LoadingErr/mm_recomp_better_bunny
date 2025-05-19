#include "modding.h"
#include "global.h"
#include "recomputils.h"
#include "recompconfig.h"
#include "z64player.h"
#include "z64inventory.h"
#include "z64play.h"
#include "z64animation.h"
#include "gfx.h"
#include "controller.h"
#include "kaleido_manager.h"
#include "z64item.h"

typedef enum PauseEquipCButton {
    /* 0 */ PAUSE_EQUIP_C_LEFT,
    /* 1 */ PAUSE_EQUIP_C_DOWN,
    /* 2 */ PAUSE_EQUIP_C_RIGHT
} PauseEquipCButton;

u16 sPlayerItemButtons[] = {
    BTN_B,
    BTN_CLEFT,
    BTN_CDOWN,
    BTN_CRIGHT,
};

typedef struct BunnyEarKinematics {
    /* 0x0 */ Vec3s rot;
    /* 0x6 */ Vec3s angVel;
} BunnyEarKinematics; // size = 0xC

BunnyEarKinematics sBunnyEarKinematics;

typedef enum ActionHandlerIndex {
    /* 0x0 */ PLAYER_ACTION_HANDLER_0,
    /* 0x1 */ PLAYER_ACTION_HANDLER_1,
    /* 0x2 */ PLAYER_ACTION_HANDLER_2,
    /* 0x3 */ PLAYER_ACTION_HANDLER_3,
    /* 0x4 */ PLAYER_ACTION_HANDLER_TALK,
    /* 0x5 */ PLAYER_ACTION_HANDLER_5,
    /* 0x6 */ PLAYER_ACTION_HANDLER_6,
    /* 0x7 */ PLAYER_ACTION_HANDLER_7,
    /* 0x8 */ PLAYER_ACTION_HANDLER_8,
    /* 0x9 */ PLAYER_ACTION_HANDLER_9,
    /* 0xA */ PLAYER_ACTION_HANDLER_10,
    /* 0xB */ PLAYER_ACTION_HANDLER_11,
    /* 0xC */ PLAYER_ACTION_HANDLER_12,
    /* 0xD */ PLAYER_ACTION_HANDLER_13,
    /* 0xE */ PLAYER_ACTION_HANDLER_14,
    /* 0xF */ PLAYER_ACTION_HANDLER_MAX
} ActionHandlerIndex;

s8 sActionHandlerList8[] = {
    /*  0 */ PLAYER_ACTION_HANDLER_0,
    /*  1 */ PLAYER_ACTION_HANDLER_11,
    /*  2 */ PLAYER_ACTION_HANDLER_1,
    /*  3 */ PLAYER_ACTION_HANDLER_2,
    /*  4 */ PLAYER_ACTION_HANDLER_3,
    /*  5 */ PLAYER_ACTION_HANDLER_12,
    /*  6 */ PLAYER_ACTION_HANDLER_5,
    /*  7 */ PLAYER_ACTION_HANDLER_TALK,
    /*  8 */ PLAYER_ACTION_HANDLER_9,
    /*  9 */ PLAYER_ACTION_HANDLER_8,
    /* 10 */ PLAYER_ACTION_HANDLER_7,
    /* 11 */ -PLAYER_ACTION_HANDLER_6,
};

#define SPEED_MODE_CURVED 0.018f
#define gSPDisplayList(pkt,dl)  gDma1p(pkt, G_DL, dl, 0, G_DL_PUSH)

void Interface_UpdateButtonAlphasByStatus(PlayState* play, s16 risingAlpha);
s32 Player_GetMovementSpeedAndYaw(Player* this, f32* outSpeedTarget, s16* outYawTarget, f32 speedMode, PlayState* play);
s32 Player_TryActionHandlerList(PlayState* play, Player* this, s8* actionHandlerList, s32 updateUpperBody);
s32 func_8083A4A4(Player* this, f32* speedTarget, s16* yawTarget, f32 decelerationRate);
bool Player_IsZTargetingWithHostileUpdate(Player* this);
void func_8083CB58(Player* this, f32 arg1, s16 arg2);
void func_8083F57C(Player* this, PlayState* play);
void func_8083A794(Player* this, PlayState* play);
void func_8083C8E8(Player* this, PlayState* play);
void func_80839E3C(Player* this, PlayState* play);
void KaleidoScope_DrawItemSelect(PlayState* play);
void Player_UpdateBunnyEars(Player* player);
void Player_DrawBunnyHood(PlayState* play);
void Audio_PlaySfx(u16 sfxId);

RECOMP_IMPORT("ProxyMM_ObjDepLoader", bool ObjDepLoader_Load(PlayState* play, u8 segment, s16 objectId));
RECOMP_IMPORT("ProxyMM_ObjDepLoader", void ObjDepLoader_Unload(PlayState* play, u8 segment, s16 objectId));

extern Gfx* D_801C0B20[];
extern u8 gEquippedItemOutlineTex[];

bool BunnyHoodMode_IsEnabled = false;

// Prevent the Bunny Hood from being equipped to a C-button if it's toggled on already
void PreventHoodEquip(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;

    if (CHECK_BTN_ALL(CONTROLLER1(&play->state)->press.button, BTN_CLEFT)) {
        if ((BunnyHoodMode_IsEnabled) &&
            (pauseCtx->pageIndex == PAUSE_MASK) &&
             pauseCtx->cursorSlot[PAUSE_MASK] == 8) {
            Audio_PlaySfx(NA_SE_SY_ERROR);
            CONTROLLER1(&play->state)->press.button &= ~BTN_CLEFT;
            return;
        }
        pauseCtx->equipTargetCBtn = PAUSE_EQUIP_C_LEFT;
    } else if (CHECK_BTN_ALL(CONTROLLER1(&play->state)->press.button, BTN_CDOWN)) {
        if ((BunnyHoodMode_IsEnabled) &&
            (pauseCtx->pageIndex == PAUSE_MASK) &&
             pauseCtx->cursorSlot[PAUSE_MASK] == 8) {
            Audio_PlaySfx(NA_SE_SY_ERROR);
            CONTROLLER1(&play->state)->press.button &= ~BTN_CDOWN;
            return;
        }
        pauseCtx->equipTargetCBtn = PAUSE_EQUIP_C_DOWN;
    } else if (CHECK_BTN_ALL(CONTROLLER1(&play->state)->press.button, BTN_CRIGHT)) {
        if ((BunnyHoodMode_IsEnabled) &&
            (pauseCtx->pageIndex == PAUSE_MASK) &&
             pauseCtx->cursorSlot[PAUSE_MASK] == 8) {
            Audio_PlaySfx(NA_SE_SY_ERROR);
            CONTROLLER1(&play->state)->press.button &= ~BTN_CRIGHT;
            return;
        }
        pauseCtx->equipTargetCBtn = PAUSE_EQUIP_C_RIGHT;
    }
}

#define CHECK_ITEM_IS_BUNNY(item) (item == ITEM_MASK_BUNNY)

void DisableHoodIfBunnyModeEnabled(PlayState* play, Input* input) {
    EquipSlot bunnyButton = EQUIP_SLOT_NONE;

    if (BunnyHoodMode_IsEnabled) {
        for (EquipSlot i = EQUIP_SLOT_C_LEFT; i <= EQUIP_SLOT_C_RIGHT; i++) {
            u8 equippedItem = gSaveContext.save.saveInfo.equips.buttonItems[0][i];
            if (CHECK_ITEM_IS_BUNNY(equippedItem)) {
                bunnyButton = i;
                break;
            }
        }

        if (bunnyButton == EQUIP_SLOT_NONE) {
            return;
        }

        if (CHECK_BTN_ALL(input->press.button, sPlayerItemButtons[bunnyButton])) {
            Audio_PlaySfx(NA_SE_SY_ERROR);
        }

        if (gSaveContext.save.saveInfo.equips.buttonItems[0][bunnyButton]) {
            gSaveContext.buttonStatus[bunnyButton] = BTN_DISABLED;
            gSaveContext.hudVisibility = HUD_VISIBILITY_IDLE;
        }
    }
}

// Equip/unequip Bunny Hood and play equip/cancel sfx on A press
void PreKaleidoHandler(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;

    if (CHECK_BTN_ALL(CONTROLLER1(&play->state)->press.button, BTN_A)) {

        switch(pauseCtx->pageIndex) {
            case PAUSE_MASK: {
                switch (pauseCtx->cursorSlot[PAUSE_MASK]) {
                    case 8: {
                        if (!BunnyHoodMode_IsEnabled) {
                            BunnyHoodMode_IsEnabled = true;
                            Audio_PlaySfx(NA_SE_SY_DECIDE);

                            // PauseContext* pauseCtx = &play->pauseCtx;
                            // u16 i;
                            // u16 j;
                        
                            // OPEN_DISPS(play->state.gfxCtx);
                        
                            // Gfx_SetupDL42_Opa(play->state.gfxCtx);
                        
                            // gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
                            // gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
                            // for (i = 0, j = ITEM_NUM_SLOTS * 4; i < 3; i++, j += 4) {
                            //     if (GET_CUR_FORM_BTN_ITEM(i + 1) != ITEM_NONE) {
                            //         if (GET_CUR_FORM_BTN_SLOT(i + 1) < ITEM_NUM_SLOTS) {
                            //             gSPVertex(POLY_OPA_DISP++, &pauseCtx->maskVtx[104], 4, 0);
                            //             POLY_OPA_DISP = Gfx_DrawTexQuadIA8(POLY_OPA_DISP, gEquippedItemOutlineTex, 32, 32, 0);
                            //         }
                            //     }
                            // }

                            // CLOSE_DISPS(play->state.gfxCtx);

                            CONTROLLER1(&play->state)->press.button &= ~BTN_A;
                        } else {
                            BunnyHoodMode_IsEnabled = false;
                            Audio_PlaySfx(NA_SE_SY_CANCEL);
                            CONTROLLER1(&play->state)->press.button &= ~BTN_A;
                        }
                    } break;
                }
            } break;
        }
    }
}

RECOMP_HOOK("KaleidoScope_UpdateItemCursor")
void PreKaleidoScope_UpdateItemCursor(PlayState* play) {
    PreKaleidoHandler(play);
}

RECOMP_HOOK("KaleidoScope_UpdateMaskCursor")
void PreKaleidoScope_UpdateMaskCursor(PlayState* play) {
    PreKaleidoHandler(play);
    PreventHoodEquip(play);
}

RECOMP_PATCH void Player_Action_13(Player* this, PlayState* play) {
    f32 speedTarget;
    s16 yawTarget;

    this->stateFlags2 |= PLAYER_STATE2_20;
    func_8083F57C(this, play);
    if (Player_TryActionHandlerList(play, this, sActionHandlerList8, true)) {
        return;
    }

    if (Player_IsZTargetingWithHostileUpdate(this)) {
        func_8083A794(this, play);
        return;
    }

    Player_GetMovementSpeedAndYaw(this, &speedTarget, &yawTarget, SPEED_MODE_CURVED, play);

    if (BunnyHoodMode_IsEnabled) {
        if (INV_CONTENT(ITEM_MASK_BUNNY) == ITEM_MASK_BUNNY) {
            speedTarget *= 1.5f;
        }
    } else if (Player_GetCurMaskItemId(play) == ITEM_MASK_BUNNY) {
        speedTarget *= 1.5f;
    }

    if (!func_8083A4A4(this, &speedTarget, &yawTarget, R_DECELERATE_RATE / 100.0f)) {
        func_8083CB58(this, speedTarget, yawTarget);
        func_8083C8E8(this, play);
        if ((this->speedXZ == 0.0f) && (speedTarget == 0.0f)) {
            func_80839E3C(this, play);
        }
    }
}

RECOMP_HOOK("Player_PostLimbDrawGameplay")
void On_Player_PostLimbDrawGameplay(PlayState* play, s32 limbIndex, Gfx** dList1, Gfx** dList2, Vec3s* rot, Actor* actor) {
    Player* player = GET_PLAYER(play);
    if (!(player->stateFlags1 & PLAYER_STATE1_100000) && 
    (INV_CONTENT(ITEM_MASK_BUNNY) == ITEM_MASK_BUNNY) && 
    (BunnyHoodMode_IsEnabled) && 
    ((player->currentMask == PLAYER_MASK_NONE) || (player->currentMask == PLAYER_MASK_DEKU) || (player->currentMask == PLAYER_MASK_GORON) || (player->currentMask == PLAYER_MASK_ZORA))) {
        if (limbIndex == PLAYER_LIMB_HEAD) {
            OPEN_DISPS(play->state.gfxCtx);

            // Set back geometry modes left over from player head DL, incase another mask changed the values
            gSPLoadGeometryMode(POLY_OPA_DISP++,
                                G_ZBUFFER | G_SHADE | G_CULL_BACK | G_FOG | G_LIGHTING | G_SHADING_SMOOTH);

            Matrix_Push();
            Player_DrawBunnyHood(play);

            Player_UpdateBunnyEars((Player*)actor);

            ObjDepLoader_Load(play, 0x0A, OBJECT_MASK_RABIT);
            gSPDisplayList(POLY_OPA_DISP++,
                            (Gfx*)D_801C0B20[PLAYER_MASK_BUNNY - 1]); // D_801C0B20 is an array of mask DLs
            ObjDepLoader_Unload(play, 0x0A, OBJECT_MASK_RABIT);

            Matrix_Pop();

            CLOSE_DISPS(play->state.gfxCtx);
        }
    }
}

RECOMP_HOOK("Player_UpdateCommon") void DisableBunny(Player* this, PlayState* play, Input* input) {
    DisableHoodIfBunnyModeEnabled(play, input);
}