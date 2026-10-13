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

// ═══ B1: FORMACIONES REALES CON MULTIPLICADORES (data-driven) ═══════════════
// Cada formacion define multiplicadores de frente/profundidad (aplicados en
// los slots via g_formPerRow/g_formSpacing/g_formRowSpacing; ver
// applyBattleFormation) y de combate (velocidad, flanqueo, daño frontal y
// de proyectil recibido). Tabla en data/battle/formations.txt; si el
// archivo falta o una clave es invalida se conservan los builtins
// (defineBuiltinFormations) con aviso archivo:linea (regla gamedata).
// D8 intacto: la aplicacion por batalla solo muta g_form* (NO g_settings).
#define FORMATION_COUNT 4
struct FormationTactic {
    char  name[10];          // LINE / COLUMN / SQUARE / LOOSE
    char  desc[28];          // hint corto para el overlay de preparacion
    float frontMult;         // frente: columnas = round(basePerRow * frontMult)
    float depthMult;         // profundidad: filas a colSpacing * depthMult
    float spacingMult;       // densidad lateral: colSpacing = base * spacingMult
    float speedMult;         // velocidad de movimiento del bando con la formacion
    float flankVuln;         // daño recibido en flanco/rear (multiplicador)
    float frontTakenMult;    // daño frontal recibido
    float projTakenMult;     // daño de proyectil recibido
};
// Builtins golden (combat.cpp): LINE +25% flanqueo, COLUMN +50% flanqueo y
// +20% profundidad, SQUARE -15% daño frontal y -15% velocidad, LOOSE -20%
// proyectiles. Balance documentado en data/battle/formations.txt.
void defineBuiltinFormations(FormationTactic out[FORMATION_COUNT]);
// Tabla activa: builtins al arrancar; loadFormationData() la sustituye por
// data/battle/formations.txt si el archivo es valido.
FormationTactic& formationTactic(int idx); // idx invalido -> slot neutro (1.0)
void setFormationTactics(const FormationTactic in[FORMATION_COUNT]);
// Aplica la formacion idx a ESTA batalla: deriva g_formPerRow/g_formSpacing/
// g_formRowSpacing desde g_settings (solo lectura, D8) y marca la formacion
// como activa para el jugador. Sin aplicar => multiplicadores 1.0 (actual).
void applyBattleFormation(int idx);
extern bool g_formationApplied; // true tras applyBattleFormation (initBattle la limpia)
// Helpers de multiplicadores para un bando: solo el JUGADOR con formacion
// aplicada recibe modificadores; el enemigo (sin formacion) queda en 1.0.
bool   formationActiveFor(const BattleUnit& bu);
float  unitFormationSpeedMult(const BattleUnit& bu);
float  unitFormationFrontTakenMult(const BattleUnit& bu);
float  unitFormationProjTakenMult(const BattleUnit& bu);
// Daño de flanco: 1.0 en el arco frontal (+-60 grados del rumbo del
// defensor); rampa lineal hasta flankVuln a 180 grados (ataque por la
// espalda). defFacingDeg en grados (dirToAngle).
float  flankDmgMult(int formIdx,float defFacingDeg,Vector2 attPos,Vector2 defPos);
float  unitFlankDmgMult(const BattleUnit& def,float defFacingDeg,
                        Vector2 attPos,Vector2 defPos);

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
