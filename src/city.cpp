#include "city.h"
#include "config.h"
#include "campaign.h"
#include "util.h"
#include "ui.h"
#include <vector>
#include <string>
#include <deque>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <random>
#include <functional>
#include <cassert>
#include <utility>

const char* bldNames[BLD_COUNT]={};
int bldGoldCost[BLD_COUNT]={};
int bldWoodCost[BLD_COUNT]={};
int bldStoneCost[BLD_COUNT]={};
int bldIronCost[BLD_COUNT]={};
int bldTurns[BLD_COUNT]={};
int bldPrereq[BLD_COUNT]={};
bool bldCoastal[BLD_COUNT]={};
float bldGoldYield[BLD_COUNT]={};
float bldFoodYield[BLD_COUNT]={};
float bldWoodYield[BLD_COUNT]={};
float bldStoneYield[BLD_COUNT]={};
float bldIronYield[BLD_COUNT]={};

namespace {
// Golden D1 paso 2: única fuente de verdad de los 15 edificios. El orden
// DEBE coincidir con el enum BuildingType — los saves almacenan índices y el
// loader de data/base/buildings sustituye por nombre, nunca reordena.
struct BuiltinBld {
    const char* name;
    int goldCost, woodCost, stoneCost, ironCost, turns, prereq;
    float goldYield, foodYield, woodYield, stoneYield, ironYield;
    bool coastal;
};
const BuiltinBld kBuiltin[BLD_COUNT]={
    {"Market",           200,  0,  0,  0, 2, -1,  80.f,  0.f,  0.f,  0.f,  0.f, false}, // 0
    {"Farm",             120,  0,  0,  0, 1, -1,   0.f, 60.f,  0.f,  0.f,  0.f, false}, // 1
    {"Sawmill",          150,  0,  0,  0, 2,  1,   0.f,  0.f, 50.f,  0.f,  0.f, false}, // 2 req Farm
    {"Quarry",           200,  0,  0,  0, 3, -1,   0.f,  0.f,  0.f, 40.f,  0.f, false}, // 3
    {"Smithy",           300,  0,  0,  0, 3,  2,   0.f,  0.f,  0.f,  0.f, 30.f, false}, // 4 req Sawmill
    {"Barracks",         200, 60,  0,  0, 2, -1,   0.f,  0.f,  0.f,  0.f,  0.f, false}, // 5
    {"Stable",           350,100,  0,  0, 3,  5,   0.f,  0.f,  0.f,  0.f,  0.f, false}, // 6 req Barracks
    {"Archery Range",    250, 80,  0,  0, 2,  5,   0.f,  0.f,  0.f,  0.f,  0.f, false}, // 7 req Barracks
    {"Armoury Workshop", 400,120,  0,120, 4,  4,   0.f,  0.f,  0.f,  0.f,  0.f, false}, // 8 req Smithy
    {"Walls Lv.1",       300,  0,150,  0, 3, -1,   0.f,  0.f,  0.f,  0.f,  0.f, false}, // 9
    {"Walls Lv.2",       600,  0,300,  0, 4,  9,   0.f,  0.f,  0.f,  0.f,  0.f, false}, // 10 req Walls1
    {"Mage Tower",       500,  0,200,  0, 5, -1,   0.f,  0.f,  0.f,  0.f,  0.f, false}, // 11
    {"Temple",           250,  0,  0,  0, 2, -1,   0.f,  0.f,  0.f,  0.f,  0.f, false}, // 12
    {"Port",             400,100,  0,  0, 3, -1, 120.f,  0.f,  0.f,  0.f,  0.f, true }, // 13 coastal
    {"Watchtower",       180, 80, 60,  0, 2, -1,   0.f,  0.f,  0.f,  0.f,  0.f, false}, // 14
};
} // namespace

// Storage estable para bldNames[] (mismo patrón que las fuentes de unidades).
static BuildingDef g_bldDefs[BLD_COUNT];

void defineBuiltinBuildings(BuildingDef out[BLD_COUNT]){
    for(int i=0;i<BLD_COUNT;i++){
        const BuiltinBld& k=kBuiltin[i];
        snprintf(out[i].name,sizeof out[i].name,"%s",k.name);
        out[i].goldCost=k.goldCost;
        out[i].woodCost=k.woodCost;
        out[i].stoneCost=k.stoneCost;
        out[i].ironCost=k.ironCost;
        out[i].turns=k.turns;
        out[i].prereq=k.prereq;
        out[i].goldYield=k.goldYield;
        out[i].foodYield=k.foodYield;
        out[i].woodYield=k.woodYield;
        out[i].stoneYield=k.stoneYield;
        out[i].ironYield=k.ironYield;
        out[i].coastal=k.coastal;
    }
}

void applyBuildingDefs(const BuildingDef defs[BLD_COUNT]){
    for(int i=0;i<BLD_COUNT;i++){
        g_bldDefs[i]=defs[i];
        bldNames[i]=g_bldDefs[i].name;
        bldGoldCost[i]=defs[i].goldCost;
        bldWoodCost[i]=defs[i].woodCost;
        bldStoneCost[i]=defs[i].stoneCost;
        bldIronCost[i]=defs[i].ironCost;
        bldTurns[i]=defs[i].turns;
        bldPrereq[i]=defs[i].prereq;
        bldCoastal[i]=defs[i].coastal;
        bldGoldYield[i]=defs[i].goldYield;
        bldFoodYield[i]=defs[i].foodYield;
        bldWoodYield[i]=defs[i].woodYield;
        bldStoneYield[i]=defs[i].stoneYield;
        bldIronYield[i]=defs[i].ironYield;
    }
}

// D6: prerrequisito único — la UI consulta aquí en vez de duplicar el literal
bool bldHasPrereq(int b, const City& c){
    if(b<0||b>=BLD_COUNT) return false;
    int pi=bldPrereq[b];
    return (pi<0||c.built[pi]);
}

const char* bldReqName(int b){
    if(b<0||b>=BLD_COUNT) return "";
    int pi=bldPrereq[b];
    if(pi<0||pi>=BLD_COUNT) return "";
    return bldNames[pi];
}
