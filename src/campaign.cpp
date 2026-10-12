#include "campaign.h"
#include "config.h"
#include "util.h"
#include "ui.h"
#include "elements.h"
#include "city.h"
#include "save.h"
#include "combat.h"
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

CampaignState  g_campaign;  // (:596)
PreBattleState g_preBattle; // (:676)

//  UI STATE GLOBALS (4.4, 4.5)
// ───────────────────────────────────────────────────────────────────────────
int g_deleteConfirmIdx  = -1;  // 4.4: garrison delete confirmation
int g_selectedProvince  = -1;  // 4.5: pulsing selected province

// ───────────────────────────────────────────────────────────────────────────
//  Fase D+: bando del jugador en el mapa
//  Territorio propio y de facciones aliadas => mismo color (el del jugador);
//  cada faccion enemiga conserva el suyo bien visible.
// ───────────────────────────────────────────────────────────────────────────
FactionId g_localFaction=FACTION_PLAYER; // main.cpp la actualiza segun el rol de red
// Reserva de la facción local (PLAYER o clientFaction en PvP)
static std::vector<std::pair<int,int>>& localReserve(){
    return (g_localFaction==FACTION_PLAYER)?g_campaign.reserve:g_campaign.clientReserve;
}
bool factionIsPlayerSide(FactionId f){
    if(f==FACTION_PLAYER) return true;
    if((int)f<0||(int)f>=FACTION_COUNT) return false;
    return g_campaign.allied[(int)f];
}
// PvP: true si la faccion esta controlada por un humano (host o cliente).
// En co-op solo FACTION_PLAYER; en PvP ademas la clientFaction del cliente.
bool factionIsHuman(FactionId f){
    if(f==FACTION_PLAYER) return true;
    if(g_campaign.pvpMode&&f==g_campaign.clientFaction) return true;
    return false;
}
Color factionDisplayColor(FactionId f){
    if(factionIsPlayerSide(f)) return factionColors[FACTION_PLAYER];
    if((int)f<0||(int)f>=FACTION_COUNT) return factionColors[FACTION_NEUTRAL];
    return factionColors[(int)f];
}

// ───────────────────────────────────────────────────────────────────────────
//  Fase J: ejércitos de campo (múltiples ejércitos estilo Shogun 2)
// ───────────────────────────────────────────────────────────────────────────
static bool isHeroType(int ti){
    if(ti<0||ti>=unitTypeCount()) return false;
    return ti==UNIT_GENERAL||ti==UNIT_KING;
}

// v11: inicialización correcta de City (grid=-1, count=0, constructSlot=-1)
static void initCity(City& c, const char* nm, FactionId owner){
    memset(&c,0,sizeof c);
    strncpy(c.name,nm,sizeof(c.name)-1); c.name[sizeof(c.name)-1]='\0';
    c.constructing=-1; c.constructTurns=0; c.defBonus=1.0f;
    for(int i=0;i<16;i++) c.grid[i]=-1;
    for(int b=0;b<BLD_COUNT;b++){ c.count[b]=0; c.built[b]=false; }
    c.constructSlot=-1;
    // Edificios iniciales según facción (van al grid para visualización)
    auto addBld=[&](BuildingType b){
        c.built[b]=true; c.count[b]++;
        // Colocar en el primer slot libre (murallas van al perímetro, no al grid)
        if(b!=BLD_WALLS1&&b!=BLD_WALLS2){
            for(int s=0;s<16;s++) if(c.grid[s]<0){ c.grid[s]=(int)b; break; }
        }
    };
    if(owner==FACTION_PLAYER){ addBld(BLD_FARM); addBld(BLD_BARRACKS); }
    if(owner==FACTION_AGGRESSIVE||owner==FACTION_DEFENSIVE){ addBld(BLD_FARM); addBld(BLD_BARRACKS); }
    if(owner==FACTION_COMMERCIAL){ addBld(BLD_FARM); addBld(BLD_MARKET); }
}

// v11: fog of war — explora provincias propias + adyacentes + 2 saltos si hay atalaya
// D1: explored es vector dinámico (redimensiona al nº real de provincias; el
// array fijo de 32 no cubría Europa = 36 provincias)
void updateFogOfWar(){
    int np=(int)g_campaign.provinces.size();
    if((int)g_campaign.explored.size()!=np) g_campaign.explored.resize(np,false);
    // Propias siempre exploradas
    for(int i=0;i<np;i++)
        if(g_campaign.provinces[i].owner==g_localFaction)
            g_campaign.explored[i]=true;
    // 1 salto desde cualquier provincia propia
    for(int i=0;i<np;i++){
        if(g_campaign.provinces[i].owner!=g_localFaction) continue;
        for(int adj:g_campaign.provinces[i].adjacent)
            if(adj>=0&&adj<np) g_campaign.explored[adj]=true;
    }
    // 2 saltos si alguna ciudad propia tiene atalaya
    bool watch=false;
    for(int i=0;i<np;i++){
        Province& p=g_campaign.provinces[i];
        if(p.owner==g_localFaction&&p.hasCity&&p.city.built[BLD_WATCHTOWER]) watch=true;
    }
    if(watch){
        for(int i=0;i<np;i++){
            if(g_campaign.provinces[i].owner!=g_localFaction) continue;
            for(int a1:g_campaign.provinces[i].adjacent){
                if(a1<0||a1>=np) continue;
                for(int a2:g_campaign.provinces[a1].adjacent)
                    if(a2>=0&&a2<np) g_campaign.explored[a2]=true;
            }
        }
    }
}

int armySoldiers(const std::vector<std::pair<int,int>>& units){
    int s=0;
    for(auto [ti,c]:units) s+=c;
    return s;
}

// D2: turns de reclutamiento con bonus de Barracks (-1t por copia, min 1 turno)
int recruitTurnsFor(int province, int baseTurns){
    if(province<0||province>=(int)g_campaign.provinces.size()) return baseTurns;
    const Province& p=g_campaign.provinces[province];
    if(!p.hasCity) return baseTurns;
    int t=baseTurns-(int)p.city.count[BLD_BARRACKS];
    if(t<1) t=1;
    return t;
}

// D2: veteranía inicial (0..3) = Range + Stable en la ciudad de la provincia
int cityVeterancy(int province){
    if(province<0||province>=(int)g_campaign.provinces.size()) return 0;
    const Province& p=g_campaign.provinces[province];
    if(!p.hasCity) return 0;
    int v=(int)p.city.count[BLD_RANGE]+(int)p.city.count[BLD_STABLE];
    if(v>3) v=3;
    return v;
}

bool armyHasKing(const std::vector<std::pair<int,int>>& units){
    for(auto [ti,c]:units) if(ti==UNIT_KING&&c>0) return true;
    return false;
}

int armyIndexAt(int province, FactionId owner){
    for(int i=0;i<(int)g_campaign.armies.size();i++)
        if(g_campaign.armies[i].province==province&&g_campaign.armies[i].owner==owner)
            return i;
    return -1;
}

void selectArmy(int idx){
    // Solo ejércitos de la faccion local (PLAYER o clientFaction en PvP):
    // si idx no existe o es de la IA (p.ej. selectArmy(0) cuando armies[0]
    // es un ejército IA), cae al primero propio. readyUnits NUNCA debe
    // contener tropas enemigas.
    auto trySelect=[&](int i)->bool{
        if(i<0||i>=(int)g_campaign.armies.size()) return false;
        if(g_campaign.armies[i].owner!=g_localFaction) return false;
        g_campaign.selectedArmy=i;
        g_campaign.readyUnits=g_campaign.armies[i].units;
        g_campaign.playerProvince=g_campaign.armies[i].province;
        return true;
    };
    if(trySelect(idx)) return;
    for(int i=0;i<(int)g_campaign.armies.size();i++)
        if(trySelect(i)) return;
    g_campaign.selectedArmy=-1;
    g_campaign.readyUnits.clear();
}

int defenseSoldiers(int province){
    int s=0;
    for(auto& a:g_campaign.armies)
        if(a.province==province&&a.owner==g_localFaction)
            s+=armySoldiers(a.units);
    return s;
}

// Fusiona todos los ejércitos del jugador en `province` en el primero de ellos
// (los refuerzos se unen antes de la batalla defensiva, estilo Shogun)
void mergePlayerDefenders(int province){
    int first=-1;
    for(int i=0;i<(int)g_campaign.armies.size();i++){
        auto& a=g_campaign.armies[i];
        if(a.province!=province||a.owner!=FACTION_PLAYER) continue;
        if(first<0){ first=i; continue; }
        for(auto& u:a.units) g_campaign.armies[first].units.push_back(u);
        // D2: la veteranía del ejército resultante es la máxima
        g_campaign.armies[first].veterancy=
            std::max(g_campaign.armies[first].veterancy,a.veterancy);
        a.units.clear();
    }
    if(first<0) return;
    // Eliminar los fusionados (índices > first)
    std::vector<FieldArmy> keep;
    for(int i=0;i<(int)g_campaign.armies.size();i++)
        if(i==first||g_campaign.armies[i].province!=province||
           g_campaign.armies[i].owner!=FACTION_PLAYER)
            keep.push_back(g_campaign.armies[i]);
    g_campaign.armies=std::move(keep);
    // Re-seleccionar el ejército fusionado
    selectArmy(armyIndexAt(province,FACTION_PLAYER));
}

// Provincia propia adyacente a la que retirarse (-1 si no existe)
int safeRetreatProvince(int from){
    if(from<0||from>=(int)g_campaign.provinces.size()) return -1;
    for(int adj:g_campaign.provinces[from].adjacent){
        if(adj<0||adj>=(int)g_campaign.provinces.size()) continue;
        if(g_campaign.provinces[adj].owner==FACTION_PLAYER) return adj;
    }
    return -1;
}

// v11: ruta marítima — un barco alcanza `to` si es adyacente (desembarque
// directo), o si existe una provincia costera `mid` adyacente a ambas
// (navegación de 2 saltos por la costa). `to` no hace falta que sea costera
// si es adyacente (permite desembarcar en tierra desde el mar).
bool shipCanReach(int from, int to){
    const auto& P=g_campaign.provinces;
    int n=(int)P.size();
    if(from<0||from>=n||to<0||to>=n||from==to) return false;
    for(int adj:P[from].adjacent) if(adj==to) return true;
    // 2 saltos: `from` —mid(costera)— `to`
    if(P[from].terrain!=TERRAIN_COAST && P[to].terrain!=TERRAIN_COAST) return false;
    for(int mid:P[from].adjacent){
        if(mid<0||mid>=n||mid==to) continue;
        if(P[mid].terrain!=TERRAIN_COAST) continue;
        for(int adj2:P[mid].adjacent)
            if(adj2==to) return true;
    }
    return false;
}

// Resta `losses` soldados de los grupos en orden (aprox. de bajas enemigas)
std::vector<std::pair<int,int>> subtractEnemyLosses(
        const std::vector<std::pair<int,int>>& units, int losses){
    std::vector<std::pair<int,int>> out;
    for(auto [ti,c]:units){
        if(losses>0){
            int take=std::min(c,losses);
            c-=take; losses-=take;
        }
        if(c>0) out.push_back({ti,c});
    }
    return out;
}

bool formArmyAt(int province, int faction){
    // Requiere 1 general (o el rey) en la reserva de la facción
    FactionId fo=(faction>=0)?(FactionId)faction:g_localFaction;
    auto& resv=(fo==FACTION_PLAYER)?g_campaign.reserve:g_campaign.clientReserve;
    int gi=-1;
    for(int i=0;i<(int)resv.size();i++)
        if(isHeroType(resv[i].first)){ gi=i; break; }
    if(gi<0) return false;
    FieldArmy a;
    a.id=g_campaign.nextArmyId++;
    a.owner=fo; // PvP: el ejército pertenece a la facción indicada
    a.province=province;
    // D2: veteranía de la ciudad si la provincia es de la facción (Range+Stable)
    if(province>=0&&province<(int)g_campaign.provinces.size()&&
       g_campaign.provinces[province].owner==fo)
        a.veterancy=cityVeterancy(province);
    a.units.push_back(resv[gi]);
    resv.erase(resv.begin()+gi);
    // Tropas no-héroe de la reserva se unen al nuevo ejército
    std::vector<std::pair<int,int>> heroes;
    for(auto& u:resv){
        if(isHeroType(u.first)) heroes.push_back(u);
        else a.units.push_back(u);
    }
    resv=std::move(heroes);
    g_campaign.armies.push_back(a);
    selectArmy((int)g_campaign.armies.size()-1);
    return true;
}

void joinArmyAt(int province, int faction){
    FactionId fo=(faction>=0)?(FactionId)faction:g_localFaction;
    int ai=armyIndexAt(province,fo);
    if(ai<0) return;
    // D2: al unir tropas en una ciudad propia, la veteranía sube al máximo
    if(province>=0&&province<(int)g_campaign.provinces.size()&&
       g_campaign.provinces[province].owner==fo)
        g_campaign.armies[ai].veterancy=
            std::max(g_campaign.armies[ai].veterancy,cityVeterancy(province));
    auto& resv=(fo==FACTION_PLAYER)?g_campaign.reserve:g_campaign.clientReserve;
    std::vector<std::pair<int,int>> heroes;
    for(auto& u:resv){
        if(isHeroType(u.first)) heroes.push_back(u);
        else g_campaign.armies[ai].units.push_back(u);
    }
    resv=std::move(heroes);
    if(g_campaign.selectedArmy==ai) g_campaign.readyUnits=g_campaign.armies[ai].units;
}

void disbandArmy(int idx){
    if(idx<0||idx>=(int)g_campaign.armies.size()) return;
    auto& resv=localReserve(); // PvP: vuelve a la reserva de su facción
    for(auto& u:g_campaign.armies[idx].units) resv.push_back(u);
    g_campaign.armies.erase(g_campaign.armies.begin()+idx);
    int nxt=(int)g_campaign.armies.size();
    selectArmy(nxt==0?-1:std::min(idx,nxt-1));
}


// ───────────────────────────────────────────────────────────────────────────
//  GARRISON ARMIES PANEL (0.1: extracted from orphan code)
// ───────────────────────────────────────────────────────────────────────────
void drawGarrisonArmiesPanel(Province& prov, float gridY, float cellH, Vector2 mouse){
    // Draw a panel showing garrisoned armies for the given province
    // Currently shown inline in city management as part of the building grid;
    // this extracted function can be called for additional overlay
    float panelY=gridY+3*cellH+8;
    if(panelY+60>SCREEN_H-44) return;
    DrawRectangle(8,(int)panelY,(int)(SCREEN_W-16),54,{10,16,10,180});
    DrawRectangleLinesEx({8,panelY,(float)(SCREEN_W-16),54},1,{60,90,50,180});
    DrawText("Garrison:",(int)18,(int)(panelY+6),13,C_GOLD);
    if(prov.army.empty()){
        DrawText("(no garrison)",(int)100,(int)(panelY+8),12,C_SECONDARY);
    } else {
        int ax=100;
        for(int k=0;k<(int)prov.army.size()&&k<8;k++){
            auto [ti,cnt]=prov.army[k];
            if(ti>=unitTypeCount()) continue;
            const UnitTypeDef& td=g_unitTypes[ti];
            DrawRectangle(ax,(int)(panelY+6),14,14,{td.r,td.g,td.b,220});
            DrawText(TextFormat("%s x%d",td.name,cnt),ax+18,(int)(panelY+8),11,C_PARCHMENT);
            ax+=120+MeasureText(td.name,11);
        }
    }
    (void)mouse;
    (void)cellH;
}

// ───────────────────────────────────────────────────────────────────────────
//  CAMPAIGN MOVEMENT ARROWS (0.1: extracted function)
// ───────────────────────────────────────────────────────────────────────────
void drawCampaignMovementArrows(){
    // Draw arrows from player province to adjacent reachable provinces
    if(g_campaign.playerProvince<0||g_campaign.playerProvince>=(int)g_campaign.provinces.size()) return;
    Province& pp=g_campaign.provinces[g_campaign.playerProvince];
    for(int adj:pp.adjacent){
        if(adj<0||adj>=(int)g_campaign.provinces.size()) continue;
        Province& ap=g_campaign.provinces[adj];
        Vector2 dir=vnorm(v2sub(ap.center,pp.center));
        Vector2 mid={pp.center.x+dir.x*40.f,pp.center.y+dir.y*40.f};
        // Small arrow pointing toward adjacent
        Color arrowCol=factionIsPlayerSide(ap.owner)?C_ALLY:C_ENEMY_COL; // Fase D+
        DrawLineEx(mid,v2add(mid,v2scale(dir,24.f)),2.f,{arrowCol.r,arrowCol.g,arrowCol.b,120});
        // Arrowhead
        Vector2 tip=v2add(mid,v2scale(dir,24.f));
        Vector2 perp={-dir.y,dir.x};
        DrawTriangle(tip,
            v2sub(v2sub(tip,v2scale(dir,8.f)),v2scale(perp,5.f)),
            v2add(v2sub(tip,v2scale(dir,8.f)),v2scale(perp,5.f)),
            {arrowCol.r,arrowCol.g,arrowCol.b,100});
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  CAMPAIGN MAP GENERATION (multiple campaigns)
// ═══════════════════════════════════════════════════════════════════════════
struct ProvinceTemplate {
    const char* name; float nx,ny; TerrainType terrain; FactionId owner; bool hasCity;
    const char* cityName;
};

// Fase J: cada facción IA levanta un ejército de campo desde su provincia con
// mayor guarnición (el primer grupo se transfiere del garrison al ejército)
static void spawnAIArmies(){
    g_campaign.armies.clear();
    g_campaign.nextArmyId=1;
    for(int f=0;f<FACTION_COUNT;f++){
        if(f==FACTION_PLAYER||f==FACTION_NEUTRAL) continue;
        int best=-1,bestStr=-1;
        for(int i=0;i<(int)g_campaign.provinces.size();i++){
            Province& p=g_campaign.provinces[i];
            if(p.owner!=(FactionId)f) continue;
            int s=0; for(auto& g:p.army) s+=g.second;
            if(s>bestStr){ bestStr=s; best=i; }
        }
        if(best<0||bestStr<=0||g_campaign.provinces[best].army.empty()) continue;
        FieldArmy a;
        a.id=g_campaign.nextArmyId++;
        a.owner=(FactionId)f;
        a.province=best;
        a.units.push_back(g_campaign.provinces[best].army.front());
        g_campaign.provinces[best].army.erase(g_campaign.provinces[best].army.begin());
        g_campaign.armies.push_back(a);
    }
}

void generateCampaignMap(int campaignId){
    g_campaign.provinces.clear();
    g_campaign.campaignId = campaignId;
    int sw = SCREEN_W>0 ? SCREEN_W : 1280;
    int sh = SCREEN_H>0 ? SCREEN_H : 768;

    if(campaignId==0){
        // Campaign 0: "The Realm" — original 14 provinces
        static const ProvinceTemplate tpls[]={
            {"Aldenmoor",     0.18f,0.42f, TERRAIN_PLAIN,    FACTION_PLAYER,     true,  "Aldenmoor"},
            {"Greenvale",     0.30f,0.28f, TERRAIN_FOREST,   FACTION_PLAYER,     true,  "Greenvale"},
            {"Ironholt",      0.28f,0.62f, TERRAIN_MOUNTAIN, FACTION_NEUTRAL,    true,  "Ironholt"},
            {"Saltmere",      0.14f,0.72f, TERRAIN_COAST,    FACTION_NEUTRAL,    true,  "Saltmere"},
            {"Dustfield",     0.42f,0.50f, TERRAIN_PLAIN,    FACTION_NEUTRAL,    false, ""},
            {"Ashford",       0.52f,0.32f, TERRAIN_FOREST,   FACTION_AGGRESSIVE, true,  "Ashford"},
            {"Cragspire",     0.60f,0.18f, TERRAIN_MOUNTAIN, FACTION_AGGRESSIVE, true,  "Cragspire"},
            {"Redmarch",      0.65f,0.48f, TERRAIN_PLAIN,    FACTION_AGGRESSIVE, true,  "Redmarch"},
            {"Blackfen",      0.50f,0.68f, TERRAIN_FOREST,   FACTION_DEFENSIVE,  true,  "Blackfen"},
            {"Stonewall",     0.72f,0.65f, TERRAIN_MOUNTAIN, FACTION_DEFENSIVE,  true,  "Stonewall"},
            {"Harborgate",    0.35f,0.85f, TERRAIN_COAST,    FACTION_COMMERCIAL, true,  "Harborgate"},
            {"Goldenport",    0.55f,0.82f, TERRAIN_COAST,    FACTION_COMMERCIAL, true,  "Goldenport"},
            {"Thornwood",     0.78f,0.35f, TERRAIN_FOREST,   FACTION_AGGRESSIVE, false, ""},
            {"Highpass",      0.82f,0.52f, TERRAIN_MOUNTAIN, FACTION_DEFENSIVE,  false, ""},
        };
        static const int adjData[][2]={
            {0,1},{0,2},{0,3},{1,4},{1,5},{2,3},{2,4},{2,8},
            {3,10},{4,5},{4,7},{4,8},{5,6},{5,7},{6,7},{6,12},
            {7,8},{7,9},{8,10},{8,11},{9,11},{9,13},{10,11},{12,13}
        };
        int n=14;
        for(int i=0;i<n;i++){
            const ProvinceTemplate& tp=tpls[i];
            Province p{};
            strncpy(p.name,tp.name,sizeof(p.name)-1); p.name[sizeof(p.name)-1]='\0';
            p.nx=tp.nx; p.ny=tp.ny;
            p.center={tp.nx*(float)sw, tp.ny*(float)sh};
            p.terrain=tp.terrain; p.owner=tp.owner; p.hasCity=tp.hasCity;
        if(tp.hasCity){
            initCity(p.city,tp.cityName,tp.owner);
        }
        p.army.clear();
        // Add enemy armies
        if(tp.owner==FACTION_AGGRESSIVE){
            p.army.push_back({0,40}); // Spear Levy x40
            p.army.push_back({1,30}); // Axe Militia x30
        } else if(tp.owner==FACTION_DEFENSIVE){
            p.army.push_back({0,50});
            p.army.push_back({4,20}); // Bow Levy
        } else if(tp.owner==FACTION_COMMERCIAL){
            p.army.push_back({0,30});
        }
        g_campaign.provinces.push_back(p);
        }
        for(auto& pr:g_campaign.provinces) pr.adjacent.clear();
        for(auto& ad:adjData){
            if(ad[0]<n&&ad[1]<n){
                g_campaign.provinces[ad[0]].adjacent.push_back(ad[1]);
                g_campaign.provinces[ad[1]].adjacent.push_back(ad[0]);
            }
        }
    } else if(campaignId==1){
        // Campaign 1: "Northern Isles" — 10 provinces, island layout
        static const ProvinceTemplate tpls[]={
            {"Frostholm",   0.22f,0.35f, TERRAIN_MOUNTAIN, FACTION_PLAYER,   true,  "Frostholm"},
            {"Icewind",     0.38f,0.22f, TERRAIN_PLAIN,    FACTION_PLAYER,   true,  "Icewind"},
            {"Northgate",   0.55f,0.28f, TERRAIN_FOREST,   FACTION_NEUTRAL,  true,  "Northgate"},
            {"Stormhaven",  0.72f,0.40f, TERRAIN_COAST,    FACTION_AGGRESSIVE, true, "Stormhaven"},
            {"Mistwood",    0.48f,0.55f, TERRAIN_FOREST,   FACTION_NEUTRAL,  false, ""},
            {"Cinderfell",  0.28f,0.62f, TERRAIN_PLAIN,    FACTION_AGGRESSIVE, true, "Cinderfell"},
            {"Saltmarsh",   0.15f,0.78f, TERRAIN_COAST,   FACTION_COMMERCIAL, true, "Saltmarsh"},
            {"Boulder",     0.62f,0.68f, TERRAIN_MOUNTAIN, FACTION_DEFENSIVE, true, "Boulder"},
            {"Grayvale",    0.80f,0.72f, TERRAIN_FOREST,   FACTION_DEFENSIVE, false, ""},
            {"Port Snow",   0.42f,0.82f, TERRAIN_COAST,    FACTION_NEUTRAL,  true,  "Port Snow"},
        };
        static const int adjData[][2]={
            {0,1},{0,4},{0,5},{1,2},{1,4},{2,3},{2,4},{2,7},{3,7},{4,5},{4,7},{4,9},{5,6},{5,9},{6,9},{7,8},{8,9}
        };
        int n=10;
        for(int i=0;i<n;i++){
            const ProvinceTemplate& tp=tpls[i];
            Province p{};
            strncpy(p.name,tp.name,sizeof(p.name)-1); p.name[sizeof(p.name)-1]='\0';
            p.nx=tp.nx; p.ny=tp.ny;
            p.center={tp.nx*(float)sw, tp.ny*(float)sh};
            p.terrain=tp.terrain; p.owner=tp.owner; p.hasCity=tp.hasCity;
            if(tp.hasCity){
                initCity(p.city,tp.cityName,tp.owner);
            }
            p.army.clear();
            if(tp.owner==FACTION_AGGRESSIVE){ p.army.push_back({0,40}); p.army.push_back({1,30}); }
            else if(tp.owner==FACTION_DEFENSIVE){ p.army.push_back({0,50}); p.army.push_back({4,20}); }
            else if(tp.owner==FACTION_COMMERCIAL){ p.army.push_back({0,30}); }
            g_campaign.provinces.push_back(p);
        }
        for(auto& pr:g_campaign.provinces) pr.adjacent.clear();
        for(auto& ad:adjData){
            if(ad[0]<n&&ad[1]<n){
                g_campaign.provinces[ad[0]].adjacent.push_back(ad[1]);
                g_campaign.provinces[ad[1]].adjacent.push_back(ad[0]);
            }
        }
    } else if(campaignId==2){
        // Campaign 2: "Eastern March" — 12 provinces
        static const ProvinceTemplate tpls[]={
            {"Riverdale",   0.20f,0.45f, TERRAIN_PLAIN,    FACTION_PLAYER,   true,  "Riverdale"},
            {"Oakshire",    0.35f,0.32f, TERRAIN_FOREST,  FACTION_PLAYER,   true,  "Oakshire"},
            {"Eastfort",    0.52f,0.38f, TERRAIN_MOUNTAIN, FACTION_NEUTRAL,  true,  "Eastfort"},
            {"Seabridge",   0.68f,0.50f, TERRAIN_COAST,   FACTION_AGGRESSIVE, true, "Seabridge"},
            {"Wheatfield",  0.42f,0.58f, TERRAIN_PLAIN,   FACTION_NEUTRAL,  false, ""},
            {"Blackwood",   0.58f,0.65f, TERRAIN_FOREST,  FACTION_DEFENSIVE, true, "Blackwood"},
            {"Cragdale",    0.75f,0.72f, TERRAIN_MOUNTAIN, FACTION_DEFENSIVE, true, "Cragdale"},
            {"Sandport",    0.28f,0.78f, TERRAIN_COAST,   FACTION_COMMERCIAL, true, "Sandport"},
            {"Millbrook",   0.12f,0.55f, TERRAIN_PLAIN,   FACTION_NEUTRAL,  true,  "Millbrook"},
            {"Pinewatch",   0.82f,0.35f, TERRAIN_FOREST,  FACTION_AGGRESSIVE, false, ""},
            {"Ironford",    0.45f,0.82f, TERRAIN_PLAIN,   FACTION_NEUTRAL,  false, ""},
            {"Highcliff",   0.65f,0.22f, TERRAIN_MOUNTAIN, FACTION_AGGRESSIVE, true, "Highcliff"},
        };
        static const int adjData[][2]={
            {0,1},{0,4},{0,8},{1,2},{1,4},{2,3},{2,4},{2,9},{2,11},{3,6},{3,9},{4,5},{4,10},{5,6},{5,10},{6,9},{7,8},{7,10},{8,10}
        };
        int n=12;
        for(int i=0;i<n;i++){
            const ProvinceTemplate& tp=tpls[i];
            Province p{};
            strncpy(p.name,tp.name,sizeof(p.name)-1); p.name[sizeof(p.name)-1]='\0';
            p.nx=tp.nx; p.ny=tp.ny;
            p.center={tp.nx*(float)sw, tp.ny*(float)sh};
            p.terrain=tp.terrain; p.owner=tp.owner; p.hasCity=tp.hasCity;
            if(tp.hasCity){
                initCity(p.city,tp.cityName,tp.owner);
            }
            p.army.clear();
            if(tp.owner==FACTION_AGGRESSIVE){ p.army.push_back({0,40}); p.army.push_back({1,30}); }
            else if(tp.owner==FACTION_DEFENSIVE){ p.army.push_back({0,50}); p.army.push_back({4,20}); }
            else if(tp.owner==FACTION_COMMERCIAL){ p.army.push_back({0,30}); }
            g_campaign.provinces.push_back(p);
        }
        for(auto& pr:g_campaign.provinces) pr.adjacent.clear();
        for(auto& ad:adjData){
            if(ad[0]<n&&ad[1]<n){
                g_campaign.provinces[ad[0]].adjacent.push_back(ad[1]);
                g_campaign.provinces[ad[1]].adjacent.push_back(ad[0]);
            }
        }
    } else if(campaignId==3){
        // Campaign 3: "Great Continent" — 32 provincias (rejilla 6x6 sin
        // esquinas), 4a faccion (Sable Fleet, piratas) + 13 neutrales.
        // Vecindad automatica: 8-conectividad de la rejilla (Chebyshev <= 1).
        static const ProvinceTemplate tpls[]{
            // Player (oeste-centro)
            {"Westhold",     0.090f,0.414f, TERRAIN_PLAIN,    FACTION_PLAYER,    true,  "Westhold"},
            {"Southwick",    0.090f,0.556f, TERRAIN_FOREST,   FACTION_PLAYER,    true,  "Southwick"},
            {"Elmcrest",     0.252f,0.414f, TERRAIN_PLAIN,    FACTION_PLAYER,    true,  "Elmcrest"},
            // Sable Fleet (piratas, costa oeste y sur)
            {"Blacktide",    0.090f,0.272f, TERRAIN_COAST,    FACTION_PIRATE,    true,  "Blacktide"},
            {"Murkwater",    0.090f,0.698f, TERRAIN_COAST,    FACTION_PIRATE,    true,  "Murkwater"},
            {"Reefhold",     0.252f,0.698f, TERRAIN_COAST,    FACTION_PIRATE,    true,  "Reefhold"},
            {"Corsair Bay",  0.414f,0.840f, TERRAIN_COAST,    FACTION_PIRATE,    true,  "Corsair Bay"},
            // Iron Pact (norte)
            {"Frostspire",   0.252f,0.150f, TERRAIN_MOUNTAIN, FACTION_AGGRESSIVE,true,  "Frostspire"},
            {"Wolfheim",     0.414f,0.150f, TERRAIN_PLAIN,    FACTION_AGGRESSIVE,true,  "Wolfheim"},
            {"Grimwall",     0.576f,0.150f, TERRAIN_FOREST,   FACTION_AGGRESSIVE,true,  "Grimwall"},
            {"Redcliff",     0.414f,0.272f, TERRAIN_PLAIN,    FACTION_AGGRESSIVE,true,  "Redcliff"},
            // Stone Realm (este montañoso)
            {"Bastion",      0.900f,0.272f, TERRAIN_MOUNTAIN, FACTION_DEFENSIVE, true,  "Bastion"},
            {"Highwall",     0.900f,0.414f, TERRAIN_MOUNTAIN, FACTION_DEFENSIVE, true,  "Highwall"},
            {"Deepdelve",    0.900f,0.556f, TERRAIN_MOUNTAIN, FACTION_DEFENSIVE, true,  "Deepdelve"},
            {"Rockford",     0.738f,0.556f, TERRAIN_MOUNTAIN, FACTION_DEFENSIVE, true,  "Rockford"},
            // Trade Republic (sureste portuario)
            {"Silverport",   0.576f,0.840f, TERRAIN_COAST,    FACTION_COMMERCIAL,true,  "Silverport"},
            {"Emerald Quay", 0.738f,0.840f, TERRAIN_COAST,    FACTION_COMMERCIAL,true,  "Emerald Quay"},
            {"Tradehold",    0.738f,0.698f, TERRAIN_COAST,    FACTION_COMMERCIAL,true,  "Tradehold"},
            {"Coinford",     0.900f,0.698f, TERRAIN_COAST,    FACTION_COMMERCIAL,true,  "Coinford"},
            // Neutrales (13)
            {"Northwatch",   0.738f,0.170f, TERRAIN_FOREST,   FACTION_NEUTRAL,   true,  "Northwatch"},
            {"Pinehall",     0.252f,0.272f, TERRAIN_FOREST,   FACTION_NEUTRAL,   true,  "Pinehall"},
            {"Lakeford",     0.576f,0.272f, TERRAIN_PLAIN,    FACTION_NEUTRAL,   false, ""},
            {"Stonecross",   0.738f,0.272f, TERRAIN_MOUNTAIN, FACTION_NEUTRAL,   false, ""},
            {"Midborough",   0.414f,0.414f, TERRAIN_PLAIN,    FACTION_NEUTRAL,   true,  "Midborough"},
            {"Oakden",       0.576f,0.414f, TERRAIN_FOREST,   FACTION_NEUTRAL,   true,  "Oakden"},
            {"Ironwood",     0.738f,0.414f, TERRAIN_FOREST,   FACTION_NEUTRAL,   false, ""},
            {"Willowmere",   0.252f,0.556f, TERRAIN_PLAIN,    FACTION_NEUTRAL,   true,  "Willowmere"},
            {"Crosskeep",    0.414f,0.556f, TERRAIN_MOUNTAIN, FACTION_NEUTRAL,   true,  "Crosskeep"},
            {"Duskhollow",   0.576f,0.556f, TERRAIN_FOREST,   FACTION_NEUTRAL,   true,  "Duskhollow"},
            {"Sandgate",     0.414f,0.698f, TERRAIN_PLAIN,    FACTION_NEUTRAL,   false, ""},
            {"Brightwell",   0.576f,0.698f, TERRAIN_PLAIN,    FACTION_NEUTRAL,   false, ""},
            {"Tidewatch",    0.252f,0.840f, TERRAIN_COAST,    FACTION_NEUTRAL,   false, ""},
        };
        int n=32;
        for(int i=0;i<n;i++){
            const ProvinceTemplate& tp=tpls[i];
            Province p{};
            strncpy(p.name,tp.name,sizeof(p.name)-1); p.name[sizeof(p.name)-1]='\0';
            p.nx=tp.nx; p.ny=tp.ny;
            p.center={tp.nx*(float)sw, tp.ny*(float)sh};
            p.terrain=tp.terrain; p.owner=tp.owner; p.hasCity=tp.hasCity;
            if(tp.hasCity){
                initCity(p.city,tp.cityName,tp.owner);
                if(tp.owner==FACTION_PIRATE){ p.city.built[BLD_FARM]=true; p.city.count[BLD_FARM]++; p.city.built[BLD_BARRACKS]=true; p.city.count[BLD_BARRACKS]++; }
            }
            p.army.clear();
            if(tp.owner==FACTION_AGGRESSIVE){ p.army.push_back({0,40}); p.army.push_back({1,30}); }
            else if(tp.owner==FACTION_DEFENSIVE){ p.army.push_back({0,50}); p.army.push_back({4,20}); }
            else if(tp.owner==FACTION_COMMERCIAL){ p.army.push_back({0,30}); }
            else if(tp.owner==FACTION_PIRATE){ p.army.push_back({0,30}); p.army.push_back({4,20}); }
            g_campaign.provinces.push_back(p);
        }
        // Vecindad: 8-conectividad de la rejilla (gx,gy = indices enteros)
        auto gxi=[&](int i){ return (int)lroundf((tpls[i].nx-0.090f)/0.162f); };
        auto gyi=[&](int i){ return (int)lroundf((tpls[i].ny-0.130f)/0.142f); };
        for(auto& pr:g_campaign.provinces) pr.adjacent.clear();
        for(int i=0;i<n;i++) for(int j=i+1;j<n;j++){
            int dx=gxi(i)-gxi(j); if(dx<0) dx=-dx;
            int dy=gyi(i)-gyi(j); if(dy<0) dy=-dy;
            if(dx<=1&&dy<=1){
                g_campaign.provinces[i].adjacent.push_back(j);
                g_campaign.provinces[j].adjacent.push_back(i);
            }
        }
    } else if(campaignId==4){
        // Campaign 4: "Europa" — 36 provincias con silueta realista de Europa.
        // Posiciones aproximadas de ciudades europeas reales (x=oeste→este,
        // y=norte→sur). Vecindad: pares manuales + radio-based automático.
        static const ProvinceTemplate tpls[]{
            // Ibérica (suroeste)
            {"Lisboa",     0.070f,0.720f, TERRAIN_COAST,    FACTION_NEUTRAL,   true,  "Lisboa"},
            {"Madrid",     0.140f,0.680f, TERRAIN_PLAIN,    FACTION_NEUTRAL,   true,  "Madrid"},
            {"Sevilla",    0.100f,0.780f, TERRAIN_PLAIN,    FACTION_NEUTRAL,   false, ""},
            {"Barcelona",  0.210f,0.660f, TERRAIN_COAST,    FACTION_NEUTRAL,   true,  "Barcelona"},
            // Francia (jugador)
            {"Paris",      0.270f,0.460f, TERRAIN_PLAIN,    FACTION_PLAYER,    true,  "Paris"},
            {"Lyon",       0.290f,0.580f, TERRAIN_MOUNTAIN, FACTION_PLAYER,    true,  "Lyon"},
            {"Bordeaux",   0.210f,0.560f, TERRAIN_COAST,    FACTION_PLAYER,    true,  "Bordeaux"},
            {"Marsella",   0.310f,0.620f, TERRAIN_COAST,    FACTION_PLAYER,    false, ""},
            // Islas Británicas (como provincia costera unida al continente)
            {"Londres",    0.220f,0.330f, TERRAIN_PLAIN,    FACTION_NEUTRAL,   true,  "Londres"},
            {"Edimburgo",  0.200f,0.220f, TERRAIN_FOREST,   FACTION_NEUTRAL,   false, ""},
            // Países Bajos / Bélgica
            {"Ámsterdam",  0.320f,0.360f, TERRAIN_COAST,    FACTION_COMMERCIAL,true,  "Ámsterdam"},
            {"Bruselas",   0.300f,0.400f, TERRAIN_PLAIN,    FACTION_NEUTRAL,   false, ""},
            // Alemania (agresivo - Iron Pact)
            {"Berlín",     0.450f,0.340f, TERRAIN_PLAIN,    FACTION_AGGRESSIVE,true,  "Berlín"},
            {"Fráncfort",  0.380f,0.420f, TERRAIN_FOREST,   FACTION_AGGRESSIVE,true,  "Fráncfort"},
            {"Múnich",     0.400f,0.520f, TERRAIN_MOUNTAIN, FACTION_AGGRESSIVE,true,  "Múnich"},
            {"Hamburgo",   0.420f,0.300f, TERRAIN_COAST,    FACTION_NEUTRAL,   false, ""},
            // Escandinavia (piratas - Vikings)
            {"Oslo",       0.400f,0.160f, TERRAIN_MOUNTAIN, FACTION_PIRATE,    true,  "Oslo"},
            {"Estocolmo",  0.500f,0.140f, TERRAIN_FOREST,   FACTION_PIRATE,    true,  "Estocolmo"},
            {"Copenhague", 0.420f,0.250f, TERRAIN_COAST,    FACTION_PIRATE,    false, ""},
            {"Bergen",     0.350f,0.120f, TERRAIN_COAST,    FACTION_NEUTRAL,   false, ""},
            // Polonia
            {"Varsovia",   0.550f,0.340f, TERRAIN_PLAIN,    FACTION_NEUTRAL,   true,  "Varsovia"},
            {"Cracovia",   0.540f,0.420f, TERRAIN_MOUNTAIN, FACTION_NEUTRAL,   false, ""},
            // Austria-Hungría (defensivo - Stone Realm)
            {"Viena",      0.480f,0.480f, TERRAIN_PLAIN,    FACTION_DEFENSIVE, true,  "Viena"},
            {"Budapest",   0.530f,0.500f, TERRAIN_PLAIN,    FACTION_DEFENSIVE, true,  "Budapest"},
            {"Praga",      0.460f,0.420f, TERRAIN_FOREST,   FACTION_NEUTRAL,   false, ""},
            // Balcanes
            {"Belgrado",   0.540f,0.560f, TERRAIN_MOUNTAIN, FACTION_NEUTRAL,   true,  "Belgrado"},
            {"Zagreb",     0.480f,0.540f, TERRAIN_FOREST,   FACTION_NEUTRAL,   false, ""},
            {"Sofía",      0.600f,0.620f, TERRAIN_MOUNTAIN, FACTION_NEUTRAL,   false, ""},
            {"Atenas",     0.580f,0.760f, TERRAIN_COAST,    FACTION_NEUTRAL,   true,  "Atenas"},
            // Italia (comercial - Trade Republic)
            {"Milán",      0.350f,0.560f, TERRAIN_PLAIN,    FACTION_COMMERCIAL,true,  "Milán"},
            {"Venecia",    0.400f,0.560f, TERRAIN_COAST,    FACTION_COMMERCIAL,true,  "Venecia"},
            {"Roma",       0.400f,0.700f, TERRAIN_PLAIN,    FACTION_NEUTRAL,   true,  "Roma"},
            {"Nápoles",    0.420f,0.760f, TERRAIN_COAST,    FACTION_NEUTRAL,   false, ""},
            // Europa del Este / Rusia
            {"Kiev",       0.680f,0.360f, TERRAIN_PLAIN,    FACTION_NEUTRAL,   true,  "Kiev"},
            {"Moscú",      0.800f,0.240f, TERRAIN_FOREST,   FACTION_NEUTRAL,   true,  "Moscú"},
            {"San Petersburgo",0.740f,0.140f,TERRAIN_COAST, FACTION_NEUTRAL,   false, ""},
        };
        int n=36;
        for(int i=0;i<n;i++){
            const ProvinceTemplate& tp=tpls[i];
            Province p{};
            strncpy(p.name,tp.name,sizeof(p.name)-1); p.name[sizeof(p.name)-1]='\0';
            p.nx=tp.nx; p.ny=tp.ny;
            p.center={tp.nx*(float)sw, tp.ny*(float)sh};
            p.terrain=tp.terrain; p.owner=tp.owner; p.hasCity=tp.hasCity;
            if(tp.hasCity) initCity(p.city,tp.cityName,tp.owner);
            p.army.clear();
            if(tp.owner==FACTION_AGGRESSIVE){ p.army.push_back({0,40}); p.army.push_back({1,30}); }
            else if(tp.owner==FACTION_DEFENSIVE){ p.army.push_back({0,50}); p.army.push_back({4,20}); }
            else if(tp.owner==FACTION_COMMERCIAL){ p.army.push_back({0,30}); }
            else if(tp.owner==FACTION_PIRATE){ p.army.push_back({0,30}); p.army.push_back({4,20}); }
            g_campaign.provinces.push_back(p);
        }
        // Vecindad: pares manuales de rutas terrestres europeas + automática
        // por distancia (cualquier par < 0.18 en espacio normalizado es adyacente)
        static const int adjData[][2]={
            // Ibérica
            {0,1},{0,2},{1,2},{1,3},{2,3},
            // Francia
            {4,5},{4,6},{4,8},{4,10},{4,11},{5,6},{5,7},{5,14},{6,7},{6,3},
            // Bélgica/Alemania
            {10,12},{10,13},{11,13},{12,13},{12,15},{12,24},{13,14},{13,24},
            {14,22},{14,23},{14,24},{15,12},
            // Escandinavia
            {16,17},{16,18},{16,19},{17,18},{17,35},
            // Polonia
            {20,12},{20,21},{20,22},{21,22},{21,25},
            // Austria-Hungría
            {22,23},{22,24},{22,25},{23,25},{23,26},{24,26},{24,29},{24,30},
            // Balcanes
            {25,26},{25,27},{25,28},{26,27},{26,30},{27,28},{27,30},{28,32},
            // Italia
            {29,30},{29,31},{30,31},{31,32},
            // Europa del Este
            {33,20},{33,34},{34,35},{33,21},
        };
        for(auto& pr:g_campaign.provinces) pr.adjacent.clear();
        for(auto& ad:adjData){
            if(ad[0]<n&&ad[1]<n){
                g_campaign.provinces[ad[0]].adjacent.push_back(ad[1]);
                g_campaign.provinces[ad[1]].adjacent.push_back(ad[0]);
            }
        }
        // Adicional: conectar provincias costeras cercanas (distancia < 0.18)
        // que no estén ya conectadas (cubre bordes del mapa)
        for(int i=0;i<n;i++) for(int j=i+1;j<n;j++){
            float dx=tpls[i].nx-tpls[j].nx, dy=tpls[i].ny-tpls[j].ny;
            if(dx*dx+dy*dy < 0.18f*0.18f){
                bool found=false;
                for(int a:g_campaign.provinces[i].adjacent) if(a==j) found=true;
                if(!found){
                    g_campaign.provinces[i].adjacent.push_back(j);
                    g_campaign.provinces[j].adjacent.push_back(i);
                }
            }
        }
    }

    // Fase J: ejércitos de campo IA
    spawnAIArmies();
}

void updateProvinceCenters(){
    for(int i=0;i<(int)g_campaign.provinces.size();i++){
        Province& p=g_campaign.provinces[i];
        p.center={p.nx*(float)SCREEN_W, p.ny*(float)SCREEN_H};
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  NEW CAMPAIGN INIT
// ═══════════════════════════════════════════════════════════════════════════
void newCampaign(int campaignId){
    if(campaignId<0||campaignId>=MAX_CAMPAIGNS) campaignId=0;
    g_campaign=CampaignState{};
    g_campaign.campaignId=campaignId;
    g_campaign.res.gold=500.f;
    g_campaign.res.food=200.f;
    g_campaign.res.wood=100.f;
    g_campaign.res.stone=50.f;
    g_campaign.res.iron=50.f;
    g_campaign.playerProvince=0;
    generateCampaignMap(campaignId);
    updateProvinceCenters();
    // Fase J: dos ejércitos iniciales — el Rey comanda el primero, un General
    // libre el segundo. Sin general no se pueden formar ejércitos.
    {
        FieldArmy a1;
        a1.id=g_campaign.nextArmyId++;
        a1.owner=FACTION_PLAYER;
        a1.province=0;
        a1.units.push_back({UNIT_KING,1});
        a1.units.push_back({0,60});
        a1.units.push_back({0,60});
        FieldArmy a2;
        a2.id=g_campaign.nextArmyId++;
        a2.owner=FACTION_PLAYER;
        a2.province=0;
        a2.units.push_back({UNIT_GENERAL,1});
        a2.units.push_back({4,40}); // Bow levy
        g_campaign.armies.push_back(a1);
        g_campaign.armies.push_back(a2);
        selectArmy(0);
        g_campaign.playerArmy=g_campaign.readyUnits; // espejo legacy
    }
    g_campaign.pendingBattleProvince=-1;
    g_campaign.turn=1;
}

// ───────────────────────────────────────────────────────────────────────────
//  Fase J: marcha IA — 1er paso BFS por territorio propio hacia una provincia
//  propia fronteriza con el jugador (-1 si no hay camino)
// ───────────────────────────────────────────────────────────────────────────
static int bfsOwnStepToPlayer(int start, FactionId owner){
    int np=(int)g_campaign.provinces.size();
    if(start<0||start>=np) return -1;
    auto isGoal=[&](int i)->bool{
        for(int adj:g_campaign.provinces[i].adjacent){
            if(adj<0||adj>=np) continue;
            if(g_campaign.provinces[adj].owner==FACTION_PLAYER) return true;
        }
        return false;
    };
    std::vector<int> parent(np,-1);
    std::deque<int> q;
    q.push_back(start); parent[start]=start;
    int found=-1;
    while(!q.empty()){
        int cur=q.front(); q.pop_front();
        if(cur!=start&&isGoal(cur)){ found=cur; break; }
        for(int nx:g_campaign.provinces[cur].adjacent){
            if(nx<0||nx>=np) continue;
            if(parent[nx]!=-1) continue;
            if(g_campaign.provinces[nx].owner!=owner) continue;
            parent[nx]=cur;
            q.push_back(nx);
        }
    }
    if(found<0) return -1;
    int step=found;
    while(step!=start&&parent[step]!=start) step=parent[step];
    return parent[step]==start?step:-1;
}

// ═══════════════════════════════════════════════════════════════════════════
//  TURN PROCESSING (centralized) — Fase J: devuelve true si hay batalla pendiente
// ═══════════════════════════════════════════════════════════════════════════
bool processTurn(){
    Resources& r=g_campaign.res;

    // Advance recruitment queue
    for(auto& e:g_campaign.recruitQueue){
        e.turnsLeft--;
        if(e.turnsLeft<=0){
            int cnt=e.typeIdx<(int)g_unitTypes.size()?g_unitTypes[e.typeIdx].soldierCount:40;
            // PvP: enrutar a la reserva de la facción que reclutó
            if(e.faction==FACTION_PLAYER||!g_campaign.pvpMode)
                g_campaign.reserve.push_back({e.typeIdx,cnt});
            else
                g_campaign.clientReserve.push_back({e.typeIdx,cnt});
            battleLogAdd(TextFormat("[T%d] %s recruitment complete!", g_campaign.turn, g_unitTypes[e.typeIdx].name));
        }
    }
    // Remove completed recruitment
    g_campaign.recruitQueue.erase(
        std::remove_if(g_campaign.recruitQueue.begin(), g_campaign.recruitQueue.end(),
            [](const RecruitEntry& e){ return e.turnsLeft<=0; }),
        g_campaign.recruitQueue.end()
    );

    // Resource generation from ALL player cities
    for(int pi=0;pi<(int)g_campaign.provinces.size();pi++){
        Province& prov=g_campaign.provinces[pi];
        // PvP: income para host (PLAYER) y para la faccion del cliente (clientRes)
        Resources* dest=nullptr;
        if(prov.owner==FACTION_PLAYER) dest=&r;
        else if(g_campaign.pvpMode&&prov.owner==g_campaign.clientFaction) dest=&g_campaign.clientRes;
        if(!dest||!prov.hasCity) continue;
        City& c=prov.city;
        for(int b=0;b<BLD_COUNT;b++){
            if(!c.built[b]) continue;
            int n=(int)c.count[b]; if(n<1) n=1; // v11: ingreso x nº de copias
            dest->gold+=bldGoldYield[b]*(float)n;
            dest->food+=bldFoodYield[b]*(float)n;
            dest->wood+=bldWoodYield[b]*(float)n;
            dest->stone+=bldStoneYield[b]*(float)n;
            dest->iron+=bldIronYield[b]*(float)n;
        }
        // Advance construction
        if(c.constructing>=0){
            int bt=c.constructing;
            if(bt<0||bt>=BLD_COUNT){
                c.constructing=-1; c.constructSlot=-1;
                continue;
            }
            c.constructTurns--;
            if(c.constructTurns<=0){
                c.built[bt]=true;
                c.count[bt]=(unsigned char)std::min(255,(int)c.count[bt]+1);
                // v11: colocar en la matriz (murallas van al perímetro, no al grid)
                if(bt!=BLD_WALLS1&&bt!=BLD_WALLS2){
                    int slot=c.constructSlot;
                    if(slot>=0&&slot<16&&c.grid[slot]<0) c.grid[slot]=bt;
                    else { for(int s=0;s<16;s++) if(c.grid[s]<0){ c.grid[s]=bt; break; } }
                }
                if(bt==BLD_WALLS1) c.defBonus=std::max(c.defBonus,1.15f);
                if(bt==BLD_WALLS2) c.defBonus=std::max(c.defBonus,1.35f);
                battleLogAdd(TextFormat("[T%d] %s: %s completed!", g_campaign.turn, prov.name, bldNames[bt]));
                c.constructing=-1;
                c.constructSlot=-1;
            }
        }
    }

    // Maintenance from all units (Fase J: ejércitos del jugador + reserva;
    // readyUnits es una vista del seleccionado y NO se suma para no duplicar)
    auto applyMaint=[&](const std::vector<std::pair<int,int>>& us){
        for(auto [ti,cnt]:us){
            if(ti>=unitTypeCount()) continue;
            const UnitTypeDef& td=g_unitTypes[ti];
            if(td.soldierCount<=0) continue;
            r.gold-=td.maintGold*(cnt/(float)td.soldierCount);
            r.food-=td.maintFood*(cnt/(float)td.soldierCount);
        }
    };
    for(auto& a:g_campaign.armies)
        if(a.owner==FACTION_PLAYER) applyMaint(a.units);
    applyMaint(g_campaign.reserve);
    // PvP: mantenimiento de las tropas del cliente (descuenta de clientRes)
    if(g_campaign.pvpMode){
        Resources& cr=g_campaign.clientRes;
        auto applyMaintC=[&](const std::vector<std::pair<int,int>>& us){
            for(auto [ti,cnt]:us){
                if(ti>=unitTypeCount()) continue;
                const UnitTypeDef& td=g_unitTypes[ti];
                if(td.soldierCount<=0) continue;
                cr.gold-=td.maintGold*(cnt/(float)td.soldierCount);
                cr.food-=td.maintFood*(cnt/(float)td.soldierCount);
            }
        };
        for(auto& a:g_campaign.armies)
            if(a.owner==g_campaign.clientFaction) applyMaintC(a.units);
        applyMaintC(g_campaign.clientReserve);
        cr.gold=std::max(0.f,cr.gold);
        cr.food=std::max(0.f,cr.food);
    }

    // Clamp resources (don't go negative)
    r.gold=std::max(0.f,r.gold);
    r.food=std::max(0.f,r.food);
    r.wood=std::max(0.f,r.wood);
    r.stone=std::max(0.f,r.stone);
    r.iron=std::max(0.f,r.iron);

    // Enemy AI and actions
    float aggression=0.05f+0.02f*(float)g_campaign.turn/10.f;
    aggression=std::min(aggression,0.4f);
    static const float diffMult[3]={0.7f,1.0f,1.4f};
    aggression*=diffMult[std::max(0,std::min(2,g_settings.difficulty))];

    // Coalition detection
    std::vector<int> coalitionAttackers;
    for(int pi2=0;pi2<(int)g_campaign.provinces.size();pi2++){
        Province& ep2=g_campaign.provinces[pi2];
        // Fase I: aliados militares del jugador no coalicionan contra el jugador
        // PvP: la faccion humana del cliente tampoco es objetivo de coalicion IA
        if(ep2.owner==FACTION_NEUTRAL||factionIsHuman(ep2.owner)
           ||factionIsPlayerSide(ep2.owner)) continue;
        bool adjPlayer=false;
        for(int adj2:ep2.adjacent) if(g_campaign.provinces[adj2].owner==FACTION_PLAYER) adjPlayer=true;
        if(!adjPlayer) continue;
        for(int adj2:ep2.adjacent){
            Province& ep3=g_campaign.provinces[adj2];
            if(ep3.owner!=ep2.owner) continue;
            bool partnerAdjPlayer=false;
            for(int adj3:ep3.adjacent) if(g_campaign.provinces[adj3].owner==FACTION_PLAYER) partnerAdjPlayer=true;
            if(partnerAdjPlayer){
                coalitionAttackers.push_back(pi2);
                break;
            }
        }
    }

    for(int pi2=0;pi2<(int)g_campaign.provinces.size();pi2++){
        Province& ep=g_campaign.provinces[pi2];
        if(ep.owner==FACTION_NEUTRAL||factionIsHuman(ep.owner)) continue; // PvP: saltar faccion humana

        float roll=frandMT();
        bool inCoalition=false;
        for(int ca:coalitionAttackers) if(ca==pi2){inCoalition=true;break;}
        float attackChance=inCoalition?aggression*2.f:aggression;

        // Fase J: los ataques ya no salen de las guarniciones — los ejércitos
        // de campo IA se encargan (ver bloque siguiente). Aquí solo refuerzos.
        if(roll<attackChance*0.4f+0.2f){
            // Reinforcement transfer
            for(int adj:ep.adjacent){
                Province& ap=g_campaign.provinces[adj];
                if(ap.owner!=ep.owner||ap.army.empty()) continue;
                int apStr=0; for(auto [ti,c]:ap.army) apStr+=c;
                int epStr=0; for(auto [ti,c]:ep.army) epStr+=c;
                if(apStr>epStr+20&&!ap.army.empty()){
                    auto& transferUnit=ap.army.front();
                    int transferAmt=transferUnit.second/4;
                    if(transferAmt>0){
                        bool found=false;
                        for(auto& eu:ep.army) if(eu.first==transferUnit.first){eu.second+=transferAmt;found=true;break;}
                        if(!found) ep.army.push_back({transferUnit.first,transferAmt});
                        transferUnit.second-=transferAmt;
                        if(transferUnit.second<=0) ap.army.erase(ap.army.begin());
                    }
                    break;
                }
            }
        }
    }

    // Fase J: ejércitos de campo IA — marchan hacia el jugador y atacan
    bool battlePending=false;
    int np=(int)g_campaign.provinces.size();
    for(int ai=0;ai<(int)g_campaign.armies.size()&&!battlePending;ai++){
        FieldArmy& a=g_campaign.armies[ai];
        // Fase I: ni el jugador ni sus aliados militares marchan contra el jugador
        // PvP: la faccion humana del cliente tampoco la mueve la IA
        if(factionIsHuman(a.owner)||factionIsPlayerSide(a.owner)||a.owner==FACTION_NEUTRAL) continue;
        a.moved=false;
        if(a.province<0||a.province>=np) continue;

        // Provincia del jugador adyacente más débil
        int target=-1,targetDef=1<<30;
        for(int adj:g_campaign.provinces[a.province].adjacent){
            if(adj<0||adj>=np) continue;
            if(g_campaign.provinces[adj].owner!=FACTION_PLAYER) continue;
            int def=defenseSoldiers(adj);
            if(def<targetDef){ targetDef=def; target=adj; }
        }
        int eStr=armySoldiers(a.units);
        float roll=frandMT();
        bool inCoalition=false;
        for(int ca:coalitionAttackers) if(ca==a.province){inCoalition=true;break;}
        float attackChance=inCoalition?aggression*2.f:aggression;

        if(target>=0){
            if(targetDef<=0){
                // Provincia indefensa: anexión directa, sin batalla
                if(roll<attackChance*0.4f){
                    g_campaign.provinces[target].owner=a.owner;
                    g_campaign.provinces[target].army.clear();
                    a.province=target;
                    a.moved=true;
                    battleLogAdd(TextFormat("[T%d] %s seized %s!",g_campaign.turn,
                                 factionNames[a.owner],g_campaign.provinces[target].name));
                }
            } else if(eStr>(int)(targetDef*0.75f)&&roll<attackChance*0.8f){
                // Ataque => batalla defensiva del jugador
                // QA1-01: capturar al atacante ANTES de fusionar. La fusion
                // reconstruye g_campaign.armies (std::move) y deja `a` colgada
                // (use-after-free al leer a.owner/a.units); ademas elimina
                // ejercitos propios anteriores a `ai` y los indices se
                // desplazan (attackerArmyIdx apuntaria a otro ejercito).
                const FactionId atkOwner=a.owner;
                const int atkId=a.id;
                const std::vector<std::pair<int,int>> atkUnits=a.units;
                mergePlayerDefenders(target);
                // Reindexar por id (unico: nextArmyId) tras la fusion
                int atkIdx=-1;
                for(int k=0;k<(int)g_campaign.armies.size();k++)
                    if(g_campaign.armies[k].id==atkId){ atkIdx=k; break; }
                if(atkIdx<0) continue; // imposible: solo se fusionan ejercitos propios
                int di=armyIndexAt(target,FACTION_PLAYER);
                selectArmy(di); // readyUnits = defensores fusionados
                g_preBattle.provinceIdx=target;
                g_preBattle.isDefense=true;
                g_preBattle.fogOfWar=false;
                g_preBattle.estimatedEnemyStrength=eStr+(int)((frandMT()-0.5f)*20);
                g_preBattle.attackerFaction=atkOwner;
                g_preBattle.attackerArmyIdx=atkIdx;
                g_preBattle.armyIdx=di;
                g_preBattle.hadKing=(di>=0&&armyHasKing(g_campaign.armies[di].units));
                g_preBattle.enemyUnits=atkUnits;
                g_preBattle.include.assign(g_campaign.readyUnits.size(),true);
                for(int j=12;j<(int)g_preBattle.include.size();j++)
                    g_preBattle.include[j]=false;
                battlePending=true;
            }
        } else {
            // Marchar hacia la frontera del jugador: 1 paso por turno,
            // solo por territorio propio
            int step=bfsOwnStepToPlayer(a.province,a.owner);
            if(step>=0){ a.province=step; a.moved=true; }
        }
    }

    // Fase J: los ejércitos del jugador ya no se mueven con la animación
    // (el movimiento consume su propio `moved`); al terminar el turno se
    // reinician para la siguiente ronda.
    for(auto& a:g_campaign.armies) a.moved=false;

    // Check victory condition
    int totalProv=(int)g_campaign.provinces.size();
    int playerProv=0;
    for(auto& p:g_campaign.provinces) if(p.owner==FACTION_PLAYER) playerProv++;
    g_campaign.peakProvinces=std::max(g_campaign.peakProvinces,playerProv);
    if(playerProv>=(int)(totalProv*0.8f)){
        battleLogAdd("[Campaign] VICTORY! Empire established!");
    }
    if(playerProv==0){
        battleLogAdd("[Campaign] DEFEAT — all provinces lost!");
    }

    // Finally: Increment turn after all processing
    g_campaign.turn++;
    return battlePending;
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST-BATALLA — resultado -> mapa de campaña (todas las rutas de fin)
// ═══════════════════════════════════════════════════════════════════════════
// Las rutas táctica (VIEW RESULTS), auto-resolve y quick battle convergen en
// updateDrawBattleResult, que llama aquí. Antes de este módulo la remoción del
// enemigo vencido estaba partida: solo se limpiaba la guarnición al capturar
// (y solo si la provincia cambiaba de dueño), de modo que los ejércitos de
// campo enemigos sobrevivían a la derrota y el atacante IA conservaba su
// ejército intacto al perder el asalto.

// El rey murió en la última batalla (leído por main.cpp al pulsar CONTINUE)
bool g_kingLost=false;

// Elimina el ejército `idx` y reajusta los índices que lo referencian.
void removeArmyAt(int idx){
    if(idx<0||idx>=(int)g_campaign.armies.size()) return;
    g_campaign.armies.erase(g_campaign.armies.begin()+idx);
    if(g_campaign.selectedArmy>=idx) g_campaign.selectedArmy--;
    if(g_preBattle.armyIdx==idx)      g_preBattle.armyIdx=-1;
    else if(g_preBattle.armyIdx>idx)  g_preBattle.armyIdx--;
    if(g_preBattle.attackerArmyIdx==idx)     g_preBattle.attackerArmyIdx=-1;
    else if(g_preBattle.attackerArmyIdx>idx) g_preBattle.attackerArmyIdx--;
    // QA1-04: armyMoveIdx guia la animacion de movimiento (main.cpp) y
    // marcara `moved` en el ejercito que apunte tras el erase.
    if(g_campaign.armyMoveIdx==idx)     g_campaign.armyMoveIdx=-1;
    else if(g_campaign.armyMoveIdx>idx) g_campaign.armyMoveIdx--;
}

bool applyBattleResultCampaign(bool quickBattle){
    // ── (A) Victoria en ATAQUE: la provincia pasa al jugador ───────────────
    // Guarnición + ejércitos de campo enemigos en la provincia: derrotados,
    // desaparecen del mapa (antes solo se limpiaba la guarnición).
    if(g_lastResult.playerWon&&!g_lastResult.isDefense&&
       g_lastResult.provinceIdx>=0&&
       g_lastResult.provinceIdx<(int)g_campaign.provinces.size()){
        Province& prov=g_campaign.provinces[g_lastResult.provinceIdx];
        if(prov.owner!=FACTION_PLAYER){
            FactionId prevOwner=prov.owner; // dueño previo: quién fue vencido
            prov.owner=FACTION_PLAYER;
            prov.army.clear(); // guarnición conquistada
            // Mismo criterio con el que se compiló el enemigo en el pre-battle
            // (provincia + dueño de la provincia); los aliados no se tocan.
            for(int k=(int)g_campaign.armies.size()-1;k>=0;k--){
                const FieldArmy& fa=g_campaign.armies[k];
                if(fa.province==g_lastResult.provinceIdx&&fa.owner==prevOwner){
                    battleLogAdd(TextFormat("Enemy army destroyed at %s!",prov.name));
                    removeArmyAt(k); // reajusta g_preBattle/armyMoveIdx si hace falta
                }
            }
            // Fase J: el ejército ganador ocupa la provincia capturada
            int ai=g_preBattle.armyIdx;
            if(ai>=0&&ai<(int)g_campaign.armies.size()){
                g_campaign.armies[ai].province=g_lastResult.provinceIdx;
                g_campaign.armies[ai].moved=true;
                selectArmy(ai);
            }
            g_campaign.playerProvince=g_lastResult.provinceIdx;
        }
    }

    // ── (B) Victoria en DEFENSA: el asalto fracasó ─────────────────────────
    // El ejército atacante (IA) fue derrotado y desaparece del mapa. Sin esta
    // rama el atacante conservaba sus tropas al 100% tras perder la batalla.
    if(g_lastResult.playerWon&&g_lastResult.isDefense){
        int tAi=g_preBattle.attackerArmyIdx;
        if(tAi>=0&&tAi<(int)g_campaign.armies.size()){
            const FieldArmy& ea=g_campaign.armies[tAi];
            // Solo si sigue siendo el atacante enemigo (guarda anti-índice
            // obsoleto): ni ejércitos propios ni de aliados.
            bool enemy=(g_preBattle.attackerFaction<0||
                        ea.owner==(FactionId)g_preBattle.attackerFaction)&&
                       !factionIsPlayerSide(ea.owner);
            if(enemy){
                const char* pname="the province";
                if(g_lastResult.provinceIdx>=0&&
                   g_lastResult.provinceIdx<(int)g_campaign.provinces.size())
                    pname=g_campaign.provinces[g_lastResult.provinceIdx].name;
                battleLogAdd(TextFormat("[T%d] Assault on %s repelled - enemy army destroyed",
                    g_campaign.turn,pname));
                removeArmyAt(tAi);
            }
        } else if(g_preBattle.attackerFaction<0&&
                  g_lastResult.provinceIdx>=0&&
                  g_lastResult.provinceIdx<(int)g_campaign.provinces.size()){
            // Ruta legada: guarnición hostil en la provincia propia (sin
            // ejército atacante). La fuerza vencida era esa guarnición.
            Province& prov=g_campaign.provinces[g_lastResult.provinceIdx];
            if(factionIsPlayerSide(prov.owner)) prov.army.clear();
        }
    }

    // ── (C) Derrota en DEFENSA: el atacante ocupa; el defensor se retira ───
    if(!g_lastResult.playerWon&&g_lastResult.isDefense&&
       g_preBattle.attackerFaction>=0&&
       g_lastResult.provinceIdx>=0&&
       g_lastResult.provinceIdx<(int)g_campaign.provinces.size()){
        Province& prov=g_campaign.provinces[g_lastResult.provinceIdx];
        if(prov.owner==FACTION_PLAYER){
            prov.owner=(FactionId)g_preBattle.attackerFaction;
            prov.army.clear();
            // El ejército atacante entra con sus bajas aproximadas
            int tAi=g_preBattle.attackerArmyIdx;
            if(tAi>=0&&tAi<(int)g_campaign.armies.size()){
                g_campaign.armies[tAi].units=subtractEnemyLosses(
                    g_preBattle.enemyUnits,g_lastResult.enemyLosses);
                if(g_campaign.armies[tAi].units.empty()){
                    removeArmyAt(tAi);
                } else {
                    g_campaign.armies[tAi].province=g_lastResult.provinceIdx;
                    g_campaign.armies[tAi].moved=true;
                }
            }
            // El ejército del jugador se retira a una provincia propia
            int pi=g_lastResult.provinceIdx;
            int dAi=g_preBattle.armyIdx;
            if(dAi>=0&&dAi<(int)g_campaign.armies.size()){
                g_campaign.armies[dAi].units=g_lastResult.survivors;
                int rp=safeRetreatProvince(pi);
                if(rp>=0) g_campaign.armies[dAi].province=rp;
                else removeArmyAt(dAi);
            }
            int sel=g_campaign.selectedArmy;
            selectArmy(sel>=0&&sel<(int)g_campaign.armies.size()?sel:-1);
        }
    }

    // ── (D) Loot + supervivientes del jugador ──────────────────────────────
    bool changed=false;
    if(!g_lastResult.lootApplied){
        g_campaign.res.gold+=g_lastResult.lootGold;
        g_lastResult.lootApplied=true;
        changed=true;
        // Update player army with survivors (so dead soldiers don't "resurrect")
        if(!quickBattle){
            // Fase J: escribir supervivientes en el ejército involucrado
            int ai=g_preBattle.armyIdx;
            if(ai>=0&&ai<(int)g_campaign.armies.size()){
                auto& u=g_campaign.armies[ai].units;
                u.clear();
                for(int i=0;i<(int)g_lastResult.survivors.size();i++){
                    int ti=g_lastResult.survivors[i].first;
                    int cnt=g_lastResult.survivors[i].second;
                    if(ti>=0&&ti<unitTypeCount()&&cnt>0) u.push_back({ti,cnt});
                }
                if(u.empty()){
                    removeArmyAt(ai);
                    selectArmy(g_campaign.armies.empty()?-1:
                               std::min(ai,(int)g_campaign.armies.size()-1));
                } else {
                    g_campaign.readyUnits=u;
                }
            }
            g_campaign.playerArmy=g_campaign.readyUnits;
            // Fase J: si el rey estaba y no sobrevivió → fin de la partida
            if(g_preBattle.hadKing){
                bool kingAlive=false;
                for(auto& s:g_lastResult.survivors)
                    if(s.first==UNIT_KING&&s.second>0) kingAlive=true;
                if(!kingAlive) g_kingLost=true;
            }
            // Track stats for 3.4
            if(g_lastResult.playerWon) g_campaign.battlesWon++;
            else g_campaign.battlesLost++;
        }
    }
    return changed;
}

// ───────────────────────────────────────────────────────────────────────────
//  SELF-TEST HEADLESS (-battletest): rutas de fin de batalla -> 0/1
//  Sin raylib ni ventana: solo estado de campaña. Las bajas/type data se
//  siembran a mano porque initBuiltinTypes() carga texturas (requiere GL).
// ───────────────────────────────────────────────────────────────────────────
int runBattleResultTest(){
    int fails=0;
    auto check=[&](const char* name,bool ok){
        printf("%s %s\n",ok?"PASS":"FAIL",name);
        if(!ok) fails++;
    };
    g_unitTypes.clear();
    for(int i=0;i<3;i++){
        UnitTypeDef t{};
        snprintf(t.name,sizeof t.name,"Test Unit %d",i);
        t.soldierCount=60;
        g_unitTypes.push_back(t);
    }
    auto mkProv=[](const char* name,FactionId owner)->Province{
        Province p{};
        snprintf(p.name,sizeof p.name,"%s",name);
        p.terrain=TERRAIN_PLAIN; p.owner=owner; p.hasCity=false;
        p.city.constructing=-1; p.city.constructTurns=0; p.city.constructSlot=-1;
        for(int i=0;i<16;i++) p.city.grid[i]=-1;
        return p;
    };
    auto reset=[&](){
        g_campaign=CampaignState{};
        g_preBattle=PreBattleState{};
        g_lastResult=BattleResult{};
        g_localFaction=FACTION_PLAYER;
        g_kingLost=false;
        g_campaign.res.gold=1000.f;
    };
    auto addArmy=[&](FactionId own,int prov,
                     const std::vector<std::pair<int,int>>& u)->int{
        FieldArmy a; a.id=g_campaign.nextArmyId++;
        a.owner=own; a.province=prov; a.units=u;
        g_campaign.armies.push_back(a);
        return (int)g_campaign.armies.size()-1;
    };

    // ── A) Victoria en ATAQUE: captura + enemigo (guarnición y campo) fuera
    {
        reset();
        Province p0=mkProv("Home",FACTION_PLAYER);
        Province p1=mkProv("Enemy Town",FACTION_AGGRESSIVE);
        Province p2=mkProv("Back",FACTION_PLAYER);
        p0.adjacent={1,2}; p1.adjacent={0}; p2.adjacent={0};
        g_campaign.provinces={p0,p1,p2};
        g_campaign.provinces[1].army={{0,40}};          // guarnición enemiga
        int pi=addArmy(FACTION_PLAYER,0,{{0,60}});      // atacante (jugador)
        addArmy(FACTION_AGGRESSIVE,1,{{1,30}});         // defensor de campo
        g_campaign.allied[FACTION_DEFENSIVE]=true;
        addArmy(FACTION_DEFENSIVE,1,{{0,20}});          // aliado en la provincia
        g_preBattle.provinceIdx=1; g_preBattle.isDefense=false;
        g_preBattle.armyIdx=pi; g_preBattle.attackerArmyIdx=-1;
        g_preBattle.attackerFaction=-1;
        g_preBattle.enemyUnits={{0,40},{1,30}};
        g_lastResult.playerWon=true; g_lastResult.isDefense=false;
        g_lastResult.provinceIdx=1; g_lastResult.survivors={{0,50}};
        g_lastResult.lootGold=60.f; g_lastResult.lootApplied=false;
        g_lastResult.playerLosses=10; g_lastResult.enemyLosses=70;
        bool changed=applyBattleResultCampaign(false);
        check("A1 attack: province captured",
              g_campaign.provinces[1].owner==FACTION_PLAYER);
        check("A2 attack: garrison cleared",
              g_campaign.provinces[1].army.empty());
        check("A3 attack: enemy field army destroyed",
              armyIndexAt(1,FACTION_AGGRESSIVE)<0);
        check("A4 attack: allied army kept",
              armyIndexAt(1,FACTION_DEFENSIVE)>=0);
        check("A5 attack: winner occupies with survivors",
              pi<(int)g_campaign.armies.size()&&
              g_campaign.armies[pi].province==1&&
              g_campaign.armies[pi].units.size()==1&&
              g_campaign.armies[pi].units[0]==std::make_pair(0,50));
        check("A6 attack: loot + battle stat",
              changed&&g_campaign.res.gold==1060.f&&g_campaign.battlesWon==1);
        bool again=applyBattleResultCampaign(false);
        check("A7 attack: idempotent (no double loot)",
              !again&&g_campaign.res.gold==1060.f&&g_campaign.battlesWon==1);
    }

    // ── B) Victoria en DEFENSA: el atacante IA desaparece (índices reajustados)
    {
        reset();
        Province p0=mkProv("Home",FACTION_PLAYER);
        Province p1=mkProv("Border",FACTION_AGGRESSIVE);
        p0.adjacent={1}; p1.adjacent={0};
        g_campaign.provinces={p0,p1};
        int att=addArmy(FACTION_AGGRESSIVE,1,{{1,40}}); // índice 0 (IA)
        int def=addArmy(FACTION_PLAYER,0,{{0,50}});      // índice 1 (defensor)
        addArmy(FACTION_PLAYER,0,{{1,40}});              // índice 2 (otro propio)
        g_campaign.selectedArmy=2;
        g_preBattle.provinceIdx=0; g_preBattle.isDefense=true;
        g_preBattle.attackerFaction=FACTION_AGGRESSIVE;
        g_preBattle.attackerArmyIdx=att; g_preBattle.armyIdx=def;
        g_preBattle.enemyUnits={{1,40}};
        g_lastResult.playerWon=true; g_lastResult.isDefense=true;
        g_lastResult.provinceIdx=0; g_lastResult.survivors={{0,45}};
        g_lastResult.lootGold=0.f; g_lastResult.lootApplied=false;
        applyBattleResultCampaign(false);
        check("B1 defense: attacker army destroyed",
              armyIndexAt(1,FACTION_AGGRESSIVE)<0&&
              (int)g_campaign.armies.size()==2);
        check("B2 defense: survivors land on the defender (indices)",
              g_preBattle.armyIdx==0&&
              g_campaign.armies[0].units.size()==1&&
              g_campaign.armies[0].units[0]==std::make_pair(0,45));
        check("B3 defense: other player army untouched",
              g_campaign.armies[1].units.size()==1&&
              g_campaign.armies[1].units[0]==std::make_pair(1,40));
        check("B4 defense: selectedArmy shifted with erase",
              g_campaign.selectedArmy==1);
        check("B5 defense: province still ours",
              g_campaign.provinces[0].owner==FACTION_PLAYER);
        check("B6 defense: no loot on defense win",
              g_campaign.res.gold==1000.f&&g_campaign.battlesWon==1);
    }

    // ── C) Derrota en DEFENSA (regresión): el atacante ocupa y el defensor huye
    {
        reset();
        Province p0=mkProv("Home",FACTION_PLAYER);
        Province p1=mkProv("Refuge",FACTION_PLAYER);
        Province p2=mkProv("Enemy Land",FACTION_AGGRESSIVE);
        p0.adjacent={1,2}; p1.adjacent={0}; p2.adjacent={0};
        g_campaign.provinces={p0,p1,p2};
        g_campaign.provinces[0].army={{4,10}};           // guarnición propia
        int att=addArmy(FACTION_AGGRESSIVE,2,{{1,40}});
        int def=addArmy(FACTION_PLAYER,0,{{0,50}});
        int oth=addArmy(FACTION_PLAYER,1,{{0,30}});
        g_preBattle.provinceIdx=0; g_preBattle.isDefense=true;
        g_preBattle.attackerFaction=FACTION_AGGRESSIVE;
        g_preBattle.attackerArmyIdx=att; g_preBattle.armyIdx=def;
        g_preBattle.enemyUnits={{1,40}};
        g_lastResult.playerWon=false; g_lastResult.isDefense=true;
        g_lastResult.provinceIdx=0; g_lastResult.survivors={{0,15}};
        g_lastResult.enemyLosses=15; g_lastResult.lootGold=0.f;
        g_lastResult.lootApplied=false;
        applyBattleResultCampaign(false);
        check("C1 defeat: province taken by attacker",
              g_campaign.provinces[0].owner==FACTION_AGGRESSIVE);
        check("C2 defeat: attacker occupies with losses applied",
              g_campaign.armies[att].province==0&&
              g_campaign.armies[att].units.size()==1&&
              g_campaign.armies[att].units[0]==std::make_pair(1,25));
        check("C3 defeat: defender retreats with survivors",
              g_campaign.armies[def].province==1&&
              g_campaign.armies[def].units.size()==1&&
              g_campaign.armies[def].units[0]==std::make_pair(0,15));
        check("C4 defeat: garrison cleared", g_campaign.provinces[0].army.empty());
        check("C5 defeat: other player army untouched",
              g_campaign.armies[oth].province==1&&
              g_campaign.armies[oth].units.size()==1&&
              g_campaign.armies[oth].units[0]==std::make_pair(0,30));
    }

    // ── D) Victoria en DEFENSA vs guarnición hostil (ruta legada, sin atacante)
    {
        reset();
        Province p0=mkProv("Home",FACTION_PLAYER);
        Province p1=mkProv("Refuge",FACTION_PLAYER);
        p0.adjacent={1}; p1.adjacent={0};
        g_campaign.provinces={p0,p1};
        g_campaign.provinces[0].army={{0,30}};           // guarnición hostil
        int def=addArmy(FACTION_PLAYER,0,{{0,50}});
        g_preBattle.provinceIdx=0; g_preBattle.isDefense=true;
        g_preBattle.attackerFaction=-1; g_preBattle.attackerArmyIdx=-1;
        g_preBattle.armyIdx=def; g_preBattle.enemyUnits={{0,30}};
        g_lastResult.playerWon=true; g_lastResult.isDefense=true;
        g_lastResult.provinceIdx=0; g_lastResult.survivors={{0,40}};
        g_lastResult.lootGold=0.f; g_lastResult.lootApplied=false;
        applyBattleResultCampaign(false);
        check("D1 legacy: hostile garrison cleared on defense win",
              g_campaign.provinces[0].army.empty());
        check("D2 legacy: defender keeps survivors",
              (int)g_campaign.armies.size()==1&&
              g_campaign.armies[0].units.size()==1&&
              g_campaign.armies[0].units[0]==std::make_pair(0,40));
    }

    // ── E) Quick battle (provinceIdx=-1): no toca la campaña
    {
        reset();
        Province p0=mkProv("Home",FACTION_PLAYER);
        g_campaign.provinces={p0};
        int def=addArmy(FACTION_PLAYER,0,{{0,50}});
        g_preBattle.armyIdx=def;
        g_lastResult.playerWon=true; g_lastResult.isDefense=false;
        g_lastResult.provinceIdx=-1; g_lastResult.survivors={{0,40}};
        g_lastResult.lootGold=50.f; g_lastResult.lootApplied=false;
        applyBattleResultCampaign(true);
        check("E1 quick: no capture (provinceIdx=-1)",
              g_campaign.provinces[0].owner==FACTION_PLAYER);
        check("E2 quick: army untouched (no survivors write)",
              (int)g_campaign.armies.size()==1&&
              g_campaign.armies[0].units.size()==1&&
              g_campaign.armies[0].units[0]==std::make_pair(0,50));
        check("E3 quick: no king/loss flags", !g_kingLost);
    }

    // ── F) removeArmyAt reajusta armyMoveIdx (QA1-04)
    {
        reset();
        addArmy(FACTION_PLAYER,0,{{0,10}});
        addArmy(FACTION_PLAYER,0,{{0,10}});
        addArmy(FACTION_PLAYER,0,{{0,10}});
        g_campaign.armyMoveIdx=2;
        removeArmyAt(0); // idx < armyMoveIdx
        check("F1 removeArmyAt: armyMoveIdx decremented",
              g_campaign.armyMoveIdx==1&&(int)g_campaign.armies.size()==2);
        g_campaign.armyMoveIdx=1;
        removeArmyAt(1); // idx == armyMoveIdx
        check("F2 removeArmyAt: armyMoveIdx reset when its army dies",
              g_campaign.armyMoveIdx==-1&&(int)g_campaign.armies.size()==1);
        addArmy(FACTION_PLAYER,0,{{0,10}});
        addArmy(FACTION_PLAYER,0,{{0,10}});
        g_campaign.armyMoveIdx=0;
        removeArmyAt(2); // idx > armyMoveIdx
        check("F3 removeArmyAt: armyMoveIdx below idx unchanged",
              g_campaign.armyMoveIdx==0&&(int)g_campaign.armies.size()==2);
    }

    // ── G) processTurn: el atacante IA se reindexa tras fusionar defensores
    // (regresión QA1-01: `FieldArmy& a` quedaba colgada al reconstruir el
    //  vector y attackerArmyIdx apuntaba a otro índice tras la fusión)
    {
        reset();
        Province p0=mkProv("Home",FACTION_PLAYER);
        Province p1=mkProv("Enemy Camp",FACTION_AGGRESSIVE);
        p0.adjacent={1}; p1.adjacent={0};
        g_campaign.provinces={p0,p1};
        addArmy(FACTION_PLAYER,0,{{0,60}});          // índice 0: defensor A
        addArmy(FACTION_PLAYER,0,{{0,60}});          // índice 1: defensor B
        addArmy(FACTION_AGGRESSIVE,1,{{1,300}});     // índice 2: atacante IA
        g_rng.seed(20261012u);                       // run determinista
        bool pending=false;
        for(int t=0;t<1000&&!pending;t++) pending=processTurn();
        int bi=g_preBattle.attackerArmyIdx;
        bool okIdx=pending&&bi>=0&&bi<(int)g_campaign.armies.size();
        check("G1 processTurn: defensive battle triggered", pending);
        check("G2 processTurn: attackerArmyIdx valid after merge", okIdx);
        check("G3 processTurn: attacker army reindexed by id",
              okIdx&&g_campaign.armies[bi].owner==FACTION_AGGRESSIVE&&
              g_preBattle.attackerFaction==(int)FACTION_AGGRESSIVE);
        check("G4 processTurn: enemyUnits = attacker units",
              okIdx&&g_preBattle.enemyUnits==g_campaign.armies[bi].units);
        int di=armyIndexAt(0,FACTION_PLAYER);
        check("G5 processTurn: defenders merged (readyUnits view)",
              okIdx&&di>=0&&
              armySoldiers(g_campaign.readyUnits)==120&&
              g_preBattle.armyIdx==di);
    }

    if(fails==0){ printf("BATTERESULT PASS\n"); return 0; }
    printf("BATTERESULT FAIL (%d)\n",fails);
    return 1;
}

// ═══════════════════════════════════════════════════════════════════════════

// (:690, :692-693) — Resource trading (externs en campaign.h)
const float TRADE_RATES[5]={1.f, 1.2f, 1.3f, 1.5f, 0.8f}; // gold, food, wood, stone, iron
int g_tradeTab=0; // 0-4 for each resource
int g_tradeAmount=10; // amount to buy/sell

// ═══════════════════════════════════════════════════════════════════════════
//  FASE I: DIPLOMACIA (alianzas militares + pactos comerciales + trueque)
// ═══════════════════════════════════════════════════════════════════════════

int g_mapMode=0; // 0 = modo normal, 1 = modo alianzas (solo cambia colores)
char g_diploMsg[160]={0};
Color g_diploMsgCol={200,165,80,255};

static const Color DIPLO_OK  ={120,220,120,255};
static const Color DIPLO_BAD ={230, 90, 90,255};
static const Color DIPLO_WARN={230,160, 60,255};
// Colores del modo de mapa alianzas (solo provincias; la UI no cambia)
static const Color DIPLO_TRADE_COL={50,200,120,255}; // pacto comercial
static const Color DIPLO_NONE_COL ={85, 85, 90,255}; // sin relacion

static void diploMsg(Color col,const char* text){
    strncpy(g_diploMsg,text,sizeof(g_diploMsg)-1);
    g_diploMsg[sizeof(g_diploMsg)-1]='\0';
    g_diploMsgCol=col;
}

int factionMilitaryPower(int f){
    if(f<0||f>=FACTION_COUNT) return 0;
    int s=0;
    for(const auto& p:g_campaign.provinces)
        if((int)p.owner==f) for(const auto& g:p.army) s+=g.second;
    for(const auto& a:g_campaign.armies)
        if((int)a.owner==f) s+=armySoldiers(a.units);
    if(f==FACTION_PLAYER) // la reserva del host
        for(const auto& r:g_campaign.reserve) s+=r.second;
    // PvP: la reserva del cliente cuenta para su facción
    if(g_campaign.pvpMode&&(FactionId)f==g_campaign.clientFaction)
        for(const auto& r:g_campaign.clientReserve) s+=r.second;
    return s;
}

int totalMilitaryPower(){
    int t=0;
    for(int f=0;f<FACTION_COUNT;f++) t+=factionMilitaryPower(f);
    return t;
}

int playerBlocPower(){
    int p=factionMilitaryPower(FACTION_PLAYER);
    for(int f=0;f<FACTION_COUNT;f++) if(g_campaign.allied[f]) p+=factionMilitaryPower(f);
    return p;
}

void proposeMilitaryAlliance(int f){
    if(f==FACTION_PLAYER||f==FACTION_NEUTRAL||f<0||f>=FACTION_COUNT){
        diploMsg(DIPLO_BAD,"Cannot form an alliance with this faction");
        return;
    }
    if(g_campaign.allied[f]){
        diploMsg(DIPLO_BAD,TextFormat("Already allied with %s",factionNames[f]));
        return;
    }
    // Regla: cada jugador solo puede pertenecer a UNA alianza militar
    for(int k=0;k<FACTION_COUNT;k++){
        if(g_campaign.allied[k]){
            diploMsg(DIPLO_BAD,"Rejected: one military alliance only");
            return;
        }
    }
    int target=factionMilitaryPower(f);
    int total=totalMilitaryPower();
    if(total<=0) return;
    // Regla del 50%: ni la alianza existente que se quiere unir ni la
    // alianza resultante (jugador + aliados + objetivo) pueden alcanzar el
    // 50% de la potencia militar total del mundo.
    if(target*2>=total){
        diploMsg(DIPLO_WARN,TextFormat(
            "Rejected: %s alone holds %d%% of world military power (max 50%%)",
            factionNames[f],(int)((long long)target*100/total)));
        return;
    }
    int bloc=playerBlocPower()+target;
    if(bloc*2>=total){
        diploMsg(DIPLO_WARN,TextFormat(
            "Rejected: alliance would hold %d%% of world military power (max 50%%)",
            (int)((long long)bloc*100/total)));
        return;
    }
    g_campaign.allied[f]=true;
    battleLogAdd(TextFormat("[Diplomacy] Military alliance formed with %s",factionNames[f]));
    diploMsg(DIPLO_OK,TextFormat("Alliance formed with %s",factionNames[f]));
}

void proposeCommercialPact(int f){
    if(f==FACTION_PLAYER||f==FACTION_NEUTRAL||f<0||f>=FACTION_COUNT){
        diploMsg(DIPLO_BAD,"No one to sign a pact with");
        return;
    }
    if(g_campaign.tradePact[f]){
        diploMsg(DIPLO_BAD,TextFormat("Already has a commercial pact with %s",factionNames[f]));
        return;
    }
    // Pacto comercial: permitido con cualquier pais (sin limite de numero)
    g_campaign.tradePact[f]=true;
    battleLogAdd(TextFormat("[Diplomacy] Commercial pact signed with %s",factionNames[f]));
    diploMsg(DIPLO_OK,TextFormat("Commercial pact signed with %s",factionNames[f]));
}

void dissolveRelations(int f,bool military){
    if(f<0||f>=FACTION_COUNT) return;
    if(military){
        if(!g_campaign.allied[f]){ diploMsg(DIPLO_BAD,"No alliance to dissolve"); return; }
        g_campaign.allied[f]=false;
        diploMsg(DIPLO_OK,TextFormat("Alliance dissolved with %s",factionNames[f]));
    } else {
        if(!g_campaign.tradePact[f]){ diploMsg(DIPLO_BAD,"No pact to dissolve"); return; }
        g_campaign.tradePact[f]=false;
        diploMsg(DIPLO_OK,TextFormat("Commercial pact dissolved with %s",factionNames[f]));
    }
}

void proposeTradeDeal(int f,int giveRes,float giveAmt,int recvRes,float recvAmt){
    if(f<0||f>=FACTION_COUNT||giveRes<0||giveRes>4||recvRes<0||recvRes>4) return;
    if(!g_campaign.tradePact[f]){
        diploMsg(DIPLO_BAD,TextFormat("Trade rejected: sign a commercial pact with %s first",
                 factionNames[f]));
        return;
    }
    if(giveAmt<=0.f||recvAmt<=0.f){
        diploMsg(DIPLO_BAD,"Trade rejected: amounts must be positive");
        return;
    }
    float* giveVal[5]={&g_campaign.res.gold,&g_campaign.res.food,&g_campaign.res.wood,
                       &g_campaign.res.stone,&g_campaign.res.iron};
    static const char* rn[5]={"Gold","Food","Wood","Stone","Iron"};
    if(*giveVal[giveRes]<giveAmt){
        diploMsg(DIPLO_BAD,TextFormat("Trade rejected: not enough %s",rn[giveRes]));
        return;
    }
    float gv=giveAmt*TRADE_RATES[giveRes];
    float rv=recvAmt*TRADE_RATES[recvRes];
    // Personalidad de la IA: AGGRESSIVE codicioso, DEFENSIVE cauto,
    // COMMERCIAL acepta a valor justo. La IA no lleva contabilidad de
    // recursos: su contrapartida es abstracta (decide por valor+precio).
    static const float factor[FACTION_COUNT]={1.f,1.3f,1.15f,1.f,1.25f,1.f};
    if(gv<rv*factor[f]){
        diploMsg(DIPLO_BAD,TextFormat("Trade rejected: offer too low (%.0f < %.0f)",
                 gv,rv*factor[f]));
        return;
    }
    *giveVal[giveRes]-=giveAmt;
    *giveVal[recvRes]+=recvAmt;
    battleLogAdd(TextFormat("[Diplomacy] Traded %d %s for %d %s with %s",
                 (int)giveAmt,rn[giveRes],(int)recvAmt,rn[recvRes],factionNames[f]));
    diploMsg(DIPLO_OK,TextFormat("Trade accepted: -%d %s, +%d %s",
                 (int)giveAmt,rn[giveRes],(int)recvAmt,rn[recvRes]));
}

Color diplomacyMapColor(FactionId f){
    if(g_mapMode!=1) return factionDisplayColor(f); // modo normal: sin cambios
    if(factionIsPlayerSide(f)) return factionDisplayColor(f); // yo + aliados militares
    if((int)f>=0&&(int)f<FACTION_COUNT&&g_campaign.tradePact[f])
        return DIPLO_TRADE_COL;   // pacto comercial
    return DIPLO_NONE_COL;        // sin relacion diplomatica
}
