// src/ui_multiplayer.h - Pantalla MULTIPLAYER (Fase G: lobby host/join)
// Extraida de main.cpp en el refactor R1 (paso 1). Refactor mecanico: misma
// UI, mismos textos/colores/coordinadas y mismo flujo de estados.
#pragma once

#include "config.h"

// Dibuja el lobby de red (status, jugadores, toggle PvP, HOST/JOIN, BACK)
// y devuelve el siguiente GameState. El estado compartido es g_netSession
// (net.cpp) y g_campaign (campaign.cpp).
GameState updateDrawMultiplayer(Vector2 mouse);
