// src/city.h — Construcciones y recursos de ciudad
#pragma once

#include "config.h"

// La struct City vive en campaign.h (:455). Tablas de edificios reales (:436-453);
// definiciones en city.cpp. D1 paso 2: las tablas ya NO son const — se rellenan
// con defineBuiltinBuildings() (golden, sin GL) y loadBuildingData() puede
// sobreescribirlas desde data/base/buildings/*.bld (mismo fallback que units).
extern const char* bldNames[BLD_COUNT];
extern int   bldGoldCost[BLD_COUNT];
extern int   bldWoodCost[BLD_COUNT];
extern int   bldStoneCost[BLD_COUNT];
extern int   bldIronCost[BLD_COUNT];
extern int   bldTurns[BLD_COUNT];
extern int   bldPrereq[BLD_COUNT];
// D1 paso 2: true si el edificio solo puede construirse en costa (Port).
extern bool  bldCoastal[BLD_COUNT];
// D6: prerrequisito único (la UI antes duplicaba el literal bldPrereq[b]).
// Devuelve true si el edificio `b` puede construirse en la ciudad `c`.
struct City;
bool bldHasPrereq(int b, const City& c);
// Nombre del edificio requerido por `b` ("" si no requiere ninguno)
const char* bldReqName(int b);
extern float bldGoldYield[BLD_COUNT];
extern float bldFoodYield[BLD_COUNT];
extern float bldWoodYield[BLD_COUNT];
extern float bldStoneYield[BLD_COUNT];
extern float bldIronYield[BLD_COUNT];

// D1 paso 2: definición data-driven de un edificio (data/base/buildings/*.bld).
// El orden de los 15 slots ES el enum BuildingType (los saves guardan índices);
// el loader sustituye por nombre, nunca reordena.
struct BuildingDef {
    char  name[32];
    int   goldCost, woodCost, stoneCost, ironCost;
    int   turns;                     // turnos de construcción
    int   prereq;                    // -1 o índice BuildingType requerido
    float goldYield, foodYield, woodYield, stoneYield, ironYield; // por turno
    bool  coastal;                   // solo en provincia costera (Port)
};

// Rellena `out` (BLD_COUNT entradas) con los edificios golden de city.cpp.
// Sin GL ni E/S: utilizable en self-tests headless y en loadBuildingData().
void defineBuiltinBuildings(BuildingDef out[BLD_COUNT]);

// Copia `defs` (BLD_COUNT entradas) a g_bldDefs y a los arrays globales de
// arriba (incluido bldCoastal). bldNames[] apunta al storage estable g_bldDefs.
void applyBuildingDefs(const BuildingDef defs[BLD_COUNT]);
