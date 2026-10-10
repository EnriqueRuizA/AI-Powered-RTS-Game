// src/save.h — Serialización binaria (formato actual: binario con SAVE_VERSION=10)
#pragma once

#include <vector>

struct CampaignState;

inline const char* CAMPAIGN_LAST_FILE = "campaign_last.dat";
inline const int   MAX_CAMPAIGNS      = 4;
inline const char* CAMPAIGN_NAMES[MAX_CAMPAIGNS] = { "The Realm", "Northern Isles", "Eastern March", "Great Continent" };

const char* getCampaignSavePath(int id);
bool saveGame();        // true si el guardado en disco tuvo éxito
bool loadGame();

// Fase H: el mismo formato binario SAVE_VERSION=8 ahora se serializa a un
// buffer en memoria (snapshots de red) y a disco (campaign_%d.dat).
// saveGame/loadGame son wrappers de estos.
bool serializeCampaignState(const CampaignState& st, std::vector<char>& out);
bool deserializeCampaignState(CampaignState& st, const char* data, size_t size);
// Fase H: aplica un snapshot de red a g_campaign (deserializa + post-proceso
// de vista: centros, ejército seleccionado, cancela animaciones).
bool applyCampaignSnapshot(const char* data, size_t size);
