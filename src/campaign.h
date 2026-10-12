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
    bool     built[BLD_COUNT];   // completed buildings (true si count>=1)
    int      constructing;       // -1 = nothing, else BuildingType
    int      constructTurns;     // turns remaining
    float    defBonus;           // from walls
    // v11: matriz de construcción 4x4 + edificios repetibles
    int      grid[16];           // slots de construcción (tipo o -1; murallas fuera)
    unsigned char count[BLD_COUNT]; // nº de copias de cada edificio
    int      constructSlot;      // slot destino (-1 = auto / perímetro para murallas)
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
    int faction=0;      // PvP: facción que recluta ( enruta a su reserva )
};

// Fase J: ejércitos de campo (múltiples ejércitos estilo Shogun 2)
struct FieldArmy {
    int      id=0;
    FactionId owner=FACTION_PLAYER;
    int      province=0;   // provincia donde se encuentra
    bool     moved=false;  // ya se movió/atacó este turno
    std::vector<std::pair<int,int>> units; // {typeIdx, soldierCount}
    bool     isShip=false; // v11: true = ejército embarcado (se mueve por costa)
    // D2: veteranía inicial por Range/Stable (0..3). Se asigna al formar el
    // ejército en una ciudad con esos edificios; initBattle la pasa a las
    // unidades del jugador (SAVE_VERSION=12).
    int      veterancy=0;
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
    std::vector<std::pair<int,int>> clientReserve; // PvP: reserva del cliente (separada)
    std::vector<std::pair<int,int>> readyUnits;
    // Fase J: múltiples ejércitos de campo (jugador + IA)
    std::vector<FieldArmy> armies;
    int              selectedArmy=-1;
    int              nextArmyId=1;
    // 6.3: Fog of war — explored provinces (D1: tamaño dinámico = nº real de
    // provincias; el array fijo de 32 dejaba Europa con niebla permanente en
    // las provincias 32-35)
    std::vector<bool> explored;         // true if province has been seen
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
    int  attackerFaction=-1; // facción atacante (defensa: IA; PvP: facción del
                             // cliente). -1 = ataque del host => FACTION_PLAYER
    int  attackerArmyIdx=-1; // ejército IA atacante (-1 si no)
    int  armyIdx=-1;         // ejército del bando local involucrado (-1 si no;
                             // en un ataque remoto, el ejército del cliente)
    bool hadKing=false;      // el rey estaba en el ejército antes de la batalla
};

extern CampaignState  g_campaign;  // (:596)
extern PreBattleState g_preBattle; // (:676)

// Fase D+: bando del jugador en el mapa (propio + aliados = mismo color;
// cada faccion enemiga conserva el suyo)
bool  factionIsPlayerSide(FactionId f);
bool  factionIsHuman(FactionId f);   // PvP: host (PLAYER) o cliente (clientFaction)
Color factionDisplayColor(FactionId f);
// Relacion de hostilidad (NEW-QA2-01): true si `actor` puede atacar a
// `target`. Gate unico del mapa (odds + click de ataque) y de la validacion
// de NET_CMD_ATTACK en el host, para que el PvP sea bidireccional.
bool  isHostileTo(FactionId actor, FactionId target);
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
void updateFogOfWar();                           // v11: niebla con atalaya (2 saltos)

// Fase J: ejércitos de campo
int  armyIndexAt(int province, FactionId owner);      // primer ejército de `owner` en provincia (-1)
void selectArmy(int idx);                             // sincroniza readyUnits + playerProvince
bool formArmyAt(int province, int faction=-1);   // consume 1 general de la reserva + tropas no-héroe (-1 = g_localFaction)
void joinArmyAt(int province, int faction=-1);   // reserva (no-héroes) -> ejército existente (-1 = g_localFaction)
void disbandArmy(int idx);                            // ejército -> reserva
int  armySoldiers(const std::vector<std::pair<int,int>>& units);
int  defenseSoldiers(int province);                   // soldados del jugador en la provincia
bool armyHasKing(const std::vector<std::pair<int,int>>& units);
void mergePlayerDefenders(int province);              // fusiona ejércitos del jugador en la provincia
int  safeRetreatProvince(int from);                   // provincia propia adyacente (-1 si no hay)
// D2: bonus reales de edificios (antes solo pintados en UI)
// Turns de reclutamiento con bonus de Barracks: max(1, base - copias Barracks)
int  recruitTurnsFor(int province, int baseTurns);
// Veteranía inicial (0..3) = copias de Range + Stable en la ciudad de la
// provincia (sin ciudad o ajena → 0)
int  cityVeterancy(int province);
// v11: navegación — un barco alcanza `to` si es adyacente, o si hay una
// provincia costera intermedia adyacente a ambas (ruta marítima de 2 saltos)
bool shipCanReach(int from, int to);
std::vector<std::pair<int,int>> subtractEnemyLosses(
    const std::vector<std::pair<int,int>>& units, int losses); // approx: bajas enemigas

// ═══════════════════════════════════════════════════════════════════════════
//  POST-BATALLA: resultado -> mapa de campaña (única ruta de fin de batalla)
// ═══════════════════════════════════════════════════════════════════════════
// El rey murió en la última batalla (fin de partida al pulsar CONTINUE).
// Se escribe aquí (applyBattleResultCampaign) y solo se lee en main.cpp.
extern bool g_kingLost;
// Aplica g_lastResult al mapa: captura de provincia, REMOCIÓN del ejército
// enemigo derrotado (ataque y defensa), retirada del defensor, loot y
// supervivientes. Todas las rutas (batalla táctica, auto-resolve, quick
// battle) convergen en el result screen que la llama. Idempotente: se invoca
// cada frame del result screen; devuelve true solo en el frame en que aplicó
// cambios (para que el caller retransmita el estado en red).
bool applyBattleResultCampaign(bool quickBattle);
// Elimina el ejército `idx` manteniendo coherentes selectedArmy y los índices
// de g_preBattle (un erase cruza los índices y el result screen acabaría
// escribiendo supervivientes en el ejército equivocado).
void removeArmyAt(int idx);
// Self-test headless (-battletest): rutas de fin de batalla -> 0/1
int runBattleResultTest();
