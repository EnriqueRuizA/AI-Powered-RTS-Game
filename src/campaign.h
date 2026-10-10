// src/campaign.h — Mapa de campaña, provincias, turnos
#pragma once

#include "config.h"
#include <string>
#include <vector>
#include <utility>

// (:467, :470-473)
inline const char* terrainNames[4]={"Plain","Forest","Mountain","Coast"};
inline const char* factionNames[FACTION_COUNT]={"The Kingdom","Iron Pact","Stone Realm","Trade Republic","Sable Fleet","Neutral"};
inline const Color factionColors[FACTION_COUNT]={
    {60,120,220,255},{220,50,50,255},{180,90,30,255},{220,200,50,255},{150,60,160,255},{120,120,100,255}
};

// (:455-461)
struct City {
    char     name[32];
    bool     built[BLD_COUNT];   // completed buildings
    int      constructing;       // -1 = nothing, else BuildingType
    int      constructTurns;     // turns remaining
    float    defBonus;           // from walls
};

// (:475-486)
struct Province {
    char       name[32];
    Vector2    center;          // on campaign map (pixel position)
    float      nx, ny;          // normalized position [0-1] for resize
    TerrainType terrain;
    FactionId  owner;
    bool       hasCity;
    City       city;
    std::vector<int> adjacent; // indices of adjacent provinces
    // Army stationed here (list of unit type indices + soldier counts)
    std::vector<std::pair<int,int>> army; // {typeIdx, soldierCount}
};

// Entrada de cola de reclutamiento (:558-562) — antes que CampaignState
struct RecruitEntry {
    int typeIdx;
    int turnsLeft;
    int originalTurns;  // 4.3: for progress bar
};

// Fase J: ejércitos de campo (múltiples ejércitos estilo Shogun 2)
struct FieldArmy {
    int      id=0;
    FactionId owner=FACTION_PLAYER;
    int      province=0;   // provincia donde se encuentra
    bool     moved=false;  // ya se movió/atacó este turno
    std::vector<std::pair<int,int>> units; // {typeIdx, soldierCount}
};

// Fase J: índices de los tipos de unidad héroe (initBuiltinTypes)
inline const int UNIT_GENERAL=8;
inline const int UNIT_KING=9;

// (:567-594)
struct CampaignState {
    int              campaignId=0;     // which campaign map (0..MAX_CAMPAIGNS-1)
    int              turn=1;
    Resources        res;
    int              playerProvince=0;  // current province of player army
    std::vector<Province> provinces;
    // Player army: list of {typeIdx, soldierCount}
    std::vector<std::pair<int,int>> playerArmy;
    // Pending battles: province index where battle happens
    int              pendingBattleProvince=-1;
    bool             pendingBattleIsDefense=false;
    // City being viewed
    int              viewedCity=-1;
    // Recruitment queue per city (province index -> queue)
    std::vector<RecruitEntry> recruitQueue; // for viewed city
    // Fase J: unidades sin asignar en reserva (reclutamiento + generales);
    // readyUnits es ahora una VISTA del ejército seleccionado
    std::vector<std::pair<int,int>> reserve;
    std::vector<std::pair<int,int>> readyUnits;
    // Fase J: múltiples ejércitos de campo (jugador + IA)
    std::vector<FieldArmy> armies;
    int              selectedArmy=-1;
    int              nextArmyId=1;
    // 6.3: Fog of war — explored provinces
    bool             explored[32]={};   // true if province has been seen
    // Fase D+: facciones aliadas con el jugador (persistencia en Fase I)
    bool             allied[FACTION_COUNT]={};
    // Fase I: pactos comerciales bilaterales con el jugador (con cualquier pais)
    bool             tradePact[FACTION_COUNT]={};
    // 3.4: Stats for victory/defeat screen
    int              battlesWon=0;
    int              battlesLost=0;
    int              peakProvinces=0;
    // Army movement animation (campaign map)
    int              armyMoveFrom=-1;
    int              armyMoveTo=-1;
    int              armyMoveIdx=-1;   // Fase J: qué ejército se está moviendo
    float            armyMoveT=0.f;
    // PvP (SAVE_VERSION=9): dos reinos separados. El host controla
    // FACTION_PLAYER; el cliente una facción designada (clientFaction).
    bool             pvpMode=false;    // true = cada jugador tiene su propio reino
    FactionId        clientFaction=FACTION_AGGRESSIVE; // facción del cliente
    Resources        clientRes;        // recursos del cliente (separados de res)
};

// (:666-675)
struct PreBattleState {
    int  provinceIdx;
    bool isDefense;
    // Which of the player's units to include (booleans)
    std::vector<bool> include;
    int  deployedCount;
    // Enemy info
    bool fogOfWar;
    int  estimatedEnemyStrength;
    // Fase J
    std::vector<std::pair<int,int>> enemyUnits; // snapshot: guarnición o ejército atacante
    int  attackerFaction=-1; // facción atacante en defensa (-1 si el jugador ataca)
    int  attackerArmyIdx=-1; // ejército IA atacante (-1 si no)
    int  armyIdx=-1;         // ejército del jugador involucrado (-1 si no)
    bool hadKing=false;      // el rey estaba en el ejército antes de la batalla
};

extern CampaignState  g_campaign;  // (:596)
extern PreBattleState g_preBattle; // (:676)

// Fase D+: bando del jugador en el mapa (propio + aliados = mismo color;
// cada faccion enemiga conserva el suyo)
bool  factionIsPlayerSide(FactionId f);
bool  factionIsHuman(FactionId f);   // PvP: host (PLAYER) o cliente (clientFaction)
Color factionDisplayColor(FactionId f);
// Faccion que controla el jugador local: FACTION_PLAYER (host/solo/co-op) o
// clientFaction (cliente PvP). main.cpp la actualiza cada frame segun el rol.
extern FactionId g_localFaction;

// ═══════════════════════════════════════════════════════════════════════════
//  FASE I: DIPLOMACIA (alianzas militares + pactos comerciales + trueque)
// ═══════════════════════════════════════════════════════════════════════════
// Modo de mapa de campana: 0 = normal (colores de bando), 1 = alianzas
// (aliado militar = color del jugador, pacto comercial = verde, resto gris).
// Solo cambia los colores; la logica no depende del modo.
extern int g_mapMode;
// Ultimo resultado de una accion diplomatica (mensaje + color para el panel)
extern char  g_diploMsg[160];
extern Color g_diploMsgCol;

int  factionMilitaryPower(int f);   // guarniciones + ejercitos de campo + reserva
int  totalMilitaryPower();          // potencia de todas las facciones
int  playerBlocPower();             // jugador + sus aliados militares
// Proponer alianza militar: una sola por jugador y bloqueada si la alianza
// existente o la resultante alcanza el 50% de la potencia total del mundo.
void proposeMilitaryAlliance(int f);
// Pacto comercial: permitido con cualquier pais, sin limite.
void proposeCommercialPact(int f);
void dissolveRelations(int f, bool military);
// Trueque de recursos con la IA (requiere pacto comercial; la IA acepta si
// el valor ofrecido >= valor pedido x factor de personalidad).
void proposeTradeDeal(int f, int giveRes, float giveAmt, int recvRes, float recvAmt);
Color diplomacyMapColor(FactionId f);  // color de provincia segun modo de mapa

// (:690, :692-693)
extern const float TRADE_RATES[5]; // gold, food, wood, stone, iron
extern int g_tradeTab;   // 0-4 for each resource
extern int g_tradeAmount; // amount to buy/sell

// (:773-774)
extern int g_deleteConfirmIdx;  // 4.4: garrison delete confirmation
extern int g_selectedProvince;  // 4.5: pulsing selected province

// Funciones (definiciones en campaign.cpp)
void generateCampaignMap(int campaignId);        // (:1039)
void updateProvinceCenters();                    // (:1214)
void newCampaign(int campaignId);                // (:1224)
bool processTurn();                              // (:1248) — Fase J: true = hay batalla pendiente (defensa IA)
void drawGarrisonArmiesPanel(Province& prov, float gridY, float cellH, Vector2 mouse); // (:820)
void drawCampaignMovementArrows();               // (:849)

// Fase J: ejércitos de campo
int  armyIndexAt(int province, FactionId owner);      // primer ejército de `owner` en provincia (-1)
void selectArmy(int idx);                             // sincroniza readyUnits + playerProvince
bool formArmyAt(int province);                        // consume 1 general de la reserva + tropas no-héroe
void joinArmyAt(int province);                        // reserva (no-héroes) -> ejército existente
void disbandArmy(int idx);                            // ejército -> reserva
int  armySoldiers(const std::vector<std::pair<int,int>>& units);
int  defenseSoldiers(int province);                   // soldados del jugador en la provincia
bool armyHasKing(const std::vector<std::pair<int,int>>& units);
void mergePlayerDefenders(int province);              // fusiona ejércitos del jugador en la provincia
int  safeRetreatProvince(int from);                   // provincia propia adyacente (-1 si no hay)
std::vector<std::pair<int,int>> subtractEnemyLosses(
    const std::vector<std::pair<int,int>>& units, int losses); // approx: bajas enemigas
