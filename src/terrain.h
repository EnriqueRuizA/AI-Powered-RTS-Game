// src/terrain.h — Obstáculos AABB por tipo de terreno
#pragma once
#include "config.h"
#include <vector>

// Extraído de initBattle (rts_game.cpp:1465-1485)
// v11: hasCity añade edificios + muralla al campo (provincia con ciudad)
void generateTerrainObstacles(TerrainType terrain,int provinceIdx,std::vector<Rectangle>& out,
                              bool hasCity=false);
