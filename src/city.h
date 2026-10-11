// src/city.h — Construcciones y recursos de ciudad
#pragma once

#include "config.h"

// La struct City vive en campaign.h (:455). Tablas de edificios reales (:436-453);
// definiciones en city.cpp.
extern const char* bldNames[BLD_COUNT];
extern const int   bldGoldCost[BLD_COUNT];
extern const int   bldWoodCost[BLD_COUNT];
extern const int   bldStoneCost[BLD_COUNT];
extern const int   bldIronCost[BLD_COUNT];
extern const int   bldTurns[BLD_COUNT];
extern const int   bldPrereq[BLD_COUNT];
// D6: prerrequisito único (la UI antes duplicaba el literal bldPrereq[b]).
// Devuelve true si el edificio `b` puede construirse en la ciudad `c`.
struct City;
bool bldHasPrereq(int b, const City& c);
// Nombre del edificio requerido por `b` ("" si no requiere ninguno)
const char* bldReqName(int b);
extern const float bldGoldYield[BLD_COUNT];
extern const float bldFoodYield[BLD_COUNT];
extern const float bldWoodYield[BLD_COUNT];
extern const float bldStoneYield[BLD_COUNT];
extern const float bldIronYield[BLD_COUNT];
