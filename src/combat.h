// src/combat.h — Lógica de combate
#pragma once

#include "config.h"
#include "elements.h"
#include <vector>
#include <string>
#include <deque>

// (:610-643)
struct BattleState {
    std::vector<BattleUnit>  playerUnits;
    std::vector<BattleUnit>  enemyUnits;
    std::vector<Projectile>  projs;
    std::vector<DeadMarker>  dead;
    // Camera
    Camera2D  cam;
    float     camZoom;
    // Battlefield terrain obstacles (AABB)
    std::vector<Rectangle>   obstacles;
    // Terrain type (affects generated obstacles)
    TerrainType terrain;
    // Time control
    float     timeScale;     // 1.0 or 2.0
    bool      paused;
    // Selection
    bool      dragging;
    Vector2   selStart;
    Rectangle selRect;
    // Province index being fought over
    int       battleProvince;
    bool      isDefense;
    // Result
    bool      battleOver;
    bool      playerWon;
    float     resultTimer;
    // Loot
    float     lootGold;
    // Scenario name
    char      scenarioName[64];
    // Control groups (Ctrl+1..9 / 1..9)
    ControlGroup controlGroups[9];
    int          activeControlGroup;  // -1 = none; set when user presses 1-9
    // v11: fase de preparación pre-batalla (formaciones + tiempo configurable)
    bool         prepPhase;           // true = aún en preparación (movimiento bloqueado)
    float        prepTimer;           // segundos restantes
    int          prepFormation;       // 0=line,1=column,2=square,3=loose
};

// (:650-660)
struct BattleResult {
    bool  playerWon;
    int   playerLosses;
    int   enemyLosses;
    float lootGold;
    int   provinceIdx;
    bool  isDefense;
    bool  lootApplied;   // 0.2: was static local, now per-result field
    bool  autoresolved;  // Fase D: resuelto via AUTO-RESOLVE (sin simulacion)
    // Survivor counts per unit type
    std::vector<std::pair<int,int>> survivors; // {typeIdx, survivors}
};

extern BattleState  g_battle;      // (:645)
extern BattleResult g_lastResult;  // (:661)

// (:757-760)
extern std::deque<std::string> g_battleLog;
void battleLogAdd(const std::string& s);

// Fase D: predicción de batalla estilo RISK (definiciones en combat.cpp)
float predictBattle(const std::vector<std::pair<int,int>>& playerGroups,
                    const std::vector<std::pair<int,int>>& enemyGroups,
                    TerrainType terrain, bool playerDefends, float defBonus);
void autoResolveBattle(const std::vector<std::pair<int,int>>& playerGroups,
                       const std::vector<std::pair<int,int>>& enemyGroups,
                       TerrainType terrain, int provinceIdx, bool isDefense,
                       float defBonus);

// Funciones de combate (definiciones en combat.cpp)
// D2: playerVet/enemyVet = veteranía inicial (0..3) de cada bando (Range+Stable)
void initBattle(const std::vector<std::pair<int,int>>& playerGroups,
                const std::vector<std::pair<int,int>>& enemyGroups,
                TerrainType terrain,int provinceIdx,bool isDefense,
                const char* scenarioName,int playerVet=0,int enemyVet=0); // (:1446)
float soldierMeleeRadius(const UnitTypeDef& td);                                // (:1567)
float calcMeleeDmg(const UnitTypeDef& attTd,const UnitTypeDef& defTd,bool chargeBonus); // (:1571)
float calcMissileDmg(const UnitTypeDef& attTd,const UnitTypeDef& defTd);        // (:1581)
Vector2 avoidObstacles(Vector2 pos, Vector2 targetDir, float speed, const std::vector<Rectangle>& obstacles); // (:1588)
bool losClean(Vector2 from,Vector2 to,const std::vector<Soldier>& friendlies,int selfIdx); // (:1610)
bool separateSoldiers(std::vector<BattleUnit>& units);                                // (:1640)
bool separateContact(std::vector<BattleUnit>& a,std::vector<BattleUnit>& b);          // (:1640)
void separateAll();          // alterna mismo-bando + cruzado hasta converger
extern bool g_debugSoldiers;                 // F12: overlay pos+orientacion de soldados
void drawSoldierDebug();
std::pair<int,int> findNearestEnemySoldier(Vector2 from,
        const std::vector<BattleUnit>& enemies);                                // (:1665)
void updateBattleUnits(std::vector<BattleUnit>& myUnits,
                       std::vector<BattleUnit>& foeUnits,
                       float dt);                                               // (:1678)
void updateEnemyAI(float dt);                                                   // (:2041)
void drawBattlefield();                                                         // (:2143)
void drawAllUnits();                                                            // (:2189)
void drawBattleHUD(Vector2 mouse);                                              // (:2328)
