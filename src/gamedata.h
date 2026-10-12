// src/gamedata.h - Servicio de datos de juego (fase D1: data-driven)
// Carga de unidades desde data/base/units/*.unit con parser INI propio
// (precedente: config.cpp), sin dependencias externas. Si falta un archivo
// o una clave se conserva el valor builtin de elements.cpp y se emite un
// aviso con formato "archivo:linea: motivo".
#pragma once

#include "elements.h"
#include <string>
#include <vector>

// Directorio base de unidades (relativo al working directory del juego).
#define GAMEDATA_UNITS_DIR "data/base/units"

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
