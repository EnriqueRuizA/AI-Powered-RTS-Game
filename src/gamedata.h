// src/gamedata.h - Servicio de datos de juego (fase D1: data-driven)
// Carga de unidades desde data/base/units/*.unit y de edificios desde
// data/base/buildings/*.bld con parser INI propio (precedente: config.cpp),
// sin dependencias externas. Si falta un archivo o una clave se conserva el
// valor builtin de elements.cpp/city.cpp y se emite un aviso con formato
// "archivo:linea: motivo".
#pragma once

#include "elements.h"
#include "city.h"
#include <string>
#include <vector>

// Directorio base de unidades (relativo al working directory del juego).
#define GAMEDATA_UNITS_DIR "data/base/units"
// Directorio base de edificios (D1 paso 2).
#define GAMEDATA_BUILDINGS_DIR "data/base/buildings"

// Carga unitsDir/*.unit sobre `target`. `target` entra como referencia de
// builtins: cada unidad solo se sustituye si su archivo existe y TODAS sus
// claves son validas; si falta archivo o clave se mantiene el builtin y se
// anade un aviso a `msgs` (nullable). Las claves desconocidas se ignoran
// con aviso (no invalidan el archivo).
// Devuelve el numero de unidades cargadas desde datos.
int loadUnitDataFrom(const char* unitsDir, std::vector<UnitTypeDef>& target,
                     std::vector<std::string>* msgs);

// Variante de juego: aplica sobre g_unitTypes y envia los avisos a consola
// (printf + TraceLog) y, si los hay, al battleLog. Devuelve nº cargadas.
int loadUnitData();

// Self-test headless (-datatest):
//   A) con data/ presente -> stats identicos a los builtins (golden)
//   B) sin data/         -> fallback total, valores identicos a builtins
//   C) archivo invalido  -> error con archivo:linea + fallback, sin crash
// Imprime "DATATEST PASS"/"DATATEST FAIL" y devuelve 0 = PASS, 1 = FAIL.
int runUnitDataTest();

// D1 paso 2: edificios data-driven. Mismo contrato que loadUnitDataFrom:
// `target` entra como referencia de builtins (defineBuiltinBuildings) y cada
// edificio SOLO se sustituye si su archivo existe y TODAS sus claves son
// validas; si falta archivo o clave se conserva el builtin y se anade un
// aviso a `msgs` (nullable). El orden de `target` (enum BuildingType) se
// preserva: la sustitucion es por nombre en su propio slot.
// Devuelve el numero de edificios cargados desde datos.
int loadBuildingDataFrom(const char* buildingsDir, BuildingDef* target,
                         std::vector<std::string>* msgs);

// Variante de juego: defineBuiltinBuildings -> loadBuildingDataFrom ->
// applyBuildingDefs (tablas globales activas) y avisos a consola (printf +
// TraceLog) y, si los hay, al battleLog. Devuelve nº cargadas.
int loadBuildingData();

// Self-test headless de buildings (seccion del -datatest):
//   A) data/ presente -> 15/15, golden campo a campo + orden de slots
//   B) sin data/      -> fallback total, 15 avisos "file not found"
//   C) archivo invalido -> error archivo:linea + fallback, sin cambios
// Imprime "BLDDATATEST PASS"/"BLDDATATEST FAIL" y devuelve 0 = PASS, 1 = FAIL.
int runBuildingDataTest();
