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

const char* bldNames[BLD_COUNT]={
    "Market","Farm","Sawmill","Quarry","Smithy",
    "Barracks","Stable","Archery Range","Armoury Workshop",
    "Walls Lv.1","Walls Lv.2","Mage Tower","Temple","Port",
    "Watchtower"
};
const int bldGoldCost[BLD_COUNT]={200,120,150,200,300,200,350,250,400,300,600,500,250,400,180};
const int bldWoodCost[BLD_COUNT]={0,0,0,0,0,60,100,80,120,0,0,0,0,100,80};
const int bldStoneCost[BLD_COUNT]={0,0,0,0,0,0,0,0,0,150,300,200,0,0,60};
const int bldIronCost[BLD_COUNT]={0,0,0,0,0,0,0,0,120,0,0,0,0,0,0};
const int bldTurns[BLD_COUNT]={2,1,2,3,3,2,3,2,4,3,4,5,2,3,2};
const int bldPrereq[BLD_COUNT]={-1,-1,1,-1,2,-1,5,5,4,-1,9,-1,-1,-1,-1}; // Smithy req Sawmill, Stable/Range req Barracks, Workshop req Smithy, Walls2 req Walls1

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

// Gold/food/wood/stone/iron per turn per building
const float bldGoldYield[BLD_COUNT]={80,0,0,0,0,0,0,0,0,0,0,0,0,120,0};
const float bldFoodYield[BLD_COUNT]={0,60,0,0,0,0,0,0,0,0,0,0,0,0,0};
const float bldWoodYield[BLD_COUNT]={0,0,50,0,0,0,0,0,0,0,0,0,0,0,0};
const float bldStoneYield[BLD_COUNT]={0,0,0,40,0,0,0,0,0,0,0,0,0,0,0};
const float bldIronYield[BLD_COUNT]={0,0,0,0,30,0,0,0,0,0,0,0,0,0,0};
