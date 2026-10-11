// src/elements.h — Tipos de unidad, soldados, unidades de batalla, proyectiles
#pragma once

#include "config.h"
#include <vector>
#include <string>

// (:155)
inline const char* spriteBaseNames[SPR_BASE_COUNT]={"Infantry","Cavalry","Ranged"};

// (:157-190)
struct UnitTypeDef {
    char    name[32];
    int     soldierCount;   // total soldiers per group
    float   hpPerSoldier;
    int     armor;          // 0-40
    int     speed;          // px/s
    int     meleeAttack;
    int     meleeDefense;
    float   meleeBaseDmg;
    float   meleeAPDmg;
    float   meleeInterval;  // seconds between attacks
    int     range;          // 0 = melee only
    float   missileBaseDmg;
    float   missileAPDmg;
    float   missileReload;
    int     recruitGold;
    int     recruitFood;
    int     recruitIron;
    int     recruitTurns;
    int     maintGold;
    int     maintFood;
    // Morale base (0-100)
    float   morale;
    // Display
    unsigned char r,g,b;
    SpriteBase spriteBase;
    int     weaponHint;    // 0=spear,1=axe,2=poleaxe,3=hammer
    bool    isBuiltin;
    // Lore
    char    lore[256];
    // Building requirements (bitmask index into BuildingType enum)
    // encoded as bit flags for simplicity
    int     buildingReqs;  // bit field: 0=Barracks,1=Stable,2=Range,3=Smithy,4=Workshop
};

// (:192-195)
extern std::vector<UnitTypeDef> g_unitTypes;
extern std::vector<UnitTypeDef> g_vanillaUnitTypes;  // copy of builtin types at init (for Default button)
extern std::vector<Texture2D>   g_playerTextures;
extern std::vector<Texture2D>   g_enemyTextures;

int unitTypeCount();        // (:418)
void initBuiltinTypes();    // (:376)

// (:493-508)
struct Soldier {
    Vector2      pos;
    Vector2      formationSlot;
    float        hp;
    float        angle, angleTarget;
    float        meleeTimer, rangeTimer;
    bool         alive;
    bool         inMelee;
    int          attackTargetSoldier; // index in enemy BattleUnit's soldiers array
    int          attackTargetUnit;    // which enemy BattleUnit
    SoldierState state;
    float        chargeMoveTime;     // accumulated time moving at full speed
    bool         chargeReady;
    float        targetRefreshTimer; // 2.3: cache nearest enemy, refresh every 0.3s
    int          kills;              // 6.2: for veterancy
};

// (:515-533)
struct BattleUnit {
    int       typeIdx;
    bool      isPlayer;
    bool      selected;
    Vector2   anchorPos;
    Vector2   orderTarget;
    int       orderAttack;     // index into enemy BattleUnit list (-1=none)
    bool      hasExplicitOrder;
    UnitGroupState groupState;
    float     morale;
    float     moraleTimer;     // timer since last damage for recovery
    float     routeTimer;      // seconds left in routing
    std::vector<Soldier> soldiers;
    int       veterancy;       // 6.2: 0=Recruit,1=Seasoned,2=Veteran,3=Elite
    float     aiReactTimer;    // 3.2: AI recalculates orders every 1.5s
    int       orderType;       // 6.6: 0=attack,1=move,2=hold,3=flank
    float     formationFacing; // 6.6: rotation angle for formation
    float     lastFormationFacing; // v7.2: track last facing to detect changes
};

// (:538-543)
struct Projectile {
    Vector2 pos, vel;
    float   dmg;
    bool    alive, fromPlayer;
    bool    isCrossbow; // affects color/speed
};

// (:548-553)
struct DeadMarker {
    Vector2 pos;
    float   alpha;
    float   timer;
    bool    isCavalry;
    float   angle;      // facing al morir (hoja de muerte iso, Fase 2)
    int     typeIdx;    // tipo de unidad muerta; -1 = sin hoja
    int     team;       // 0=aliado, 1=enemigo
};

// (:601-605)
struct ControlGroup {
    std::vector<int>     unitIndices;  // indices into playerUnits
    std::vector<Vector2> relOffsets;   // (anchorPos - groupCentroid) at save time
    bool                 active=false;
};

// (:1002)
void drawStatBars(float x,float y,float w,float h,const UnitTypeDef& td);
// (:1404)
std::vector<Vector2> calcFormationSlots(Vector2 anchor,int count,float facing);
void formationDims(int count,int* cols,int* rows);   // columnas/filas segun g_formPerRow

// D8: parametros de formacion POR BATALLA (no mutan g_settings). initBattle
// los copia desde g_settings (defaults del usuario); el selector pre-batalla
// solo toca estos valores => no se persisten en settings.ini ni contaminan
// la siguiente batalla.
extern int   g_formPerRow;   // soldados por fila (3..10)
extern float g_formSpacing;  // px entre soldados (16..40)
