// src/config.h — Configuración global del juego
#pragma once

#include <string>
#include <vector>
#include <cmath>
#include "raylib.h"

// === PANTALLA ===
// OJO: son variables, NO constantes. El código las reasigna cada frame
// (rts_game.cpp:4694, :4730-4731) y al redimensionar (:3811).
inline int SCREEN_W = 1280;
inline int SCREEN_H = 768;
inline const int HUD_H     = 96;    // HUD inferior en batalla   (:40)
inline const int TOPBAR_H  = 32;    // Barra superior en batalla (:41)
inline const int BATTLE_W  = 2560;  // Tamaño del campo de batalla (:42)
inline const int BATTLE_H  = 1536;  //                             (:43)

// === PALETA (nombres originales del código) ===
inline const Color C_BG        = {14,12,10,255};
inline const Color C_GOLD      = {200,165,80,255};
inline const Color C_COPPER    = {160,80,40,255};
inline const Color C_PARCHMENT = {230,220,200,255};
inline const Color C_SECONDARY = {140,130,110,255};
inline const Color C_BLOOD     = {160,30,20,255};
inline const Color C_ALLY      = {80,160,220,255};
inline const Color C_ENEMY_COL = {220,60,50,255};
inline const Color C_TERRAIN   = {42,54,32,255};

// === ESCALA DE UI (7.0) — usada por ui.h (uiFS/uiPx) ===
inline float g_uiScaleDraw = 1.f;
inline int   uiFS(int fs){ return (int)roundf((float)fs*g_uiScaleDraw); }
inline float uiPx(float px){ return px*g_uiScaleDraw; }

// === GAME STATES (:110-125) ===
enum GameState {
    STATE_MAIN_MENU=0,
    STATE_CAMPAIGN_MAP,
    STATE_CITY_MANAGEMENT,
    STATE_RECRUITMENT,
    STATE_PRE_BATTLE,
    STATE_BATTLE,
    STATE_BATTLE_RESULT,
    STATE_UNIT_CODEX,
    STATE_SETTINGS,
    STATE_UNIT_EDITOR,    // sandbox only from main menu
    STATE_QUICK_BATTLE_SETUP,
    STATE_VICTORY,
    STATE_DEFEAT,
    STATE_MARKETPLACE,     // 6.5: resource trading
    STATE_DIPLOMACY,       // Fase I: alianzas + trueque con facciones IA
    STATE_MULTIPLAYER      // Fase G: lobby de red (host/join, sin sync aun)
};

// Some screens (unit lists, dense tables) have fixed row heights. To avoid
// overlapping at very high UI scales, we cap the effective draw-scale per state.
inline float effectiveUiScale(GameState st, float requested){
    float maxS=2.25f;
    switch(st){
        // Dense list / table screens
        case STATE_PRE_BATTLE:
        case STATE_CITY_MANAGEMENT:
        case STATE_RECRUITMENT:
        case STATE_MARKETPLACE:
        case STATE_DIPLOMACY:
        case STATE_UNIT_CODEX:
            maxS=2.0f; break;
        // Combat/campaign HUD mixes world + UI; keep it readable but safe
        case STATE_BATTLE:
        case STATE_CAMPAIGN_MAP:
            maxS=2.0f; break;
        default:
            maxS=2.25f; break;
    }
    if(requested<1.f) requested=1.f;
    if(requested>maxS) requested=maxS;
    return requested;
}

// === OTROS ENUMS ===
enum SpriteBase { SPR_INFANTRY=0, SPR_CAVALRY, SPR_RANGED, SPR_BASE_COUNT };
enum BuildingType {
    BLD_MARKET=0, BLD_FARM, BLD_SAWMILL, BLD_QUARRY, BLD_SMITHY,
    BLD_BARRACKS, BLD_STABLE, BLD_RANGE, BLD_WORKSHOP,
    BLD_WALLS1, BLD_WALLS2, BLD_MAGETOWER, BLD_TEMPLE, BLD_PORT,
    BLD_COUNT
};
enum FactionId { FACTION_PLAYER=0, FACTION_AGGRESSIVE, FACTION_DEFENSIVE, FACTION_COMMERCIAL, FACTION_PIRATE, FACTION_NEUTRAL, FACTION_COUNT };
enum TerrainType { TERRAIN_PLAIN=0, TERRAIN_FOREST, TERRAIN_MOUNTAIN, TERRAIN_COAST };
enum SoldierState { SS_IDLE=0, SS_MOVING_SLOT, SS_MOVING_TARGET, SS_ATTACKING_MELEE, SS_ATTACKING_RANGED, SS_FLEEING };
enum UnitGroupState { UGS_IDLE=0, UGS_ADVANCING, UGS_ENGAGED, UGS_ROUTING };
enum TradeResource { TRADE_GOLD=0, TRADE_FOOD=1, TRADE_WOOD=2, TRADE_STONE=3, TRADE_IRON=4 };

// === RECURSOS (:423-425) — lo usa CampaignState ===
struct Resources {
    float gold=500.f, food=200.f, wood=100.f, stone=50.f, iron=50.f;
};

// === AJUSTES (:698-710) ===
inline const char* SETTINGS_FILE = "settings.ini";
struct GameSettings {
    int   screenW      = 1280;
    int   screenH      = 768;   // FASE 2: unificado (era 720; SCREEN_W/H usa 1280x768)
    bool  fullscreen   = false;
    float uiScale      = 1.f;  // 1.0 = 100%
    float masterVolume = 1.f;
    float musicVolume  = 0.7f;
    int   difficulty   = 1;   // 0=EASY,1=NORMAL,2=HARD
    bool  showFPS      = false;
    int   language     = 0;   // 0=EN,1=ES
    // Realismo de combate: formacion (afecta a calcFormationSlots y separacion)
    float formationSpacing = 26.f;  // px entre soldados (16..40)
    int   formationPerRow  = 5;     // soldados por fila en formacion (3..10)
};
extern GameSettings g_settings;
void saveSettings();
void loadSettings();

// === GUARDADO ===
inline const int SAVE_VERSION = 10;  // 10: PvP reserva por facción (clientReserve + RecruitEntry.faction)

// === ESTADO GLOBAL (:681-684) ===
extern GameState g_state;
extern bool      g_quitRequested;
extern bool      g_hasSave;
extern float     g_menuTime;
