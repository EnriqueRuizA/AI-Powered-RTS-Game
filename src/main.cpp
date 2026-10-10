#include "config.h"
#include "util.h"
#include "ui.h"
#include "audio.h"
#include "fx.h"
#include "atmos.h"
#include "sprite.h"
#include "terrain.h"
#include "camera.h"
#include "elements.h"
#include "combat.h"
#include "campaign.h"
#include "city.h"
#include "save.h"
#include "net.h"
#include "prof.h"
#include "mapart.h"
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

//  STATE FADE SYSTEM (4.2)
// ───────────────────────────────────────────────────────────────────────────
float     g_fadeAlpha     = 0.f;
GameState g_pendingState  = STATE_MAIN_MENU;
bool      g_fadingOut     = false;

// ───────────────────────────────────────────────────────────────────────────
bool g_quickBattle = false;
bool g_kingLost    = false; // Fase J: el rey murió en la última batalla
struct QuickBattleSetup {
    std::vector<int> playerCounts;
    std::vector<int> enemyCounts;
    int difficulty; // 0=easy,1=normal,2=hard
};
QuickBattleSetup g_quickSetup;

// ───────────────────────────────────────────────────────────────────────────
//  UNIT EDITOR STATE (sandbox)
// ───────────────────────────────────────────────────────────────────────────
int       g_editTypeIdx = 0;
float     g_editorPreviewAngle = 0.f;
float     g_editorListScrollY = 0.f;   // scroll for left-panel unit list
float     g_editorRightScrollY = 0.f; // scroll for right-panel stats
bool      g_editorNewUnitDialogOpen = false;
std::string g_editorNewUnitNameBuf;
UnitTypeDef g_editorEditCopy;           // working copy for right-panel textboxes
bool      g_editorEditCopyValid = false;
int       g_editorFocusField = -1;     // which stat textbox is focused (-1 = none)
std::string g_editorFocusBuf;           // current text being edited in focused textbox

// ───────────────────────────────────────────────────────────────────────────
//  FASE H — SINCRONIZACIÓN DE CAMPAÑA (host autoritativo, co-op)
// Modelo: ambos jugadores comparten el mismo reino. El cliente envía
// comandos (END_TURN/MOVE_ARMY/RECRUIT/DISBAND) y el host los aplica y
// retransmite un snapshot (mismo formato binario SAVE_VERSION=8 que
// saveGame). El cliente no muta estado local: lo sobreescribe el snapshot.
// ───────────────────────────────────────────────────────────────────────────
static bool g_netDirty=false; // host: hay cambios locales sin retransmitir

static bool netIsClient(){
    return g_netSession.role==NET_ROLE_CLIENT&&g_netSession.connected;
}
static bool netIsHost(){
    return g_netSession.role==NET_ROLE_HOST;
}
// Host: marca el estado de campaña como cambiado (se retransmite en
// netSyncPump). En cliente no hace nada (el estado no es local).
static void netMarkDirty(){
    if(netIsHost()) g_netDirty=true;
}
// Cliente: true si puede ejecutar acciones locales de campaña (en co-op
// el host es autoritativo; el cliente solo observa y envía comandos).
static bool netCanEdit(){
    return !netIsClient();
}

// Host: aplica un comando recibido del cliente (validación básica).
static void netHostApplyCmd(const NetCmd& c){
    // PvP: c.faction es la facción del remitente (0=FACTION_PLAYER en co-op).
    // En co-op todos los clientes son FACTION_PLAYER; en PvP cada uno su facción.
    int cf=c.faction;
    switch(c.cmd){
    case NET_CMD_END_TURN:{
        if(g_state!=STATE_CAMPAIGN_MAP) break; // host ocupado (batalla, etc.)
        bool pending=processTurn();
        int totalProv=(int)g_campaign.provinces.size();
        int playerProv=0;
        for(auto& p:g_campaign.provinces) if(p.owner==FACTION_PLAYER) playerProv++;
        g_netDirty=true;
        if(playerProv>=(int)(totalProv*0.8f)) g_state=STATE_VICTORY;
        else if(playerProv==0) g_state=STATE_DEFEAT;
        else if(pending) g_state=STATE_PRE_BATTLE;
        break;
    }
    case NET_CMD_MOVE_ARMY:{
        if(g_state!=STATE_CAMPAIGN_MAP) break;
        int from=c.a,to=c.b,mi=c.c;
        if(from<0||from>=(int)g_campaign.provinces.size()) break;
        if(to<0||to>=(int)g_campaign.provinces.size()) break;
        bool adj=false; for(int a:g_campaign.provinces[from].adjacent) if(a==to) adj=true;
        if(!adj) break;
        if(g_campaign.provinces[to].owner!=(FactionId)cf) break; // solo provincias de la facción del remitente
        if(mi>=0&&mi<(int)g_campaign.armies.size()&&g_campaign.armies[mi].owner==(FactionId)cf){
            if(g_campaign.armies[mi].province==from&&!g_campaign.armies[mi].moved){
                g_campaign.armies[mi].province=to; g_campaign.armies[mi].moved=true;
                if(cf==FACTION_PLAYER) selectArmy(mi); // solo seleccionar si es del host
                g_netDirty=true;
            }
        } else if(cf==FACTION_PLAYER&&g_campaign.playerProvince==from){
            // playerProvince solo aplica al host (la capital del cliente se maneja aparte)
            g_campaign.playerProvince=to; g_netDirty=true;
        }
        break;
    }
    case NET_CMD_RECRUIT:{
        if(g_state!=STATE_CAMPAIGN_MAP&&g_state!=STATE_RECRUITMENT&&
           g_state!=STATE_CITY_MANAGEMENT) break;
        int prov=c.a,ti=c.b;
        if(prov<0||prov>=(int)g_campaign.provinces.size()) break;
        Province& p=g_campaign.provinces[prov];
        if(p.owner!=(FactionId)cf||!p.hasCity) break;
        if(ti<0||ti>=unitTypeCount()) break;
        const UnitTypeDef& td=g_unitTypes[ti];
        if(td.recruitGold<0) break;
        // PvP: cada facción tiene sus propios recursos (host=res, cliente=clientRes)
        Resources& r=(cf==FACTION_PLAYER)?g_campaign.res:g_campaign.clientRes;
        if(r.gold<td.recruitGold||r.food<td.recruitFood||r.iron<td.recruitIron) break;
        r.gold-=td.recruitGold; r.food-=td.recruitFood; r.iron-=td.recruitIron;
        g_campaign.recruitQueue.push_back({ti,td.recruitTurns,td.recruitTurns,cf}); // PvP: facción del remitente
        g_netDirty=true;
        break;
    }
    case NET_CMD_DISBAND:{
        if(g_state!=STATE_CAMPAIGN_MAP) break;
        int mi=c.a;
        if(mi>=0&&mi<(int)g_campaign.armies.size()&&g_campaign.armies[mi].owner==(FactionId)cf){
            disbandArmy(mi); g_netDirty=true;
        }
        break;
    }
    case NET_CMD_FORM_ARMY:{ // PvP: forma ejército de la facción del remitente
        if(g_state!=STATE_CAMPAIGN_MAP) break;
        int prov=c.a;
        if(prov<0||prov>=(int)g_campaign.provinces.size()) break;
        if(formArmyAt(prov,cf)) g_netDirty=true;
        break;
    }
    case NET_CMD_JOIN_ARMY:{ // PvP: une reserva de la facción a su ejército
        if(g_state!=STATE_CAMPAIGN_MAP) break;
        int prov=c.a;
        if(prov<0||prov>=(int)g_campaign.provinces.size()) break;
        joinArmyAt(prov,cf); g_netDirty=true;
        break;
    }
    case NET_CMD_ATTACK:{ // H2: host entra en PRE_BATTLE y resuelve la batalla
        if(g_state!=STATE_CAMPAIGN_MAP) break; // host ocupado
        int to=c.a, mi=c.b;
        if(to<0||to>=(int)g_campaign.provinces.size()) break;
        if(mi<0||mi>=(int)g_campaign.armies.size()) break;
        if(g_campaign.armies[mi].owner!=(FactionId)cf) break;
        if(g_campaign.armies[mi].moved) break; // ya movido este turno
        Province& p=g_campaign.provinces[to];
        // no atacar a la propia facción (en PvP el otro jugador es enemigo)
        if(p.owner==(FactionId)cf) break;
        // adyacencia: provincia del ejército -> objetivo
        int from=g_campaign.armies[mi].province;
        bool adj=false; for(int a:g_campaign.provinces[from].adjacent) if(a==to) adj=true;
        if(!adj) break;
        selectArmy(mi); // readyUnits = vista del ejército atacante
        // unidades enemigas = guarnición + ejércitos de campo de la facción
        std::vector<std::pair<int,int>> eu=p.army;
        for(auto& fa:g_campaign.armies)
            if(fa.province==to&&fa.owner==p.owner&&fa.owner!=(FactionId)cf)
                for(auto& u:fa.units) eu.push_back(u);
        g_preBattle.provinceIdx=to;
        g_preBattle.isDefense=false;
        g_preBattle.fogOfWar=(p.owner!=FACTION_NEUTRAL);
        g_preBattle.estimatedEnemyStrength=armySoldiers(eu)+(int)((frandMT()-0.5f)*20);
        g_preBattle.attackerFaction=-1;
        g_preBattle.attackerArmyIdx=-1;
        g_preBattle.armyIdx=mi;
        g_preBattle.hadKing=armyHasKing(g_campaign.armies[mi].units);
        g_preBattle.enemyUnits=eu;
        g_preBattle.include.clear();
        g_preBattle.include.resize(g_campaign.readyUnits.size(),false);
        for(int j=0;j<std::min((int)g_preBattle.include.size(),12);j++)
            g_preBattle.include[j]=true;
        g_state=STATE_PRE_BATTLE; // el host resuelve (auto-resolve o batalla real)
        break;
    }
    case NET_CMD_SYNC_REQ: g_netDirty=true; break;
    }
}

// Un frame de sincronización (llamar 1×/frame cuando hay sesión activa).
static void netSyncPump(){
    if(g_netSession.role==NET_ROLE_NONE){
        g_localFaction=FACTION_PLAYER; // sin sesión: siempre el jugador local
        return;
    }
    // PvP: el cliente controla clientFaction; el host (y co-op) controlan PLAYER
    if(g_netSession.role==NET_ROLE_CLIENT&&g_campaign.pvpMode&&
       g_netSession.assignedFaction>=0)
        g_localFaction=(FactionId)g_netSession.assignedFaction;
    else
        g_localFaction=FACTION_PLAYER;
    netSessionPoll();
    if(netIsHost()){
        NetCmd cmds[8];
        int n=netSessionTakeCmd(cmds,8);
        for(int i=0;i<n;i++) netHostApplyCmd(cmds[i]);
        if(g_netSession.needSnapshot){ g_netSession.needSnapshot=false; g_netDirty=true; }
        if(g_netDirty&&netSessionHasClients()&&!g_campaign.provinces.empty()&&
           g_state!=STATE_MAIN_MENU&&g_state!=STATE_MULTIPLAYER){
            std::vector<char> blob;
            if(serializeCampaignState(g_campaign,blob))
                netSessionBroadcastSnapshot(blob.data(),(uint32_t)blob.size());
        }
        g_netDirty=false;
    } else if(netIsClient()){
        if(g_netSession.snapReady){
            GameState st=g_state;
            bool safe=(st==STATE_CAMPAIGN_MAP||st==STATE_MULTIPLAYER||st==STATE_MAIN_MENU||
                       st==STATE_RECRUITMENT||st==STATE_CITY_MANAGEMENT||st==STATE_MARKETPLACE||
                       st==STATE_DIPLOMACY||st==STATE_UNIT_CODEX);
            if(safe){
                if(applyCampaignSnapshot(g_netSession.snapBuf,g_netSession.snapSize)){
                    netSessionConsumeSnapshot();
                    if(st==STATE_MULTIPLAYER||st==STATE_MAIN_MENU) g_state=STATE_CAMPAIGN_MAP;
                    // PvP: aplicar facción asignada (del WELCOME) al estado local
                    if(g_netSession.assignedFaction>=0)
                        g_campaign.clientFaction=(FactionId)g_netSession.assignedFaction;
                    // Cliente: check victoria/derrota local (mismo umbral que host)
                    int totalProv=(int)g_campaign.provinces.size();
                    FactionId myF=(g_campaign.pvpMode)?g_campaign.clientFaction:FACTION_PLAYER;
                    int playerProv=0;
                    for(auto& p:g_campaign.provinces) if(p.owner==myF) playerProv++;
                    if(playerProv>=(int)(totalProv*0.8f)) g_state=STATE_VICTORY;
                    else if(playerProv==0) g_state=STATE_DEFEAT;
                } else netSessionConsumeSnapshot();
            }
        }
    }
}

// ───────────────────────────────────────────────────────────────────────────
//  STATE: BATTLE
// ═══════════════════════════════════════════════════════════════════════════
GameState updateDrawBattle(Vector2 mouse,float dt){
    float eff=dt*g_battle.timeScale;

    // Controls
    if(IsKeyPressed(KEY_ESCAPE)) g_battle.paused=!g_battle.paused;
    if(IsKeyPressed(KEY_SPACE)) g_battle.timeScale=(g_battle.timeScale>1.f)?1.f:2.f;
    if(IsKeyPressed(KEY_F12)) g_debugSoldiers=!g_debugSoldiers;

    // Camera pan/zoom/lerp (extraído a camera.cpp)
    updateBattleCamera(dt);

    // Pause overlay
    if(g_battle.paused){
        // Still draw world
        beginBattleView();
        drawBattlefield();
        drawAllUnits();
        fxDraw();
        drawSoldierDebug();
        atmosDrawWorld();
        endBattleView();
        atmosDrawScreen(SCREEN_W,SCREEN_H);
        drawBattleHUD(mouse);
        // Overlay
        DrawRectangle(0,0,SCREEN_W,SCREEN_H,{0,0,0,160});
        const char* pm="PAUSED";
        int ptw=MeasureText(pm,52);
        DrawText(pm,SCREEN_W/2-ptw/2,SCREEN_H/2-80,52,C_GOLD);
        if(drawButton({(float)(SCREEN_W/2-130),(float)(SCREEN_H/2+10),260,50},"RESUME",mouse))
            g_battle.paused=false;
        if(drawButton({(float)(SCREEN_W/2-130),(float)(SCREEN_H/2+70),260,50},"ABANDON BATTLE",mouse,
                       {60,20,20,255},{100,40,40,255})){
            g_battle.paused=false;
            g_battle.battleOver=true;
            g_battle.playerWon=false;
        }
        return STATE_BATTLE;
    }

    // ── CONTROL GROUPS ────────────────────────────────────────────────────────
    double t0=GetTime(); double t1=t0; // profiler: input / update
    // Ctrl+1..9  →  save currently selected units as group N (preserves relative positions)
    // 1..9       →  select group N and mark it as active (move orders will keep formation)
    {
        static const int keys[9]={KEY_ONE,KEY_TWO,KEY_THREE,KEY_FOUR,KEY_FIVE,
                                   KEY_SIX,KEY_SEVEN,KEY_EIGHT,KEY_NINE};
        bool ctrl=IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL);
        for(int g=0;g<9;g++){
            if(!IsKeyPressed(keys[g])) continue;
            if(ctrl){
                // ── SAVE GROUP ──────────────────────────────────────────────
                ControlGroup& cg=g_battle.controlGroups[g];
                cg.unitIndices.clear();
                cg.relOffsets.clear();

                // Collect selected units and calculate their centroid
                Vector2 centroid={0,0}; int cnt=0;
                for(int i=0;i<(int)g_battle.playerUnits.size();i++){
                    if(!g_battle.playerUnits[i].selected) continue;
                    int alive=0;
                    for(auto& s:g_battle.playerUnits[i].soldiers) if(s.alive) alive++;
                    if(alive==0) continue;
                    centroid=v2add(centroid,g_battle.playerUnits[i].anchorPos);
                    cnt++;
                    cg.unitIndices.push_back(i);
                }
                if(cnt>0){
                    centroid=v2scale(centroid,1.f/(float)cnt);
                    for(int idx:cg.unitIndices){
                        cg.relOffsets.push_back(v2sub(g_battle.playerUnits[idx].anchorPos,centroid));
                    }
                    cg.active=true;
                }
            } else {
                // ── RECALL GROUP ────────────────────────────────────────────
                ControlGroup& cg=g_battle.controlGroups[g];
                if(!cg.active||cg.unitIndices.empty()) continue;

                // Deselect all, then select only this group's surviving units
                for(auto& bu:g_battle.playerUnits) bu.selected=false;
                for(int idx:cg.unitIndices){
                    if(!validIdx(idx,g_battle.playerUnits)) continue;
                    int alive=0;
                    for(auto& s:g_battle.playerUnits[idx].soldiers) if(s.alive) alive++;
                    if(alive>0) g_battle.playerUnits[idx].selected=true;
                }
                g_battle.activeControlGroup=g;
                playSfx(SFX_SELECT,0.8f,1.1f);
            }
        }
    }

    if(!g_battle.battleOver){
        // Selection
        bool overHud=(mouse.y>SCREEN_H-HUD_H||mouse.y<TOPBAR_H);
        if(!overHud){
            // Convert mouse to world coords
            Vector2 worldMouse=screenToWorldBattle(mouse);

            if(IsMouseButtonPressed(MOUSE_LEFT_BUTTON)){
                g_battle.selStart=worldMouse;
                g_battle.dragging=true;
                for(auto& bu:g_battle.playerUnits) bu.selected=false;
                g_battle.activeControlGroup=-1; // manual selection breaks group mode
            }
            if(IsMouseButtonDown(MOUSE_LEFT_BUTTON)&&g_battle.dragging){
                g_battle.selRect.x=fminf(worldMouse.x,g_battle.selStart.x);
                g_battle.selRect.y=fminf(worldMouse.y,g_battle.selStart.y);
                g_battle.selRect.width=fabsf(worldMouse.x-g_battle.selStart.x);
                g_battle.selRect.height=fabsf(worldMouse.y-g_battle.selStart.y);
            }
            if(IsMouseButtonReleased(MOUSE_LEFT_BUTTON)){
                g_battle.dragging=false;
                bool gotSel=false;
                if(g_battle.selRect.width<8&&g_battle.selRect.height<8){
                    // Single click
                    float best=9999; int bidx=-1;
                    for(int i=0;i<(int)g_battle.playerUnits.size();i++){
                        float d=vdist(worldMouse,g_battle.playerUnits[i].anchorPos);
                        if(d<best&&d<40){best=d;bidx=i;}
                    }
                    if(bidx>=0){ g_battle.playerUnits[bidx].selected=true; gotSel=true; }
                } else {
                    for(auto& bu:g_battle.playerUnits){
                        if(ptInRect(bu.anchorPos,g_battle.selRect)){ bu.selected=true; gotSel=true; }
                    }
                }
                if(gotSel) playSfx(SFX_SELECT,0.9f);
                g_battle.selRect={};
            }
            if(IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)){
                // Check for enemy target
                int eIdx=-1; float bestD=9999;
                for(int i=0;i<(int)g_battle.enemyUnits.size();i++){
                    float d=vdist(worldMouse,g_battle.enemyUnits[i].anchorPos);
                    if(d<bestD&&d<40){bestD=d;eIdx=i;}
                }
                bool anySel=false;
                for(auto& bu:g_battle.playerUnits) if(bu.selected){ anySel=true; break; }

                // Determine if the selected units are all from the active control group
                // (so we can preserve their saved relative positions)
                int acg=g_battle.activeControlGroup;
                bool useGroupOffsets=false;
                if(acg>=0&&acg<9&&g_battle.controlGroups[acg].active&&eIdx<0){
                    // All currently selected units must be in this control group
                    const ControlGroup& cg=g_battle.controlGroups[acg];
                    useGroupOffsets=!cg.unitIndices.empty();
                    for(auto& bu:g_battle.playerUnits){
                        if(!bu.selected) continue;
                        bool inGroup=false;
                        for(int idx:cg.unitIndices){
                            if(validIdx(idx,g_battle.playerUnits)&&&g_battle.playerUnits[idx]==&bu){
                                inGroup=true; break;
                            }
                        }
                        if(!inGroup){ useGroupOffsets=false; break; }
                    }
                }

                if(useGroupOffsets){
                    // ── CONTROL-GROUP MOVE: preserve saved relative positions ──
                    const ControlGroup& cg=g_battle.controlGroups[acg];
                    // The click point is the new centroid of the group
                    Vector2 newCentroid=worldMouse;
                    for(int gi=0;gi<(int)cg.unitIndices.size();gi++){
                        int idx=cg.unitIndices[gi];
                        if(!validIdx(idx,g_battle.playerUnits)) continue;
                        BattleUnit& bu=g_battle.playerUnits[idx];
                        int alive=0; for(auto& s:bu.soldiers) if(s.alive) alive++;
                        if(alive==0) continue;
                        Vector2 target=v2add(newCentroid,cg.relOffsets[gi]);
                        bu.orderAttack=-1;
                        bu.hasExplicitOrder=true;
                        bu.orderType=1;
                        bu.orderTarget=target;
                        bu.groupState=UGS_ADVANCING;
                        Vector2 moveDir=vnorm(v2sub(target,bu.anchorPos));
                        bu.formationFacing=dirToAngle(moveDir);
                        auto slots=calcFormationSlots(bu.anchorPos,(int)bu.soldiers.size(),bu.formationFacing);
                        for(int s=0;s<(int)bu.soldiers.size()&&s<(int)slots.size();s++)
                            bu.soldiers[s].formationSlot=slots[s];
                        for(auto& sol:bu.soldiers){
                            sol.attackTargetUnit=-1;
                            sol.attackTargetSoldier=-1;
                            sol.targetRefreshTimer=0.f;
                            sol.state=SS_MOVING_TARGET;
                        }
                    }
                } else {
                    // ── NORMAL MOVE / ATTACK ORDER ────────────────────────────
                    int offX=0;
                    for(auto& bu:g_battle.playerUnits){
                        if(!bu.selected) continue;
                        if(eIdx>=0){
                            // Attack order
                            bu.orderAttack=eIdx;
                            bu.hasExplicitOrder=true;
                            bu.orderType=0;
                            bu.groupState=UGS_ADVANCING;
                            bu.orderTarget=bu.anchorPos;
                            for(auto& sol:bu.soldiers){
                                sol.attackTargetUnit=-1;
                                sol.attackTargetSoldier=-1;
                                sol.targetRefreshTimer=0.f;
                            }
                        } else {
                            // Movement order
                            bu.orderAttack=-1;
                            bu.hasExplicitOrder=true;
                            bu.orderType=1;
                            bu.orderTarget={worldMouse.x+(float)offX,worldMouse.y};
                            bu.groupState=UGS_ADVANCING;
                            Vector2 moveDir=vnorm(v2sub(bu.orderTarget,bu.anchorPos));
                            bu.formationFacing=dirToAngle(moveDir);
                            auto slots=calcFormationSlots(bu.anchorPos,(int)bu.soldiers.size(),bu.formationFacing);
                            for(int s=0;s<(int)bu.soldiers.size()&&s<(int)slots.size();s++)
                                bu.soldiers[s].formationSlot=slots[s];
                            for(auto& sol:bu.soldiers){
                                sol.attackTargetUnit=-1;
                                sol.attackTargetSoldier=-1;
                                sol.targetRefreshTimer=0.f;
                                sol.state=SS_MOVING_TARGET;
                            }
                            offX=(offX>0)?(-offX):((-offX)+50);
                        }
                    }
                }
                // Any manual order clears the active control group state (selection changed intent)
                if(eIdx<0) g_battle.activeControlGroup=-1;
                if(anySel) playSfx(eIdx>=0?SFX_ORDER_ATTACK:SFX_ORDER_MOVE,0.9f);
            }
            // 6.6: Shift+RClick to set formation facing angle
            if(IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)&&IsKeyDown(KEY_LEFT_SHIFT)){
                for(auto& bu:g_battle.playerUnits){
                    if(!bu.selected) continue;
                    Vector2 dir=vnorm(v2sub(worldMouse,bu.anchorPos));
                    bu.formationFacing=dirToAngle(dir);
                    // Recalc formation slots with new facing
                    auto slots=calcFormationSlots(bu.anchorPos,(int)bu.soldiers.size(),bu.formationFacing);
                    for(int s=0;s<(int)bu.soldiers.size()&&s<(int)slots.size();s++)
                        bu.soldiers[s].formationSlot=slots[s];
                }
            }
        }

        // Update
        profAdd(PROF_INPUT,(GetTime()-t0)*1000.0); t1=GetTime();
        updateEnemyAI(eff);
        updateBattleUnits(g_battle.playerUnits,g_battle.enemyUnits,eff);
        updateBattleUnits(g_battle.enemyUnits,g_battle.playerUnits,eff);
        double tSep=GetTime();
        separateAll();
        profAdd(PROF_SEP,(GetTime()-tSep)*1000.0);
        fxUpdate(eff);
        fxBattleDust(eff);
        atmosUpdate(eff);

        // Update projectiles
        for(auto& p:g_battle.projs){
            if(!p.alive) continue;
            p.pos=v2add(p.pos,v2scale(p.vel,eff));
            if(p.pos.x<0||p.pos.x>BATTLE_W||p.pos.y<0||p.pos.y>BATTLE_H){p.alive=false;continue;}
            auto& targets=p.fromPlayer?g_battle.enemyUnits:g_battle.playerUnits;
            for(auto& bu:targets){
                if(!p.alive) break;
                for(auto& sol:bu.soldiers){
                    if(!sol.alive) continue;
                    if(vdist(p.pos,sol.pos)<14.f){
                        sol.hp-=p.dmg;
                        p.alive=false;
                        playSfx(SFX_HIT_FLESH,0.7f);
                        fxSpawnBlood(sol.pos,5);
                        if(sol.hp<=0){
                            sol.alive=false;
                            playSfx(SFX_DEATH,0.8f);
                            fxSpawnBlood(sol.pos,9);
                            bool isCav=g_unitTypes[bu.typeIdx].spriteBase==SPR_CAVALRY;
                            g_battle.dead.push_back({sol.pos,1.f,8.f,isCav,
                                                     sol.angle,bu.typeIdx,
                                                     bu.isPlayer?0:1});
                        }
                        break;
                    }
                }
            }
        }
        // 2.5: Projectile cleanup with swap-with-back (faster than erase)
        for(int pi=(int)g_battle.projs.size()-1;pi>=0;pi--){
            if(!g_battle.projs[pi].alive){
                g_battle.projs[pi]=g_battle.projs.back();
                g_battle.projs.pop_back();
            }
        }

        // Fade dead markers (2.5: swap-with-back)
        for(auto& d:g_battle.dead){d.timer-=eff;d.alpha=d.timer/8.f;}
        for(int di=(int)g_battle.dead.size()-1;di>=0;di--){
            if(g_battle.dead[di].timer<=0){
                g_battle.dead[di]=g_battle.dead.back();
                g_battle.dead.pop_back();
            }
        }

        // Check win / lose
        int pa=0,ea=0;
        for(auto& bu:g_battle.playerUnits) for(auto& s:bu.soldiers) if(s.alive) pa++;
        for(auto& bu:g_battle.enemyUnits) for(auto& s:bu.soldiers) if(s.alive) ea++;
        g_profSoldiers=pa+ea;
        if(pa==0){g_battle.battleOver=true;g_battle.playerWon=false;
            battleLogAdd("DEFEAT — all forces destroyed!");}
        if(ea==0&&pa>0){g_battle.battleOver=true;g_battle.playerWon=true;
            battleLogAdd("VICTORY — all enemies routed!");
            // 6.2: Grant veterancy to survivors with >5 kills
            for(auto& bu:g_battle.playerUnits){
                int topKills=0;
                for(auto& s:bu.soldiers) if(s.alive) topKills=std::max(topKills,s.kills);
                if(topKills>5&&bu.veterancy<3){
                    bu.veterancy++;
                    char vbuf[128];
                    snprintf(vbuf,127,"[T%d] %s promoted to level %d!",
                        g_campaign.turn,g_unitTypes[bu.typeIdx].name,bu.veterancy);
                    battleLogAdd(vbuf);
                }
            }
        }
    }

    // DRAW
    profAdd(PROF_UPDATE,(GetTime()-t1)*1000.0); double t2=GetTime();
    beginBattleView();
    double tT=GetTime();
    drawBattlefield();
    profAdd(PROF_TERRAIN,(GetTime()-tT)*1000.0);
    double tU=GetTime();
    drawBattlefield();
    // Selection rect in world space
    if(g_battle.dragging&&(g_battle.selRect.width>8||g_battle.selRect.height>8)){
        DrawRectangleRec(g_battle.selRect,{100,180,255,25});
        DrawRectangleLinesEx(g_battle.selRect,1,C_ALLY);
    }
    drawAllUnits();
    fxDraw();
    drawSoldierDebug();
    atmosDrawWorld();
    endBattleView();
    profAdd(PROF_UNITS,(GetTime()-tU)*1000.0);
    atmosDrawScreen(SCREEN_W,SCREEN_H);

    drawBattleHUD(mouse);
    profAdd(PROF_DRAW,(GetTime()-t2)*1000.0);

    // Game over overlay
    if(g_battle.battleOver){
        g_battle.resultTimer+=dt;
        float fade=std::min(1.f,g_battle.resultTimer*0.8f);
        Color overlay=g_battle.playerWon?Color{0,80,0,(unsigned char)(int)(160*fade)}:
                                          Color{80,0,0,(unsigned char)(int)(160*fade)};
        DrawRectangle(0,0,SCREEN_W,SCREEN_H,overlay);
        if(fade>0.5f){
            const char* w=g_battle.playerWon?"VICTORY!":"DEFEAT!";
            int tsz=64;
            int tw2=MeasureText(w,tsz);
            Color tc=g_battle.playerWon?Color{0,228,48,255}:Color{230,41,55,255};
            DrawText(w,SCREEN_W/2-tw2/2+3,SCREEN_H/2-70+3,tsz,{0,0,0,200});
            DrawText(w,SCREEN_W/2-tw2/2,SCREEN_H/2-70,tsz,tc);
            if(drawButton({(float)(SCREEN_W/2-160),(float)(SCREEN_H/2+20),320,54},"VIEW RESULTS",mouse)){
                // Build result
                g_lastResult.playerWon=g_battle.playerWon;
                g_lastResult.playerLosses=0; g_lastResult.enemyLosses=0;
                g_lastResult.survivors.clear();
                g_lastResult.lootApplied=false; // 0.2: reset before entering result screen
                g_lastResult.autoresolved=false; // Fase D: batalla real, no auto
                for(auto& bu:g_battle.playerUnits){
                    int alive=0; for(auto& s:bu.soldiers) if(s.alive) alive++;
                    int dead=0;  for(auto& s:bu.soldiers) if(!s.alive) dead++;
                    g_lastResult.playerLosses+=dead;
                    g_lastResult.survivors.push_back({bu.typeIdx,alive});
                }
                for(auto& bu:g_battle.enemyUnits){
                    for(auto& s:bu.soldiers) if(!s.alive) g_lastResult.enemyLosses++;
                }
                g_lastResult.lootGold=g_battle.playerWon&&!g_battle.isDefense?
                    (50.f+frandMT()*100.f):0.f;
                g_lastResult.provinceIdx=g_battle.battleProvince;
                g_lastResult.isDefense=g_battle.isDefense;
                return STATE_BATTLE_RESULT;
            }
        }
    }

    return STATE_BATTLE;
}

// ═══════════════════════════════════════════════════════════════════════════
//  STATE: BATTLE RESULT
// ═══════════════════════════════════════════════════════════════════════════
GameState updateDrawBattleResult(Vector2 mouse){
    ClearBackground({8,6,4,255});
    // Gradient overlay — single GPU call instead of per-line loop (2.1)
    Color topCol=g_lastResult.playerWon?Color{0,30,10,255}:Color{30,0,0,255};
    DrawRectangleGradientV(0,0,SCREEN_W,SCREEN_H,topCol,C_BG);

    // Title
    const char* ttl=g_lastResult.playerWon?"VICTORY":"DEFEAT";
    int tsz=60;
    int ttw=MeasureText(ttl,tsz);
    Color tc=g_lastResult.playerWon?Color{80,220,80,255}:Color{220,60,60,255};
    DrawText(ttl,SCREEN_W/2-ttw/2+3,40+3,tsz,{0,0,0,160});
    DrawText(ttl,SCREEN_W/2-ttw/2,40,tsz,tc);
    DrawLine(SCREEN_W/2-200,110,SCREEN_W/2+200,110,C_GOLD);
    if(g_lastResult.autoresolved){
        const char* ar="(auto-resolved)";
        DrawText(ar,SCREEN_W/2-MeasureText(ar,14)/2,114,14,C_SECONDARY);
    }

    // Stats
    DrawText(TextFormat("Enemy soldiers killed: %d",g_lastResult.enemyLosses),
             SCREEN_W/2-220,130,16,C_PARCHMENT);
    DrawText(TextFormat("Friendly losses: %d",g_lastResult.playerLosses),
             SCREEN_W/2-220,156,16,C_PARCHMENT);
    if(g_lastResult.lootGold>0){
        DrawText(TextFormat("Loot collected: %.0f gold",g_lastResult.lootGold),
                 SCREEN_W/2-220,182,16,C_GOLD);
    }

    // Survivor list
    DrawText("Surviving units:",SCREEN_W/2-220,220,15,C_GOLD);
    for(int i=0;i<(int)g_lastResult.survivors.size();i++){
        auto [ti,cnt]=g_lastResult.survivors[i];
        if(ti>=unitTypeCount()) continue;
        const UnitTypeDef& td=g_unitTypes[ti];
        Color col={td.r,td.g,td.b,255};
        DrawRectangle(SCREEN_W/2-220,240+i*24,18,18,col);
        DrawText(TextFormat("%s  —  %d soldiers surviving",td.name,cnt),
                 SCREEN_W/2-196,242+i*24,14,C_PARCHMENT);
    }

    // Province change
    if(g_lastResult.playerWon&&!g_lastResult.isDefense&&g_lastResult.provinceIdx>=0
       &&g_lastResult.provinceIdx<(int)g_campaign.provinces.size()){
        Province& prov=g_campaign.provinces[g_lastResult.provinceIdx];
        DrawText(TextFormat("Province captured: %s",prov.name),
                 SCREEN_W/2-220,240+(int)g_lastResult.survivors.size()*24+20,16,C_GOLD);
        // Actually update ownership
        if(prov.owner!=FACTION_PLAYER){
            prov.owner=FACTION_PLAYER;
            prov.army.clear(); // guarnición conquistada
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
    // Fase J: derrota en defensa — el atacante ocupa la provincia
    if(!g_lastResult.playerWon&&g_lastResult.isDefense&&g_preBattle.attackerFaction>=0
       &&g_lastResult.provinceIdx>=0&&g_lastResult.provinceIdx<(int)g_campaign.provinces.size()){
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
                    g_campaign.armies.erase(g_campaign.armies.begin()+tAi);
                    if(g_campaign.selectedArmy>=tAi) g_campaign.selectedArmy--;
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
                else {
                    g_campaign.armies.erase(g_campaign.armies.begin()+dAi);
                    if(g_campaign.selectedArmy>=dAi) g_campaign.selectedArmy--;
                }
            }
            int sel=g_campaign.selectedArmy;
            selectArmy(sel>=0&&sel<(int)g_campaign.armies.size()?sel:-1);
        }
    }

    // Apply loot to campaign (0.2: was static bool, now per-result field)
    if(!g_lastResult.lootApplied){
        g_campaign.res.gold+=g_lastResult.lootGold;
        g_lastResult.lootApplied=true;
        // Update player army with survivors (so dead soldiers don't "resurrect")
        if(!g_quickBattle){
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
                    g_campaign.armies.erase(g_campaign.armies.begin()+ai);
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
        netMarkDirty(); // Fase H: batalla resuelta (captura de provincia + loot)
    }

    // Fase J: el rey ha caído
    if(g_kingLost){
        const char* kl="THE KING HAS FALLEN!";
        DrawText(kl,SCREEN_W/2-MeasureText(kl,20)/2,208,20,C_ENEMY_COL);
    }

    // (Survivor list and province capture already shown above; army updated in lootApplied block)

    // Continue button
    if(drawButton({(float)(SCREEN_W/2-140),(float)(SCREEN_H-80),280,54},"CONTINUE",mouse)){
        bool kingLost=g_kingLost;
        g_kingLost=false;
        g_lastResult.lootApplied=false;  // reset for next battle
        if(g_quickBattle){
            g_quickBattle=false;
            return STATE_MAIN_MENU;
        }
        if(kingLost) return STATE_DEFEAT;
        // Sin provincias → derrota (p.ej. la última cayó en esta defensa)
        int playerProv=0;
        for(auto& p:g_campaign.provinces) if(p.owner==FACTION_PLAYER) playerProv++;
        if(playerProv==0) return STATE_DEFEAT;
        return STATE_CAMPAIGN_MAP;
    }
    return STATE_BATTLE_RESULT;
}

// ═══════════════════════════════════════════════════════════════════════════
//  STATE: PRE-BATTLE
// ═══════════════════════════════════════════════════════════════════════════
GameState updateDrawPreBattle(Vector2 mouse){
    ClearBackground(C_BG);
    for(int gx=0;gx<SCREEN_W;gx+=40) DrawLine(gx,0,gx,SCREEN_H,{20,18,14,50});
    for(int gy=0;gy<SCREEN_H;gy+=40) DrawLine(0,gy,SCREEN_W,gy,{20,18,14,50});

    DrawRectangle(0,0,SCREEN_W,50,{0,0,0,220});
    DrawText("PRE-BATTLE DEPLOYMENT",14,10,26,C_GOLD);
    if(g_preBattle.provinceIdx>=0&&g_preBattle.provinceIdx<(int)g_campaign.provinces.size())
        DrawText(g_campaign.provinces[g_preBattle.provinceIdx].name,14,38,12,C_SECONDARY);

    float halfH=(SCREEN_H-50)/2.f;
    // Top half: player units
    DrawRectangle(0,50,SCREEN_W,(int)halfH,{8,12,20,200});
    DrawText("YOUR FORCES — Select units to deploy (max 12 groups):",14,58,14,C_ALLY);
    DrawLine(0,50+halfH,(int)SCREEN_W,50+(int)halfH,{80,65,30,120});

    int deployCount=0;
    for(bool b:g_preBattle.include) if(b) deployCount++;

    // Ensure include vector sized
    g_preBattle.include.resize(g_campaign.readyUnits.size(),false);

    for(int i=0;i<(int)g_campaign.readyUnits.size();i++){
        auto [ti,cnt]=g_campaign.readyUnits[i];
        if(ti>=unitTypeCount()) continue;
        const UnitTypeDef& td=g_unitTypes[ti];
        float ry=80.f+i*36.f;
        if(ry+36>50+halfH) break;
        Color rowbg=(i%2==0)?Color{10,15,25,200}:Color{8,12,20,200};
        DrawRectangle(0,(int)ry,SCREEN_W-220,34,rowbg);
        DrawRectangle(8,(int)(ry+8),20,20,{td.r,td.g,td.b,255});
        DrawText(td.name,34,(int)(ry+10),13,C_PARCHMENT);
        DrawText(TextFormat("%d soldiers",cnt),280,(int)(ry+10),12,C_SECONDARY);
        // Checkbox
        bool sel=g_preBattle.include[i];
        Rectangle cb={(float)(SCREEN_W-210),(float)(ry+6),22,22};
        bool chv=ptInRect(mouse,cb);
        DrawRectangleRec(cb,sel?Color{30,80,30,255}:chv?Color{22,48,22,255}:Color{14,28,14,220});
        DrawRectangleLinesEx(cb,1,sel?C_GOLD:Color{60,90,40,255});
        if(sel) DrawText("X",(int)(cb.x+6),(int)(cb.y+3),14,C_GOLD);
        if(chv&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)){
            if(!sel&&deployCount<12) g_preBattle.include[i]=true;
            else if(sel) g_preBattle.include[i]=false;
        }
        // Stats brief
        DrawText(TextFormat("Atk:%d Def:%d HP:%.0f",td.meleeAttack,td.meleeDefense,
                 td.hpPerSoldier*cnt),(int)(SCREEN_W-190),(int)(ry+10),11,C_SECONDARY);
    }

    // Bottom half: enemy info
    DrawRectangle(0,(int)(50+halfH),SCREEN_W,(int)halfH,{20,8,8,200});
    DrawText("ENEMY FORCES:",14,(int)(58+halfH),14,C_ENEMY_COL);
    if(g_preBattle.fogOfWar){
        DrawText(TextFormat("Estimated strength: ~%d soldiers",g_preBattle.estimatedEnemyStrength),
                 14,(int)(84+halfH),14,C_SECONDARY);
        DrawText("(Scout reports unreliable — deploy explorers for better intel)",
                 14,(int)(106+halfH),12,{100,80,60,255});
    } else {
        // Show actual army (Fase J: snapshot del enemigo, no la guarnición)
        for(int i=0;i<(int)g_preBattle.enemyUnits.size();i++){
            auto [ti,cnt]=g_preBattle.enemyUnits[i];
            if(ti>=unitTypeCount()) continue;
            const UnitTypeDef& td=g_unitTypes[ti];
            float ry=84.f+halfH+i*28.f;
            DrawRectangle(8,(int)(ry+4),16,16,{td.r,td.g,td.b,100});
            DrawText(td.name,30,(int)(ry+6),13,C_PARCHMENT);
            DrawText(TextFormat("x%d",cnt),280,(int)(ry+6),12,C_ENEMY_COL);
        }
    }

    // Bottom buttons
    int bY=SCREEN_H-60;
    DrawRectangle(0,bY,SCREEN_W,60,{0,0,0,200});
    DrawText(TextFormat("Deploying: %d / 12 groups",deployCount),14,(int)(bY+22),13,C_SECONDARY);

    bool canStart=(deployCount>0);

    // Fase D: predicción de victoria estilo RISK (según lo desplegado)
    int piP=g_preBattle.provinceIdx;
    bool piOk=(piP>=0&&piP<(int)g_campaign.provinces.size());
    std::vector<std::pair<int,int>> predP, predE;
    for(int i=0;i<(int)g_campaign.readyUnits.size();i++)
        if(i<(int)g_preBattle.include.size()&&g_preBattle.include[i])
            predP.push_back(g_campaign.readyUnits[i]);
    TerrainType terP=TERRAIN_PLAIN; float defP=1.f;
    if(piOk){
        terP=g_campaign.provinces[piP].terrain;
        if(g_campaign.provinces[piP].hasCity) defP=g_campaign.provinces[piP].city.defBonus;
        if(g_preBattle.fogOfWar){
            // Composición desconocida: proxy con tropa base + estimación de exploradores
            int est=g_preBattle.estimatedEnemyStrength; if(est<0) est=0;
            if(unitTypeCount()>0) predE.push_back({0,est});
        } else predE=g_preBattle.enemyUnits;
    }
    if(canStart){
        float pch=predictBattle(predP,predE,terP,g_preBattle.isDefense,defP);
        int pct=(int)(pch*100.f+0.5f);
        Color pcol= pch>=0.60f?Color{90,220,90,255}:
                   (pch>=0.40f?Color{230,200,90,255}:Color{230,90,90,255});
        DrawText(TextFormat("Predicted victory: %d%%",pct),230,(int)(bY+22),13,pcol);
    }

    Color cn=canStart?Color{30,65,30,255}:Color{30,30,30,255};
    Color ch=canStart?Color{55,120,50,255}:Color{30,30,30,255};
    // Fase D: AUTO-RESOLVE — resuelve la batalla al instante con la misma predicción
    if(drawButton({(float)(SCREEN_W-700),(float)(bY+6),230,46},"AUTO-RESOLVE",mouse,
                  canStart?Color{70,55,25,255}:Color{30,30,30,255},
                  canStart?Color{125,100,45,255}:Color{30,30,30,255})&&canStart){
        std::vector<std::pair<int,int>> playerGroups=predP;
        std::vector<std::pair<int,int>> enemyGroups=g_preBattle.enemyUnits;
        autoResolveBattle(playerGroups,enemyGroups,terP,piP,g_preBattle.isDefense,defP);
        netMarkDirty(); // Fase H: batalla auto-resuelta en el host
        return STATE_BATTLE_RESULT;
    }
    if(drawButton({(float)(SCREEN_W-450),(float)(bY+6),240,46},"START BATTLE",mouse,cn,ch)&&canStart){
        // Build player groups from selection
        std::vector<std::pair<int,int>> playerGroups, enemyGroups;
        for(int i=0;i<(int)g_campaign.readyUnits.size();i++){
            if(i<(int)g_preBattle.include.size()&&g_preBattle.include[i])
                playerGroups.push_back(g_campaign.readyUnits[i]);
        }
        int pi=g_preBattle.provinceIdx;
        enemyGroups=g_preBattle.enemyUnits;
        char sname[64];
        snprintf(sname,63,"Battle of %s",
                 (pi>=0&&pi<(int)g_campaign.provinces.size())?g_campaign.provinces[pi].name:"Unknown");
        TerrainType ter=(pi>=0&&pi<(int)g_campaign.provinces.size())?
                         g_campaign.provinces[pi].terrain:TERRAIN_PLAIN;
        initBattle(playerGroups,enemyGroups,ter,pi,g_preBattle.isDefense,sname);
        return STATE_BATTLE;
    }
    if(drawButton({(float)(SCREEN_W-200),(float)(bY+6),186,46},"RETREAT",mouse,{55,20,20,255},{90,35,35,255})){
        if(g_preBattle.isDefense&&g_preBattle.provinceIdx>=0&&
           g_preBattle.provinceIdx<(int)g_campaign.provinces.size()){
            // Lose province — Fase J: pasa al atacante (no a neutral)
            int pi=g_preBattle.provinceIdx;
            Province& prov=g_campaign.provinces[pi];
            prov.owner=g_preBattle.attackerFaction>=0?
                       (FactionId)g_preBattle.attackerFaction:FACTION_NEUTRAL;
            prov.army.clear();
            // El ejército atacante ocupa
            int tAi=g_preBattle.attackerArmyIdx;
            if(tAi>=0&&tAi<(int)g_campaign.armies.size()){
                g_campaign.armies[tAi].province=pi;
                g_campaign.armies[tAi].moved=true;
            }
            // Los ejércitos del jugador se retiran a provincia propia
            int dAi=g_preBattle.armyIdx;
            if(dAi>=0&&dAi<(int)g_campaign.armies.size()){
                int rp=safeRetreatProvince(pi);
                if(rp>=0) g_campaign.armies[dAi].province=rp;
                else {
                    g_campaign.armies.erase(g_campaign.armies.begin()+dAi);
                    if(g_campaign.selectedArmy>=dAi) g_campaign.selectedArmy--;
                }
            }
            int sel=g_campaign.selectedArmy;
            selectArmy(sel>=0&&sel<(int)g_campaign.armies.size()?sel:-1);
        }
        netMarkDirty(); // Fase H: retirada resuelta (puede perder provincia)
        return STATE_CAMPAIGN_MAP;
    }
    return STATE_PRE_BATTLE;
}

// ═══════════════════════════════════════════════════════════════════════════
//  STATE: CAMPAIGN MAP
// ═══════════════════════════════════════════════════════════════════════════
const float ARMY_MOVE_DURATION = 0.9f;

GameState updateDrawCampaignMap(Vector2 mouse,float dt){
    updateProvinceCenters();

    // F5: guardar partida en cualquier momento (feedback breve en consola)
    if(IsKeyPressed(KEY_F5)){
        if(saveGame()) printf("[SAVE] Game saved to %s\n",getCampaignSavePath(g_campaign.campaignId));
        else printf("[SAVE] ERROR: could not write %s\n",getCampaignSavePath(g_campaign.campaignId));
    }

    // Army movement animation: advance and complete when done
    bool armyMoving = (g_campaign.armyMoveFrom>=0 && g_campaign.armyMoveTo>=0);
    if(armyMoving){
        g_campaign.armyMoveT += dt/ARMY_MOVE_DURATION;
        if(g_campaign.armyMoveT>=1.f){
            int to = g_campaign.armyMoveTo;
            int mi = g_campaign.armyMoveIdx;
            g_campaign.armyMoveFrom = -1;
            g_campaign.armyMoveTo = -1;
            g_campaign.armyMoveIdx = -1;
            g_campaign.armyMoveT = 0.f;
            // Fase J: el movimiento consume el turno DEL EJÉRCITO (moved),
            // no avanza el turno global — eso lo hace END TURN
            if(mi>=0&&mi<(int)g_campaign.armies.size()){
                g_campaign.armies[mi].province=to;
                g_campaign.armies[mi].moved=true;
                selectArmy(mi);
            } else {
                g_campaign.playerProvince = to;
            }
            netMarkDirty(); // Fase H: retransmitir nuevo estado
            if(!g_campaign.provinces[to].army.empty()){
                // Guarnición hostil en provincia propia (legado)
                g_preBattle.provinceIdx=to;
                g_preBattle.isDefense=true;
                g_preBattle.fogOfWar=false;
                g_preBattle.estimatedEnemyStrength=0;
                for(auto [ti,c]:g_campaign.provinces[to].army) g_preBattle.estimatedEnemyStrength+=c;
                g_preBattle.attackerFaction=-1;
                g_preBattle.attackerArmyIdx=-1;
                g_preBattle.armyIdx=mi;
                g_preBattle.hadKing=(mi>=0&&mi<(int)g_campaign.armies.size()&&
                                     armyHasKing(g_campaign.armies[mi].units));
                g_preBattle.enemyUnits=g_campaign.provinces[to].army;
                g_preBattle.include.clear();
                g_preBattle.include.resize(g_campaign.readyUnits.size(),true);
                for(int j=12;j<(int)g_preBattle.include.size();j++)
                    g_preBattle.include[j]=false;
                return STATE_PRE_BATTLE;
            }
        }
    }

    // Fase C: mar animado de fondo (antes pergamino+grid)
    drawCampaignSea(g_menuTime);
    static float fogX=0;
    fogX+=8.f*dt;
    for(int i=0;i<8;i++){
        float fx=fmodf(fogX+i*200.f,(float)SCREEN_W+400)-200;
        DrawRectangle((int)fx,0,110,SCREEN_H,{140,170,195,(unsigned char)(4+i*2)});
    }

    // Fase C: continente tipo Risk — celdas Voronoi + fronteras + costa
    drawCampaignContinent(g_campaign.campaignId);

    // Roads (adjacencies) — thicker, earth tone
    for(int i=0;i<(int)g_campaign.provinces.size();i++){
        auto& p=g_campaign.provinces[i];
        for(int adj:p.adjacent){
            if(adj>i){
                Vector2 mid = {(p.center.x+g_campaign.provinces[adj].center.x)*0.5f,
                              (p.center.y+g_campaign.provinces[adj].center.y)*0.5f};
                DrawLineEx(p.center,mid,3.5f,{70,55,35,180});
                DrawLineEx(mid,g_campaign.provinces[adj].center,3.5f,{70,55,35,180});
            }
        }
    }

    // Update explored provinces (6.3)
    for(int i=0;i<(int)g_campaign.provinces.size()&&i<32;i++){
        if(g_campaign.provinces[i].owner==FACTION_PLAYER) g_campaign.explored[i]=true;
        // Adjacent to player province are also explored
        for(int adj:g_campaign.provinces[g_campaign.playerProvince].adjacent){
            if(adj<32) g_campaign.explored[adj]=true;
        }
        if((int)g_campaign.playerProvince<32) g_campaign.explored[g_campaign.playerProvince]=true;
    }

    // Fase C: decor + mini-ciudad + labels sobre las celdas del continente
    for(int i=0;i<(int)g_campaign.provinces.size();i++){
        auto& p=g_campaign.provinces[i];
        bool explored=(i<32&&g_campaign.explored[i]);
        Color oc=factionDisplayColor(p.owner); // Fase D+: aliados => color del jugador
        if(!explored) oc={40,40,40,255};
        float rad=38.f; // radio de decor/labels (la celda la dibuja el continente)
        if(explored) drawTerrainDecor(i,p.terrain,p.center,rad);
        if(g_selectedProvince==i){
            float pulse=2.f+sinf(g_menuTime*4.f)*1.5f;
            drawProvinceCellOutline(i,C_GOLD,2.5f+pulse);
        }
        if(p.hasCity&&explored)
            drawMiniCity(p.center,cityBuildLevel(p.city),p.owner,g_menuTime);
        else if(explored){
            static const char* terrIcons[4]={"~","T","^","W"};
            DrawText(terrIcons[(int)p.terrain],(int)(p.center.x-5),(int)(p.center.y-8),18,{240,235,200,230});
        }
        // Name (scale-aware spacing below circle)
        int tw=MeasureText(p.name,11);
        float nameY=p.center.y+rad+uiPx(4.f);
        DrawText(p.name,(int)(p.center.x-tw/2),(int)nameY,11,explored?C_PARCHMENT:Color{100,100,100,255});
        // Player marker (below name; hide when army is moving — icon shows position)
        if(i==g_campaign.playerProvince && !armyMoving){
            drawProvinceCellOutline(i,C_ALLY,3.f);
            float youY=nameY+(float)uiFS(11)+uiPx(4.f);
            DrawText("YOU",(int)(p.center.x-12),(int)youY,11,C_ALLY);
        }
        // Army indicator — only if explored (6.3)
        if(!p.army.empty()&&explored){
            int total=0; for(auto [ti,c]:p.army) total+=c;
            DrawText(TextFormat("⚔%d",total),(int)(p.center.x-12),(int)(p.center.y-rad-uiPx(16.f)),11,
                     factionIsPlayerSide(p.owner)?C_ALLY:C_ENEMY_COL); // Fase D+
        } else if(!p.army.empty()&&!explored){
            DrawText("?",(int)(p.center.x-4),(int)(p.center.y-rad-uiPx(16.f)),11,{80,80,80,255});
        }
    }

    // Fase J: badges de ejércitos de campo — selección y estado (moved)
    {
        auto armiesAtProv=[&](int prov)->std::vector<int>{
            std::vector<int> v;
            for(int i=0;i<(int)g_campaign.armies.size();i++)
                if(g_campaign.armies[i].province==prov) v.push_back(i);
            return v;
        };
        auto badgeRect=[&](int prov,int k,int cnt)->Rectangle{
            float w=40.f,h=18.f,gap=4.f;
            float total=(float)cnt*(w+gap)-gap;
            float bx=g_campaign.provinces[prov].center.x-total*0.5f+(float)k*(w+gap);
            float by=g_campaign.provinces[prov].center.y-38.f-38.f;
            return {bx,by,w,h};
        };
        for(int i=0;i<(int)g_campaign.provinces.size();i++){
            std::vector<int> list=armiesAtProv(i);
            if(list.empty()) continue;
            bool explored=(i<32&&g_campaign.explored[i]);
            for(int k=0;k<(int)list.size();k++){
                int idx=list[k];
                FieldArmy& a=g_campaign.armies[idx];
                // El ejército en animación no muestra badge en origen/destino
                if(armyMoving&&idx==g_campaign.armyMoveIdx) continue;
                bool isPlayerSide=(a.owner==FACTION_PLAYER);
                if(!isPlayerSide&&!explored) continue; // IA oculta sin explorar
                Rectangle br=badgeRect(i,k,(int)list.size());
                // Clic: solo ejércitos del jugador → seleccionar
                if(isPlayerSide&&!armyMoving&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)
                   &&ptInRect(mouse,br)){
                    selectArmy(idx);
                    return STATE_CAMPAIGN_MAP;
                }
                bool sel=(idx==g_campaign.selectedArmy);
                Color border=sel?C_GOLD:(a.moved?Color{110,110,110,255}:C_ALLY);
                if(!isPlayerSide) border=factionColors[(int)a.owner];
                DrawRectangle((int)br.x,(int)br.y,(int)br.width,(int)br.height,{15,22,30,235});
                DrawRectangleLinesEx(br,sel?2.f:1.f,border);
                char bl[12];
                snprintf(bl,11,"%d",armySoldiers(a.units));
                DrawText(bl,(int)(br.x+br.width/2)-MeasureText(bl,11)/2,(int)br.y+3,11,
                         isPlayerSide?C_PARCHMENT:C_ENEMY_COL);
            }
        }
    }

    // Moving army animation: draw icon between origin and destination
    if(armyMoving && g_campaign.armyMoveFrom>=0 && g_campaign.armyMoveTo<(int)g_campaign.provinces.size()){
        Vector2 from = g_campaign.provinces[g_campaign.armyMoveFrom].center;
        Vector2 to   = g_campaign.provinces[g_campaign.armyMoveTo].center;
        float t = g_campaign.armyMoveT;
        float ease = t*t*(3.f-2.f*t); // smooth step
        Vector2 pos = { from.x+(to.x-from.x)*ease, from.y+(to.y-from.y)*ease };
        Color armyCol = C_ALLY;
        DrawCircleV(pos,14,{armyCol.r,armyCol.g,armyCol.b,200});
        DrawCircleLines((int)pos.x,(int)pos.y,14,armyCol);
        for(int k=0;k<5;k++){
            float a=(float)k*0.4f+ g_menuTime*2.f;
            float rx=pos.x+cosf(a)*8.f, ry=pos.y+sinf(a)*8.f;
            DrawCircleV({rx,ry},4,{220,220,180,240});
        }
    }
    if(!armyMoving) drawCampaignMovementArrows();

    // Hover tooltip (scale-aware line spacing to avoid overlap)
    // Guard: fuera de las barras superior/inferior (la celda puede alcanzarlas;
    // los botones de UI se dibujan despues y deben ganar el clic)
    float hoverTop=uiPx(46.f), hoverBot=(float)SCREEN_H-uiPx(52.f);
    for(int i=0;i<(int)g_campaign.provinces.size();i++){
        auto& p=g_campaign.provinces[i];
        float rad=38.f; // Fase C: offset de tooltip
        bool explored=(i<32&&g_campaign.explored[i]);
        Color oc=diplomacyMapColor(p.owner); // Fase I: modo alianzas cambia solo el color
        if(!explored) oc={40,40,40,255};
        bool hov=(mouse.y>hoverTop && mouse.y<hoverBot && pointInProvinceCell(i,mouse));
        if(hov){
            // Highlight adjacent (borde pulsante de cada celda adyacente)
            for(int adj:p.adjacent){
                DrawLineEx(p.center,g_campaign.provinces[adj].center,2.f,C_GOLD);
                drawProvinceCellOutline(adj,{C_GOLD.r,C_GOLD.g,C_GOLD.b,80},2.f);
            }
            // Tooltip — scaled box and line spacing
            float lineH1=(float)uiFS(14)+uiPx(6.f);
            float lineH2=(float)uiFS(12)+uiPx(4.f);
            // Fase D: odds line needs tot + isAdj before sizing the box
            int tot=0; for(auto [ti,c]:p.army) tot+=c;
            bool isAdjHover=false;
            for(int adj:g_campaign.provinces[g_campaign.playerProvince].adjacent) if(adj==i) isAdjHover=true;
            // Fase J: solo el ejército seleccionado (y aún sin mover) puede actuar
            int selArmy=g_campaign.selectedArmy;
            bool armyCanAct=selArmy>=0&&selArmy<(int)g_campaign.armies.size()
                            &&!g_campaign.armies[selArmy].moved;
            // Fase I: sin odds sobre provincias aliadas (ni propias)
            bool showOdds=isAdjHover&&!armyMoving&&!factionIsPlayerSide(p.owner)
                          &&armyCanAct&&explored;
            float twW=uiPx(240.f), twH=lineH1+lineH2*(showOdds?6.f:5.f)+uiPx(8.f);
            float tx=mouse.x+10, ty=mouse.y-twH-8;
            if(ty<uiPx(4.f)) ty=mouse.y+rad+uiPx(8.f);
            DrawRectangle((int)(tx-4),(int)(ty-4),(int)(twW+8),(int)(twH+8),{0,0,0,200});
            DrawRectangleLinesEx({tx-4,ty-4,twW+8,twH+8},1,C_GOLD);
            DrawText(p.name,(int)tx,(int)ty,14,C_GOLD); ty+=lineH1;
            DrawText(TextFormat("Terrain: %s",terrainNames[(int)p.terrain]),(int)tx,(int)ty,12,C_SECONDARY); ty+=lineH2;
            DrawText(TextFormat("Owner: %s",factionNames[(int)p.owner]),(int)tx,(int)ty,12,oc); ty+=lineH2;
            if(p.hasCity) DrawText(TextFormat("City: %s",p.city.name),(int)tx,(int)ty,12,C_PARCHMENT); ty+=lineH2;
            if(tot>0) DrawText(TextFormat("Army: ~%d soldiers",tot),(int)tx,(int)ty,12,C_ENEMY_COL); ty+=lineH2;
            // Fase D: predicted victory if the player attacks this province
            if(showOdds){
                std::vector<std::pair<int,int>> tipP;
                for(int k=0;k<(int)g_campaign.readyUnits.size()&&k<12;k++)
                    tipP.push_back(g_campaign.readyUnits[k]);
                // Fase J: enemigo = guarnición + ejércitos de campo de la facción
                std::vector<std::pair<int,int>> tipE=p.army;
                for(auto& fa:g_campaign.armies)
                    if(fa.province==i&&fa.owner==p.owner&&fa.owner!=g_localFaction)
                        for(auto& u:fa.units) tipE.push_back(u);
                float db=p.hasCity?p.city.defBonus:1.f;
                float oc2=predictBattle(tipP,tipE,p.terrain,false,db);
                int opct=(int)(oc2*100.f+0.5f);
                Color opc= oc2>=0.60f?Color{90,220,90,255}:
                          (oc2>=0.40f?Color{230,200,90,255}:Color{230,90,90,255});
                DrawText(TextFormat("Predicted victory: %d%%",opct),(int)tx,(int)ty,12,opc); ty+=lineH2;
            }
            // Tooltip: estado del ejército seleccionado (Fase J: ya no avanza turno)
            if(isAdjHover&&!armyMoving){
                if(armyCanAct)
                    DrawText("Click to move here (1 move/turn)",(int)tx,(int)ty,11,{255,220,100,255});
                else if(selArmy>=0&&selArmy<(int)g_campaign.armies.size())
                    DrawText("Army already moved this turn",(int)tx,(int)ty,11,{230,140,80,255});
                else
                    DrawText("Select an army first",(int)tx,(int)ty,11,{230,140,80,255});
            }

            // Click to interact (ignored while army is moving)
            if(!armyMoving&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)){
                g_selectedProvince=i;
                bool isAdj=false;
                for(int adj:g_campaign.provinces[g_campaign.playerProvince].adjacent)
                    if(adj==i) isAdj=true;
                bool isPlayer=(i==g_campaign.playerProvince);

                if(isPlayer&&p.hasCity){
                    g_campaign.viewedCity=i;
                    return STATE_CITY_MANAGEMENT;
                } else if(isAdj){
                    if(p.owner==g_localFaction){
                        // Fase J: mover el ejército seleccionado (si aún puede)
                        if(armyCanAct){
                            if(netIsClient()){
                                // Fase H: el cliente envía el movimiento al host
                                NetCmd mc; mc.cmd=NET_CMD_MOVE_ARMY;
                                mc.a=g_campaign.playerProvince; mc.b=i; mc.c=selArmy;
                                netSessionSendCmd(mc);
                            } else {
                                g_campaign.armyMoveFrom=g_campaign.playerProvince;
                                g_campaign.armyMoveTo=i;
                                g_campaign.armyMoveIdx=selArmy;
                                g_campaign.armyMoveT=0.f;
                            }
                        }
                    } else if(armyCanAct&&!factionIsPlayerSide(p.owner)){
                        // Fase I: no se puede atacar a un aliado militar
                        if(netIsClient()){
                            // H2: el cliente pide el ataque al host
                            NetCmd ac; ac.cmd=NET_CMD_ATTACK;
                            ac.a=i; ac.b=selArmy; ac.c=0;
                            netSessionSendCmd(ac);
                        } else {
                        std::vector<std::pair<int,int>> eu=p.army;
                        for(auto& fa:g_campaign.armies)
                            if(fa.province==i&&fa.owner==p.owner&&fa.owner!=g_localFaction)
                                for(auto& u:fa.units) eu.push_back(u);
                        g_preBattle.provinceIdx=i;
                        g_preBattle.isDefense=false;
                        g_preBattle.fogOfWar=(p.owner!=FACTION_NEUTRAL);
                        g_preBattle.estimatedEnemyStrength=armySoldiers(eu)+(int)((frandMT()-0.5f)*20);
                        g_preBattle.attackerFaction=-1;
                        g_preBattle.attackerArmyIdx=-1;
                        g_preBattle.armyIdx=selArmy;
                        g_preBattle.hadKing=armyHasKing(g_campaign.armies[selArmy].units);
                        g_preBattle.enemyUnits=eu;
                        g_preBattle.include.clear();
                        g_preBattle.include.resize(g_campaign.readyUnits.size(),false);
                        // Auto-select all by default
                        for(int j=0;j<std::min((int)g_preBattle.include.size(),12);j++)
                            g_preBattle.include[j]=true;
                        return STATE_PRE_BATTLE;
                        }
                    }
                }
            }
        }
    }

    // Top bar: resources (scale-aware height and spacing to avoid overlap)
    float topBarH=uiPx(44.f);
    float topBarY=(topBarH-(float)uiFS(14))*0.5f;
    if(topBarY<uiPx(4.f)) topBarY=uiPx(4.f);
    DrawRectangle(0,0,SCREEN_W,(int)topBarH,{0,0,0,210});
    DrawText(TextFormat("Turn: %d",g_campaign.turn),(int)uiPx(10.f),(int)topBarY,14,C_GOLD);
    // PvP: el cliente ve sus propios recursos (clientRes); host/co-op ven res
    Resources& r=(g_campaign.pvpMode&&g_netSession.role==NET_ROLE_CLIENT)
                 ?g_campaign.clientRes:g_campaign.res;
    int turnW=MeasureText(TextFormat("Turn: %d",g_campaign.turn),14);
    float resX=uiPx(10.f)+(float)turnW+uiPx(24.f);
    DrawText(TextFormat("Gold: %.0f  Food: %.0f  Wood: %.0f  Stone: %.0f  Iron: %.0f",
             r.gold,r.food,r.wood,r.stone,r.iron),(int)resX,(int)topBarY,13,C_PARCHMENT);

    // Fase I: acceso a diplomacia y modo de mapa (este ultimo solo cambia
    // colores de las provincias; la logica no depende del modo)
    if(drawSmBtn({uiPx(660.f),uiPx(7.f),uiPx(150.f),uiPx(30.f)},"DIPLOMACY",mouse,
                 {35,30,55,255},{60,50,95,255})){
        g_diploMsg[0]='\0'; // sin mensaje previo al abrir el panel
        return STATE_DIPLOMACY;
    }
    if(drawSmBtn({uiPx(820.f),uiPx(7.f),uiPx(170.f),uiPx(30.f)},
                 g_mapMode?"MAP: ALLIANCES":"MAP: NORMAL",mouse,
                 g_mapMode?Color{30,60,45,255}:Color{25,35,55,255},
                 g_mapMode?Color{50,100,75,255}:Color{45,60,95,255})){
        g_mapMode=(g_mapMode==0)?1:0;
    }

    // Fase J: estado del ejército seleccionado (esquina superior derecha)
    if(g_campaign.selectedArmy>=0&&g_campaign.selectedArmy<(int)g_campaign.armies.size()
       &&g_campaign.armies[g_campaign.selectedArmy].owner==g_localFaction){
        FieldArmy& sa=g_campaign.armies[g_campaign.selectedArmy];
        // Numeración propia: solo ejércitos de la facción local (los IA no cuentan)
        int ord=0,ptot=0;
        for(int i=0;i<(int)g_campaign.armies.size();i++){
            if(g_campaign.armies[i].owner!=g_localFaction) continue;
            ptot++;
            if(i==g_campaign.selectedArmy) ord=ptot;
        }
        char abuf[80];
        snprintf(abuf,79,"Army %d/%d: %d soldiers%s",ord,ptot,armySoldiers(sa.units),
                 sa.moved?" - moved":"");
        int abw=MeasureText(abuf,13);
        DrawText(abuf,SCREEN_W-abw-12,(int)topBarY+1,13,sa.moved?C_SECONDARY:C_GOLD);
    }

    // Bottom buttons
    float botBarH=uiPx(50.f);
    float botBarY=(float)SCREEN_H-botBarH;
    DrawRectangle(0,(int)botBarY,SCREEN_W,(int)botBarH,{0,0,0,210});
    DrawLine(0,(int)botBarY,SCREEN_W,(int)botBarY,{80,65,30,160});

    float btnY=botBarY+(botBarH-uiPx(34.f))*0.5f;
    if(!armyMoving&&drawSmBtn({uiPx(10.f),btnY,uiPx(140.f),uiPx(34.f)},"END TURN",mouse,{20,50,20,255},{40,90,38,255})){
        if(netIsClient()){
            // Fase H: el cliente pide pasar turno; el host ejecuta processTurn
            NetCmd ec; ec.cmd=NET_CMD_END_TURN; ec.a=ec.b=ec.c=0;
            netSessionSendCmd(ec);
        }else{
        bool pending=processTurn(); // Fase J: true = la IA ataca => pre-battle
        netMarkDirty(); // Fase H: retransmitir a los clientes
        // Check victory/defeat after processing (facción local: PLAYER o clientFaction en PvP)
        int totalProv=(int)g_campaign.provinces.size();
        int playerProv=0;
        for(auto& p:g_campaign.provinces) if(p.owner==g_localFaction) playerProv++;
        if(playerProv>=(int)(totalProv*0.8f)){
            return STATE_VICTORY;
        }
        if(playerProv==0){
            return STATE_DEFEAT;
        }
        if(pending) return STATE_PRE_BATTLE;
        }
    }
    float bw=uiPx(120.f), bw2=uiPx(140.f), bh=uiPx(34.f);
    float bx=uiPx(10.f)+uiPx(140.f)+uiPx(10.f);
    if(drawSmBtn({bx,btnY,bw,bh},"CODEX",mouse)) return STATE_UNIT_CODEX;
    bx+=bw+uiPx(10.f);
    if(drawSmBtn({bx,btnY,bw,bh},"RECRUIT",mouse)){
        g_campaign.viewedCity=g_campaign.playerProvince;
        return STATE_RECRUITMENT;
    }
    bx+=bw+uiPx(10.f);
    if(drawSmBtn({bx,btnY,bw2,bh},"MARKETPLACE",mouse)) return STATE_MARKETPLACE;
    bx+=bw2+uiPx(10.f);
    // Fase J: disolver ejército seleccionado → reserva
    {
        bool haveSel=g_campaign.selectedArmy>=0&&g_campaign.selectedArmy<(int)g_campaign.armies.size();
        if(drawSmBtn({bx,btnY,bw,bh},"DISBAND",mouse,
                     haveSel?Color{50,25,25,255}:Color{25,25,25,255},
                     haveSel?Color{80,40,40,255}:Color{25,25,25,255})&&haveSel){
            if(netIsClient()){
                // Fase H: el cliente pide disolver; el host lo aplica
                NetCmd dc; dc.cmd=NET_CMD_DISBAND; dc.a=g_campaign.selectedArmy; dc.b=dc.c=0;
                netSessionSendCmd(dc);
            }else{
                disbandArmy(g_campaign.selectedArmy);
                netMarkDirty();
            }
        }
    }
    // SAVE + MENU (guardar antes de salir al menú principal)
    {
        float mX=(float)(SCREEN_W-uiPx(130.f));
        float sX=mX-uiPx(10.f)-bw;
        if(drawSmBtn({sX,btnY,bw,bh},"SAVE",mouse,{20,50,20,255},{40,90,38,255})){
            saveGame(); // g_hasSave se actualiza dentro de saveGame()
        }
        if(drawSmBtn({mX,btnY,bw,bh},"MENU",mouse,
                      {50,20,20,255},{90,38,38,255})){
            saveGame(); // guardar al volver al menú principal
            return STATE_MAIN_MENU;
        }
    }

    return STATE_CAMPAIGN_MAP;
}

// ═══════════════════════════════════════════════════════════════════════════
//  STATE: CITY MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════
GameState updateDrawCityManagement(Vector2 mouse){
    if(g_campaign.viewedCity<0||g_campaign.viewedCity>=(int)g_campaign.provinces.size())
        return STATE_CAMPAIGN_MAP;

    Province& prov=g_campaign.provinces[g_campaign.viewedCity];
    if(!prov.hasCity) return STATE_CAMPAIGN_MAP;
    City& city=prov.city;
    // PvP: el cliente gestiona sus recursos (clientRes); host/co-op usan res
    Resources& res=(g_campaign.pvpMode&&g_netSession.role==NET_ROLE_CLIENT)
                   ?g_campaign.clientRes:g_campaign.res;

    ClearBackground({12,10,8,255});
    // City illustration (procedural schematic)
    // Draw silhouette shapes
    DrawRectangle(0,0,SCREEN_W,120,{20,16,10,255});
    // Towers
    for(int t=0;t<5;t++){
        int tx=100+t*220;
        DrawRectangle(tx,20,40,100,{55,50,42,255});
        // Battlements
        for(int b=0;b<3;b++) DrawRectangle(tx+b*14,12,10,10,{55,50,42,255});
        // Window
        DrawRectangle(tx+14,50,12,18,{120,100,60,60});
    }
    // Walls connecting
    DrawRectangle(0,95,SCREEN_W,25,{45,40,32,255});
    DrawLine(0,96,SCREEN_W,96,{70,60,40,200});

    DrawText(TextFormat("City of %s",city.name),14,10,24,C_GOLD);
    DrawLine(0,124,SCREEN_W,124,{80,65,30,150});

    // Building grid (5x3 = 15 slots, but BLD_COUNT=14)
    float gridX=14, gridY=132;
    float cellW=(SCREEN_W-28)/5.f, cellH=110.f;

    for(int b=0;b<BLD_COUNT;b++){
        int col=b%5, row=b/5;
        float bx=gridX+col*cellW;
        float by=gridY+row*cellH;
        float bw=cellW-6, bh=cellH-6;

        bool built=city.built[b];
        bool constructing=(city.constructing==b);
        bool canBuild=!built&&!constructing;

        // Check prereq
        // Fix: use the fixed prereq table below
        static const int prereqs[BLD_COUNT]={-1,-1,1,-1,2,-1,5,5,4,-1,9,-1,-1,-1};
        bool prereqOk=(prereqs[b]<0||city.built[prereqs[b]]);
        // Check coast requirement for port
        bool terrainOk=(b==BLD_PORT)?(prov.terrain==TERRAIN_COAST):true;
        canBuild=canBuild&&prereqOk&&terrainOk;

        // Cost check
        bool canAfford=(res.gold>=bldGoldCost[b]&&res.wood>=bldWoodCost[b]
                        &&res.stone>=bldStoneCost[b]&&res.iron>=bldIronCost[b]);
        bool noOtherConstruction=(city.constructing<0);

        Color bgCol=built?Color{20,40,20,220}:constructing?Color{30,30,50,220}:Color{20,16,12,220};
        bool hover=ptInRect(mouse,{bx,by,bw,bh});
        if(hover&&!built) bgCol.r=clampU8(bgCol.r+15);
        DrawRectangleRec({bx,by,bw,bh},bgCol);
        DrawRectangleLinesEx({bx,by,bw,bh},1,built?C_GOLD:constructing?C_ALLY:Color{50,45,35,255});

        // Icon color patch
        Color ic=built?C_GOLD:constructing?C_ALLY:Color{80,70,50,255};
        DrawRectangle((int)(bx+4),(int)(by+4),18,18,ic);
        DrawText(bldNames[b],(int)(bx+26),(int)(by+6),11,built?C_GOLD:C_SECONDARY);

        if(built){
            // Show yield
            char yield[64]="";
            if(bldGoldYield[b]>0) snprintf(yield,63,"+%.0fg",bldGoldYield[b]);
            else if(bldFoodYield[b]>0) snprintf(yield,63,"+%.0ff",bldFoodYield[b]);
            else if(bldWoodYield[b]>0) snprintf(yield,63,"+%.0fw",bldWoodYield[b]);
            else if(bldStoneYield[b]>0) snprintf(yield,63,"+%.0fs",bldStoneYield[b]);
            else if(bldIronYield[b]>0) snprintf(yield,63,"+%.0fi",bldIronYield[b]);
            else strcpy(yield,"active");
            DrawText(yield,(int)(bx+6),(int)(by+28),12,{100,200,100,255});
        } else if(constructing){
            DrawText(TextFormat("%d turns",city.constructTurns),(int)(bx+6),(int)(by+28),12,{100,140,220,255});
        } else {
            // Cost
            char costStr[80]="";
            if(bldGoldCost[b]) snprintf(costStr+strlen(costStr),79-strlen(costStr)," %dG",bldGoldCost[b]);
            if(bldWoodCost[b]) snprintf(costStr+strlen(costStr),79-strlen(costStr)," %dW",bldWoodCost[b]);
            if(bldStoneCost[b]) snprintf(costStr+strlen(costStr),79-strlen(costStr)," %dS",bldStoneCost[b]);
            if(bldIronCost[b]) snprintf(costStr+strlen(costStr),79-strlen(costStr)," %dI",bldIronCost[b]);
            DrawText(costStr,(int)(bx+4),(int)(by+28),10,canAfford?Color{180,200,100,255}:Color{200,100,80,255});
            DrawText(TextFormat("%dt",bldTurns[b]),(int)(bx+4),(int)(by+42),11,C_SECONDARY);
        }

        // Build button
        if(canBuild&&noOtherConstruction){
            Rectangle buildBtn={bx+4,by+bh-26,bw-8,22};
            if(drawSmBtn(buildBtn,"BUILD",mouse,
                         canAfford?Color{20,50,20,255}:Color{30,20,20,255},
                         canAfford?Color{40,90,38,255}:Color{30,20,20,255})&&canAfford&&netCanEdit()){
                res.gold-=bldGoldCost[b];
                res.wood-=bldWoodCost[b];
                res.stone-=bldStoneCost[b];
                res.iron-=bldIronCost[b];
                city.constructing=b;
                city.constructTurns=bldTurns[b];
                netMarkDirty(); // Fase H
            }
        }
        if(!prereqOk&&!built&&hover){
            // Prereq tooltip
            int pi=prereqs[b];
            if(pi>=0&&pi<BLD_COUNT)
                DrawText(TextFormat("Requires: %s",bldNames[pi]),
                         (int)(bx+4),(int)(by+bh-26),10,{200,120,60,255});
        }
        if(!terrainOk&&!built){
            DrawText("Coastal only",(int)(bx+4),(int)(by+bh-26),10,{200,120,60,255});
        }
    }

    // Bottom bar
    DrawRectangle(0,SCREEN_H-44,SCREEN_W,44,{0,0,0,210});
    DrawLine(0,SCREEN_H-44,SCREEN_W,SCREEN_H-44,{80,65,30,160});
    DrawText(TextFormat("Gold: %.0f  Food: %.0f  Wood: %.0f  Stone: %.0f  Iron: %.0f",
             res.gold,res.food,res.wood,res.stone,res.iron),14,SCREEN_H-34,13,C_PARCHMENT);

    // Fase J: reservas → formar/unir ejército (requiere general en reserva)
    // PvP: el cliente usa su propia reserva (clientReserve) y facción local
    {
        auto& resv=(g_localFaction==FACTION_PLAYER)?g_campaign.reserve:g_campaign.clientReserve;
        int resNonHero=0,resHeroes=0;
        for(auto& u:resv){
            if(u.first==UNIT_GENERAL||u.first==UNIT_KING) resHeroes++;
            else resNonHero+=u.second;
        }
        int armyHere=armyIndexAt(g_campaign.viewedCity,g_localFaction);
        if(resHeroes>0){
            if(drawSmBtn({(float)(SCREEN_W-530),(float)(SCREEN_H-38),120,30},"FORM ARMY",mouse,
                         Color{20,50,20,255},Color{40,90,38,255})){
                if(netIsClient()){
                    // PvP/co-op: el cliente pide formar ejército al host
                    NetCmd fc; fc.cmd=NET_CMD_FORM_ARMY; fc.a=g_campaign.viewedCity; fc.b=fc.c=0;
                    netSessionSendCmd(fc);
                }else{
                    formArmyAt(g_campaign.viewedCity);
                    netMarkDirty();
                }
            }
        }
        if(armyHere>=0&&resNonHero>0){
            if(drawSmBtn({(float)(SCREEN_W-400),(float)(SCREEN_H-38),120,30},"JOIN ARMY",mouse,
                         Color{20,50,20,255},Color{40,90,38,255})){
                if(netIsClient()){
                    // PvP/co-op: el cliente pide unir reserva al host
                    NetCmd jc; jc.cmd=NET_CMD_JOIN_ARMY; jc.a=g_campaign.viewedCity; jc.b=jc.c=0;
                    netSessionSendCmd(jc);
                }else{
                    joinArmyAt(g_campaign.viewedCity);
                    netMarkDirty();
                }
            }
        }
    }
    if(drawSmBtn({(float)(SCREEN_W-260),(float)(SCREEN_H-38),120,30},"RECRUIT",mouse))
        return STATE_RECRUITMENT;
    if(drawSmBtn({(float)(SCREEN_W-130),(float)(SCREEN_H-38),116,30},"BACK",mouse,
                  {50,25,25,255},{80,40,40,255}))
        return STATE_CAMPAIGN_MAP;

    return STATE_CITY_MANAGEMENT;
}

// ═══════════════════════════════════════════════════════════════════════════
//  STATE: RECRUITMENT
// ═══════════════════════════════════════════════════════════════════════════
GameState updateDrawRecruitment(Vector2 mouse){
    if(g_campaign.viewedCity<0||g_campaign.viewedCity>=(int)g_campaign.provinces.size())
        return STATE_CAMPAIGN_MAP;
    Province& prov=g_campaign.provinces[g_campaign.viewedCity];
    City& city=prov.city;
    Resources& res=(g_campaign.pvpMode&&g_netSession.role==NET_ROLE_CLIENT)
                   ?g_campaign.clientRes:g_campaign.res;

    ClearBackground(C_BG);
    for(int gx=0;gx<SCREEN_W;gx+=40) DrawLine(gx,0,gx,SCREEN_H,{18,14,10,50});
    for(int gy=0;gy<SCREEN_H;gy+=40) DrawLine(0,gy,SCREEN_W,gy,{18,14,10,50});

    DrawRectangle(0,0,SCREEN_W,50,{0,0,0,220});
    DrawText(TextFormat("RECRUITMENT — %s",city.name),14,10,24,C_GOLD);
    DrawText(TextFormat("Turn: %d  |  Gold: %.0f  Food: %.0f  Iron: %.0f",
             g_campaign.turn,res.gold,res.food,res.iron),14,36,12,C_SECONDARY);

    // Bit requirements mapping
    // buildingReqs bits: 0=Barracks,1=Stable,2=Range(idx=7),3=Smithy(idx=4),4=Workshop(idx=8)
    auto hasReq=[&](int reqs)->bool{
        if(reqs&1) if(!city.built[BLD_BARRACKS]) return false;
        if(reqs&2) if(!city.built[BLD_STABLE]) return false;
        if(reqs&4) if(!city.built[BLD_RANGE]) return false;
        if(reqs&8) if(!city.built[BLD_SMITHY]) return false;
        if(reqs&16) if(!city.built[BLD_WORKSHOP]) return false;
        return true;
    };

    // Left: available units
    float leftW=SCREEN_W*0.55f;
    DrawText("Available for recruitment:",(int)14,(int)60,13,C_PARCHMENT);

    for(int t=0;t<unitTypeCount();t++){
        const UnitTypeDef& td=g_unitTypes[t];
        if(td.recruitGold<0) continue; // Fase J: héroes (General/Rey) no se reclutan
        float ry=80.f+t*38.f;
        if(ry+38>SCREEN_H-60) break;
        bool unlocked=hasReq(td.buildingReqs);

        Color rowbg=(t%2==0)?Color{18,14,10,200}:Color{14,10,8,200};
        DrawRectangle(0,(int)ry,(int)leftW,36,rowbg);
        Color col2={td.r,td.g,td.b,(unsigned char)(unlocked?220:80)};
        DrawRectangle(6,(int)(ry+8),18,18,col2);
        DrawText(td.name,(int)28,(int)(ry+10),13,unlocked?C_PARCHMENT:C_SECONDARY);

        if(unlocked){
            DrawText(TextFormat("G:%d F:%d I:%d",td.recruitGold,td.recruitFood,td.recruitIron),
                     (int)(leftW-280),(int)(ry+8),11,C_SECONDARY);
            DrawText(TextFormat("%d turns",td.recruitTurns),(int)(leftW-150),(int)(ry+8),11,C_SECONDARY);
            DrawText(TextFormat("%d soldiers",td.soldierCount),(int)(leftW-150),(int)(ry+22),11,C_SECONDARY);
            bool canAfford=(res.gold>=td.recruitGold&&res.food>=td.recruitFood&&res.iron>=td.recruitIron);
            Rectangle addBtn={leftW-80,ry+6,72,26};
            if(drawSmBtn(addBtn,"RECRUIT",mouse,
                         canAfford?Color{20,50,20,255}:Color{30,20,20,255},
                         canAfford?Color{40,90,38,255}:Color{30,20,20,255})&&canAfford){
                if(netIsClient()){
                    // Fase H: el cliente pide reclutar; el host valida y descuenta
                    NetCmd rc; rc.cmd=NET_CMD_RECRUIT;
                    rc.a=g_campaign.viewedCity; rc.b=t; rc.c=0;
                    netSessionSendCmd(rc);
                }else{
                    res.gold-=td.recruitGold; res.food-=td.recruitFood; res.iron-=td.recruitIron;
                    g_campaign.recruitQueue.push_back({t,td.recruitTurns,td.recruitTurns,(int)g_localFaction});
                    netMarkDirty();
                }
            }
        } else {
            // Show requirements
            std::string req="Requires: ";
            if(td.buildingReqs&1) req+=std::string(bldNames[BLD_BARRACKS])+", ";
            if(td.buildingReqs&2) req+=std::string(bldNames[BLD_STABLE])+", ";
            if(td.buildingReqs&4) req+=std::string(bldNames[BLD_RANGE])+", ";
            if(td.buildingReqs&8) req+=std::string(bldNames[BLD_SMITHY])+", ";
            if(td.buildingReqs&16) req+=std::string(bldNames[BLD_WORKSHOP])+", ";
            if(req.size()>2&&req.back()==' ') req=req.substr(0,req.size()-2);
            DrawText(req.c_str(),(int)(leftW-350),(int)(ry+10),10,{140,100,60,255});
        }
    }

    // Right: queue + ready
    float rx=leftW+10;
    DrawLine((int)rx,50,(int)rx,SCREEN_H-50,{60,50,35,120});
    float rightW=SCREEN_W-rx-10;
    DrawText("Recruitment queue:",(int)(rx+10),(int)60,13,C_PARCHMENT);
    // PvP: el cliente solo ve SU cola (la del host no le concierne)
    int qRow=0;
    for(int i=0;i<(int)g_campaign.recruitQueue.size();i++){
        auto& e=g_campaign.recruitQueue[i];
        if(e.typeIdx>=unitTypeCount()) continue;
        if(g_campaign.pvpMode&&e.faction!=(int)g_localFaction) continue;
        float ry=80.f+qRow*30.f; qRow++;
        if(ry+30>SCREEN_H/2) break;
        DrawText(g_unitTypes[e.typeIdx].name,(int)(rx+10),(int)(ry+6),13,C_ALLY);
        DrawText(TextFormat("%d turn(s)",e.turnsLeft),(int)(rx+rightW-80),(int)(ry+6),12,C_SECONDARY);
        // 4.3: Progress bar
        int orig=e.originalTurns>0?e.originalTurns:1;
        float progress=1.f-(float)e.turnsLeft/(float)orig;
        float pbW=rightW-100.f;
        DrawRectangle((int)(rx+10),(int)(ry+20),(int)pbW,5,DARKGRAY);
        DrawRectangle((int)(rx+10),(int)(ry+20),(int)(pbW*progress),5,{80,160,80,255});
    }
    if(g_campaign.recruitQueue.empty())
        DrawText("(empty)",(int)(rx+10),(int)100,12,C_SECONDARY);

    DrawLine((int)rx,SCREEN_H/2,(int)SCREEN_W,SCREEN_H/2,{60,50,35,80});
    DrawText("Reserve (unassigned):",(int)(rx+10),(int)(SCREEN_H/2+8),13,C_PARCHMENT);
    // PvP: el cliente ve su propia reserva (clientReserve)
    auto& resvUI=(g_localFaction==FACTION_PLAYER)?g_campaign.reserve:g_campaign.clientReserve;
    for(int i=0;i<(int)resvUI.size();i++){
        auto [ti,cnt]=resvUI[i];
        if(ti>=unitTypeCount()) continue;
        const UnitTypeDef& td2=g_unitTypes[ti];
        float ry=SCREEN_H/2+30.f+i*28.f;
        if(ry+28>SCREEN_H-50) break;
        DrawRectangle(6+(int)rx,(int)(ry+4),14,14,{td2.r,td2.g,td2.b,255});
        DrawText(td2.name,(int)(rx+26),(int)(ry+6),13,C_PARCHMENT);
        DrawText(TextFormat("x%d",cnt),(int)(rx+rightW-50),(int)(ry+6),12,C_SECONDARY);
    }

    // Bottom
    DrawRectangle(0,SCREEN_H-50,SCREEN_W,50,{0,0,0,210});
    if(drawSmBtn({(float)(SCREEN_W-130),(float)(SCREEN_H-42),116,32},"BACK",mouse,
                  {50,25,25,255},{80,40,40,255}))
        return STATE_CITY_MANAGEMENT;
    return STATE_RECRUITMENT;
}

// ═══════════════════════════════════════════════════════════════════════════
//  STATE: UNIT CODEX
// ═══════════════════════════════════════════════════════════════════════════
int g_codexIdx=0;
float g_codexAngle=0;
GameState updateDrawUnitCodex(Vector2 mouse,float dt){
    g_codexAngle+=45.f*dt;

    ClearBackground({8,8,16,255});
    for(int gx=0;gx<SCREEN_W;gx+=40) DrawLine(gx,0,gx,SCREEN_H,{16,16,32,60});
    for(int gy=0;gy<SCREEN_H;gy+=40) DrawLine(0,gy,SCREEN_W,gy,{16,16,32,60});

    DrawRectangle(0,0,SCREEN_W,50,{0,0,0,220});
    DrawText("UNIT CODEX",14,10,28,C_GOLD);
    DrawText("Encyclopedia of all military units",14,38,12,C_SECONDARY);

    float listW=260, listX=10, contentX=listW+20;
    int nc=unitTypeCount();

    // Left list
    DrawRectangle((int)listX,52,(int)listW,SCREEN_H-52-44,{12,10,8,210});
    DrawRectangleLinesEx({listX,52,listW,(float)(SCREEN_H-52-44)},1,{60,50,35,200});

    for(int i=0;i<nc;i++){
        const UnitTypeDef& td=g_unitTypes[i];
        bool sel=(i==g_codexIdx);
        float ry=60.f+i*38.f;
        if(ry+38>SCREEN_H-44) break;
        Color rowbg=sel?Color{25,50,70,220}:(i%2==0)?Color{16,13,10,200}:Color{12,10,8,200};
        bool hv=ptInRect(mouse,{listX,ry,listW,36});
        if(hv&&!sel) rowbg={22,20,16,200};
        DrawRectangleRec({listX,ry,listW,36},rowbg);
        if(sel) DrawRectangleLinesEx({listX,ry,listW,36},1,C_GOLD);
        DrawRectangle((int)(listX+4),(int)(ry+8),16,16,{td.r,td.g,td.b,255});
        DrawText(td.name,(int)(listX+26),(int)(ry+10),13,sel?C_GOLD:C_PARCHMENT);
        if(hv&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) g_codexIdx=i;
    }

    // Right content
    if(g_codexIdx>=nc) return STATE_UNIT_CODEX; // 1.6: bounds check
    if(g_codexIdx<nc){
        const UnitTypeDef& td=g_unitTypes[g_codexIdx];
        float cx=contentX, cy=58.f;

        DrawText(td.name,(int)cx,(int)cy,22,C_GOLD); cy+=30;
        DrawLine((int)cx,(int)cy,(int)(SCREEN_W-10),(int)cy,{80,65,30,120}); cy+=10;

        // Sprite (pies anclados: se baja para centrar el cuerpo en el círculo)
        Vector2 sprCenter={(float)(SCREEN_W-100),(float)140};
        sprCenter.y+=unitHeadOffset(g_codexIdx,2.5f)*0.5f;
        drawUnit(g_codexIdx,0,sprCenter,g_codexAngle,2.5f,true,false,
                 UA_IDLE,g_menuTime);
        DrawCircleLines((int)(SCREEN_W-100),140,55,{80,70,50,80});

        // Lore — word wrapped (3.8)
        drawWrappedText(td.lore,(int)cx,(int)cy,(int)(SCREEN_W-contentX-120),12,C_SECONDARY);
        cy+=30;

        // Stats table
        DrawText(TextFormat("Soldiers: %d",td.soldierCount),(int)cx,(int)cy,13,C_PARCHMENT); cy+=18;
        DrawText(TextFormat("HP per soldier: %.0f",td.hpPerSoldier),(int)cx,(int)cy,13,C_PARCHMENT); cy+=18;
        DrawText(TextFormat("Armor: %d / Speed: %d",td.armor,td.speed),(int)cx,(int)cy,13,C_PARCHMENT); cy+=18;
        DrawText(TextFormat("Melee: Atk%d Def%d Dmg%.0f+%.0f (%.1fs)",
                 td.meleeAttack,td.meleeDefense,td.meleeBaseDmg,td.meleeAPDmg,td.meleeInterval),
                 (int)cx,(int)cy,13,{220,180,60,255}); cy+=18;
        if(td.range>0){
            DrawText(TextFormat("Range: %dpx  Dmg%.0f+%.0f (%.1fs reload)",
                     td.range,td.missileBaseDmg,td.missileAPDmg,td.missileReload),
                     (int)cx,(int)cy,13,{180,100,200,255}); cy+=18;
        }
        DrawText(TextFormat("Morale: %.0f",td.morale),(int)cx,(int)cy,13,{200,200,80,255}); cy+=18;
        cy+=8;

        // Recruitment cost
        DrawText("Recruitment:",(int)cx,(int)cy,13,C_GOLD); cy+=16;
        DrawText(TextFormat("Gold: %d  Food: %d  Iron: %d  Turns: %d",
                 td.recruitGold,td.recruitFood,td.recruitIron,td.recruitTurns),
                 (int)cx,(int)cy,12,C_SECONDARY); cy+=16;
        DrawText(TextFormat("Maintenance: %d gold/turn, %d food/turn",td.maintGold,td.maintFood),
                 (int)cx,(int)cy,12,C_SECONDARY); cy+=20;

        // Stat bars
        DrawText("Stats (normalized):",(int)cx,(int)cy,12,{100,140,200,255}); cy+=14;
        drawStatBars(cx,cy,(float)(SCREEN_W-contentX-20),140.f,td);
    }

    // Bottom
    DrawRectangle(0,SCREEN_H-44,SCREEN_W,44,{0,0,0,210});
    if(drawSmBtn({(float)(SCREEN_W-130),(float)(SCREEN_H-38),116,30},"BACK",mouse,
                  {50,25,25,255},{80,40,40,255})) return STATE_CAMPAIGN_MAP;

    return STATE_UNIT_CODEX;
}

// ═══════════════════════════════════════════════════════════════════════════
//  STATE: SETTINGS
// ═══════════════════════════════════════════════════════════════════════════
GameState updateDrawSettings(Vector2 mouse){
    struct ResOpt { int w; int h; const char* label; };
    static const ResOpt RES_OPTS[] = {
        // 16:9
        {1280, 720,  "1280x720 (16:9)"},
        {1600, 900,  "1600x900 (16:9)"},
        {1920, 1080, "1920x1080 (16:9)"},
        {2560, 1440, "2560x1440 (16:9)"},
        {3840, 2160, "3840x2160 (16:9)"},
        // 21:9 (ultrawide)
        {2560, 1080, "2560x1080 (21:9)"},
        {3440, 1440, "3440x1440 (21:9)"},
        {3840, 1600, "3840x1600 (21:9)"},
        {5120, 2160, "5120x2160 (21:9)"},
    };
    static const int RES_COUNT = (int)(sizeof(RES_OPTS)/sizeof(RES_OPTS[0]));
    auto findResIdx = [&](int w,int h)->int{
        for(int i=0;i<RES_COUNT;i++) if(RES_OPTS[i].w==w && RES_OPTS[i].h==h) return i;
        return 0;
    };

    // Local UI copy: only applied when SAVE is pressed
    static bool s_uiActive=false;
    static GameSettings s_uiSettings;
    static int s_resIdx=0;
    if(!s_uiActive){
        s_uiSettings = g_settings;
        s_resIdx = findResIdx(s_uiSettings.screenW, s_uiSettings.screenH);
        s_uiSettings.screenW = RES_OPTS[s_resIdx].w;
        s_uiSettings.screenH = RES_OPTS[s_resIdx].h;
        s_uiActive=true;
    }
    // Live-preview UI scale while editing settings
    g_uiScaleDraw = effectiveUiScale(STATE_SETTINGS, s_uiSettings.uiScale);

    ClearBackground(C_BG);
    DrawRectangleGradientV(0,0,SCREEN_W,SCREEN_H,{10,8,6,255},C_BG);
    int titleFs=32;
    float titleY=uiPx(30.f);
    int tw2=MeasureText("SETTINGS",titleFs);
    DrawText("SETTINGS",SCREEN_W/2-tw2/2,(int)titleY,titleFs,C_GOLD);
    float lineY=titleY+(float)uiFS(titleFs)+uiPx(10.f);
    float lineW=uiPx(200.f);
    DrawLine(SCREEN_W/2-(int)lineW,(int)lineY,SCREEN_W/2+(int)lineW,(int)lineY,C_GOLD);

    // Scale-aware layout (so high UI scale doesn't overlap)
    float cw=fminf(uiPx(500.f),(float)SCREEN_W-uiPx(60.f));
    if(cw<uiPx(320.f)) cw=uiPx(320.f);
    float cx=(float)SCREEN_W*0.5f - cw*0.5f;
    float cy=lineY+uiPx(24.f);
    float lineGap=uiPx(36.f);
    float labelGap=uiPx(14.f);

    // Resolution
    DrawText("Resolution",(int)cx,(int)(cy-labelGap),12,C_SECONDARY);
    float resH=uiPx(34.f);
    Rectangle resR={cx,cy,cw,resH};
    bool resHv=ptInRect(mouse,resR);
    DrawRectangleRec(resR,resHv?Color{18,28,18,220}:Color{12,18,12,220});
    DrawRectangleLinesEx(resR,1,Color{60,90,40,255});
    float arrowW=uiPx(44.f);
    if(drawSmBtn({cx,cy,arrowW,resH},"<",mouse,{25,35,25,255},{45,65,45,255})){
        s_resIdx = (s_resIdx-1+RES_COUNT)%RES_COUNT;
        s_uiSettings.screenW = RES_OPTS[s_resIdx].w;
        s_uiSettings.screenH = RES_OPTS[s_resIdx].h;
    }
    if(drawSmBtn({cx+cw-arrowW,cy,arrowW,resH},">",mouse,{25,35,25,255},{45,65,45,255})){
        s_resIdx = (s_resIdx+1)%RES_COUNT;
        s_uiSettings.screenW = RES_OPTS[s_resIdx].w;
        s_uiSettings.screenH = RES_OPTS[s_resIdx].h;
    }
    const char* rl=RES_OPTS[s_resIdx].label;
    int rtw=MeasureText(rl,14);
    DrawText(rl,(int)(cx+cw/2-rtw/2),(int)(cy+resH*0.5f-uiPx(7.f)),14,C_PARCHMENT);
    cy+=uiPx(50.f);

    // Fullscreen checkbox
    {
        float cbS=uiPx(20.f);
        Rectangle fsr={(float)cx,cy,cbS,cbS};
        bool fshv=ptInRect(mouse,fsr);
        DrawRectangleRec(fsr,s_uiSettings.fullscreen?Color{40,80,40,255}:fshv?Color{25,45,25,255}:Color{15,25,15,220});
        DrawRectangleLinesEx(fsr,1,s_uiSettings.fullscreen?C_GOLD:Color{60,90,40,255});
        if(s_uiSettings.fullscreen) DrawText("X",(int)(cx+uiPx(5.f)),(int)(cy+uiPx(2.f)),14,C_GOLD);
        DrawText("Fullscreen",(int)(cx+uiPx(28.f)),(int)(cy+uiPx(2.f)),14,C_SECONDARY);
        if((fshv||ptInRect(mouse,{(float)cx,(float)cy,cw,cbS}))&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            s_uiSettings.fullscreen=!s_uiSettings.fullscreen;
        cy+=lineGap;
    }

    // Master Volume
    DrawText("Master Volume",(int)cx,(int)(cy-labelGap),12,C_SECONDARY);
    s_uiSettings.masterVolume=drawFloatSlider({cx,cy,cw,uiPx(14.f)},s_uiSettings.masterVolume,0.f,1.f,"","%.2f",mouse,{80,160,80,200});
    previewAudioVolumes(s_uiSettings.masterVolume,s_uiSettings.musicVolume);
    cy+=lineGap;

    // Music Volume
    DrawText("Music Volume",(int)cx,(int)(cy-labelGap),12,C_SECONDARY);
    s_uiSettings.musicVolume=drawFloatSlider({cx,cy,cw,uiPx(14.f)},s_uiSettings.musicVolume,0.f,1.f,"","%.2f",mouse,{80,120,180,200});
    previewAudioVolumes(s_uiSettings.masterVolume,s_uiSettings.musicVolume);
    cy+=lineGap;

    // UI Scale
    DrawText("UI Scale",(int)cx,(int)(cy-labelGap),12,C_SECONDARY);
    float uiPct=s_uiSettings.uiScale*100.f;
    uiPct=drawFloatSlider({cx,cy,cw,uiPx(14.f)},uiPct,100.f,225.f,"","%.0f%%",mouse,{200,165,80,200});
    s_uiSettings.uiScale=uiPct/100.f;
    if(s_uiSettings.uiScale<1.f) s_uiSettings.uiScale=1.f;
    if(s_uiSettings.uiScale>2.25f) s_uiSettings.uiScale=2.25f;
    cy+=lineGap;

    // Formation spacing (realismo: separacion entre soldados en batalla)
    DrawText("Formation Spacing",(int)cx,(int)(cy-labelGap),12,C_SECONDARY);
    s_uiSettings.formationSpacing=drawFloatSlider({cx,cy,cw,uiPx(14.f)},s_uiSettings.formationSpacing,16.f,40.f,"","%.0f px",mouse,{150,120,60,200});
    cy+=lineGap;

    // Soldiers per row (ancho de la linea de formacion)
    DrawText("Soldiers per Row",(int)cx,(int)(cy-labelGap),12,C_SECONDARY);
    s_uiSettings.formationPerRow=drawIntSlider({cx,cy,cw,uiPx(14.f)},s_uiSettings.formationPerRow,3,10,"",mouse,{120,100,160,200});
    cy+=lineGap;

    // Difficulty
    DrawText("Difficulty:",(int)cx,(int)cy,14,C_SECONDARY); cy+=uiPx(20.f);
    static const char* diffNames[]={"EASY","NORMAL","HARD"};
    for(int d=0;d<3;d++){
        float bx=cx+d*uiPx(120.f);
        bool sel=(s_uiSettings.difficulty==d);
        Color bc=sel?Color{40,80,40,255}:Color{20,30,20,255};
        Color bh=sel?Color{60,120,60,255}:Color{35,55,35,255};
        float bw=uiPx(110.f), bhh=uiPx(30.f);
        if(drawSmBtn({bx,cy,bw,bhh},diffNames[d],mouse,bc,bh)) s_uiSettings.difficulty=d;
        if(sel) DrawRectangleLinesEx({bx,cy,bw,bhh},2,C_GOLD);
    }
    cy+=uiPx(46.f);

    // Show FPS checkbox
    float cbS=uiPx(20.f);
    Rectangle cbr={(float)cx,cy,cbS,cbS};
    bool cbhv=ptInRect(mouse,cbr);
    DrawRectangleRec(cbr,s_uiSettings.showFPS?Color{40,80,40,255}:cbhv?Color{25,45,25,255}:Color{15,25,15,220});
    DrawRectangleLinesEx(cbr,1,s_uiSettings.showFPS?C_GOLD:Color{60,90,40,255});
    if(s_uiSettings.showFPS) DrawText("X",(int)(cx+uiPx(5.f)),(int)(cy+uiPx(2.f)),14,C_GOLD);
    DrawText("Show FPS Counter",(int)(cx+uiPx(28.f)),(int)(cy+uiPx(2.f)),14,C_SECONDARY);
    if((cbhv||ptInRect(mouse,{(float)cx,(float)cy,cw,cbS}))&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        s_uiSettings.showFPS=!s_uiSettings.showFPS;
    cy+=lineGap;

    // F11 hint
    DrawText("F11 / Alt+Enter: Toggle Fullscreen",(int)cx,(int)cy,13,C_PARCHMENT);
    cy+=uiPx(28.f);

    // Save button
    float btnY=cy+uiPx(10.f);
    float btnW=fminf(uiPx(160.f),(cw-uiPx(12.f))*0.5f);
    float btnH=uiPx(44.f);
    float btnGap=uiPx(12.f);
    float bx1=(float)SCREEN_W*0.5f - (btnW*2.f+btnGap)*0.5f;
    if(drawButton({bx1,btnY,btnW,btnH},"SAVE",mouse,{30,60,30,255},{50,100,50,255})){
        // Apply & persist
        g_settings = s_uiSettings;
        saveSettings();
        playSfx(SFX_UI_CONFIRM,0.9f);

        // Apply fullscreen + windowed resolution immediately
        bool isFs=IsWindowFullscreen();
        if(isFs && !g_settings.fullscreen){
            ToggleFullscreen();
            isFs=false;
        }
        if(!isFs){
            SetWindowSize(g_settings.screenW, g_settings.screenH);
        }
        if(!isFs && g_settings.fullscreen){
            ToggleFullscreen();
        }

        s_uiActive=false;
        return STATE_MAIN_MENU;
    }
    if(drawButton({bx1+btnW+btnGap,btnY,btnW,btnH},"BACK",mouse,{50,25,25,255},{90,40,40,255})){
        // Discard pending changes
        s_uiActive=false;
        g_uiScaleDraw = effectiveUiScale(g_state, g_settings.uiScale);
        return STATE_MAIN_MENU;
    }
    return STATE_SETTINGS;
}

// ═══════════════════════════════════════════════════════════════════════════
//  STATE: UNIT EDITOR (sandbox, from main menu)
// ═══════════════════════════════════════════════════════════════════════════
void loadEditorEditCopy(){
    if(g_editTypeIdx<0||g_editTypeIdx>=unitTypeCount()) return;
    g_editorEditCopy=g_unitTypes[g_editTypeIdx];
    g_editorEditCopyValid=true;
    g_editorFocusField=-1;
}

int getVanillaIndexByName(const char* name){
    for(int i=0;i<(int)g_vanillaUnitTypes.size();i++)
        if(strcmp(g_vanillaUnitTypes[i].name,name)==0) return i;
    return -1;
}

// Field indices: 0=name, 1=soldiers, 2=hpPerSoldier, 3=armor, 4=speed, 5=meleeAtk, 6=meleeDef, 7=meleeBaseDmg, 8=meleeAPDmg, 9=meleeInterval, 10=range, 11=missileBase, 12=missileAP, 13=missileReload, 14=R, 15=G, 16=B
void getEditorFieldDisplay(int fieldIdx, char* out, int outLen){
    const UnitTypeDef& t=g_editorEditCopy;
    switch(fieldIdx){
        case 0: snprintf(out,outLen,"%s",t.name); break;
        case 1: snprintf(out,outLen,"%d",t.soldierCount); break;
        case 2: snprintf(out,outLen,"%.1f",t.hpPerSoldier); break;
        case 3: snprintf(out,outLen,"%d",t.armor); break;
        case 4: snprintf(out,outLen,"%d",t.speed); break;
        case 5: snprintf(out,outLen,"%d",t.meleeAttack); break;
        case 6: snprintf(out,outLen,"%d",t.meleeDefense); break;
        case 7: snprintf(out,outLen,"%.1f",t.meleeBaseDmg); break;
        case 8: snprintf(out,outLen,"%.1f",t.meleeAPDmg); break;
        case 9: snprintf(out,outLen,"%.1f",t.meleeInterval); break;
        case 10: snprintf(out,outLen,"%d",t.range); break;
        case 11: snprintf(out,outLen,"%.1f",t.missileBaseDmg); break;
        case 12: snprintf(out,outLen,"%.1f",t.missileAPDmg); break;
        case 13: snprintf(out,outLen,"%.1f",t.missileReload); break;
        case 14: snprintf(out,outLen,"%d",(int)t.r); break;
        case 15: snprintf(out,outLen,"%d",(int)t.g); break;
        case 16: snprintf(out,outLen,"%d",(int)t.b); break;
        default: out[0]='\0'; break;
    }
}

void commitEditorField(int fieldIdx){
    UnitTypeDef& t=g_editorEditCopy;
    const char* s=g_editorFocusBuf.c_str();
    int vi; float vf;
    switch(fieldIdx){
        case 0: strncpy(t.name,s,sizeof(t.name)-1); t.name[sizeof(t.name)-1]='\0'; break;
        case 1: vi=atoi(s); t.soldierCount=fmax(1,fmin(120,vi)); break;
        case 2: vf=(float)atof(s); t.hpPerSoldier=fmaxf(1.f,fminf(100.f,vf)); break;
        case 3: vi=atoi(s); t.armor=fmax(0,fmin(40,vi)); break;
        case 4: vi=atoi(s); t.speed=fmax(30,fmin(200,vi)); break;
        case 5: vi=atoi(s); t.meleeAttack=fmax(1,fmin(80,vi)); break;
        case 6: vi=atoi(s); t.meleeDefense=fmax(0,fmin(60,vi)); break;
        case 7: vf=(float)atof(s); t.meleeBaseDmg=fmaxf(1.f,fminf(80.f,vf)); break;
        case 8: vf=(float)atof(s); t.meleeAPDmg=fmaxf(0.f,fminf(60.f,vf)); break;
        case 9: vf=(float)atof(s); t.meleeInterval=fmaxf(0.5f,fminf(4.f,vf)); break;
        case 10: vi=atoi(s); t.range=fmax(0,fmin(400,vi)); break;
        case 11: vf=(float)atof(s); t.missileBaseDmg=fmaxf(0.f,fminf(60.f,vf)); break;
        case 12: vf=(float)atof(s); t.missileAPDmg=fmaxf(0.f,fminf(40.f,vf)); break;
        case 13: vf=(float)atof(s); t.missileReload=fmaxf(0.5f,fminf(8.f,vf)); break;
        case 14: vi=atoi(s); t.r=(unsigned char)fmax(0,fmin(255,vi)); break;
        case 15: vi=atoi(s); t.g=(unsigned char)fmax(0,fmin(255,vi)); break;
        case 16: vi=atoi(s); t.b=(unsigned char)fmax(0,fmin(255,vi)); break;
        default: break;
    }
}

// Draw label at xLabel,y and textbox at xBox,y; handle focus and key input.
void drawEditorTextBox(float xLabel, float xBox, float y, float boxW, float h, int fieldIdx, const char* label, Vector2 mouse){
    char disp[64];
    getEditorFieldDisplay(fieldIdx,disp,sizeof(disp));
    Rectangle r={xBox,y,boxW,h};
    bool hover=ptInRect(mouse,r);
    bool focused=(g_editorFocusField==fieldIdx);
    if(IsMouseButtonPressed(MOUSE_LEFT_BUTTON)){
        if(hover){
            if(!focused){ g_editorFocusField=fieldIdx; g_editorFocusBuf=disp; }
        } else if(focused){ commitEditorField(fieldIdx); g_editorFocusField=-1; }
    }
    if(focused){
        int key=GetCharPressed();
        while(key>0){
            if((key>='0'&&key<='9')||key=='.'||key=='-'||(fieldIdx==0&&key>=' ')){ if((int)g_editorFocusBuf.size()<31) g_editorFocusBuf+=(char)key; }
            key=GetCharPressed();
        }
        if(IsKeyPressed(KEY_BACKSPACE)&&!g_editorFocusBuf.empty()) g_editorFocusBuf.pop_back();
        if(IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_KP_ENTER)){ commitEditorField(fieldIdx); g_editorFocusField=-1; }
    }
    if(label&&label[0]) DrawText(label,(int)xLabel,(int)(y+uiPx(2.f)),11,C_SECONDARY);
    DrawRectangle((int)r.x,(int)r.y,(int)r.width,(int)r.height,{18,18,18,255});
    DrawRectangleLinesEx(r,1,focused?C_GOLD:Color{55,55,55,255});
    const char* show=focused?g_editorFocusBuf.c_str():disp;
    if(show[0]) DrawText(show,(int)(r.x+uiPx(4.f)),(int)(r.y+uiPx(2.f)),12,WHITE);
}

GameState updateDrawUnitEditor(Vector2 mouse,float dt){
    if(g_editTypeIdx>=unitTypeCount()) g_editTypeIdx=0;
    g_editorPreviewAngle+=40.f*dt;

    // Scale-aware layout (title + subtitle in top bar, no overlap)
    float titleY=uiPx(10.f);
    float subY=titleY+(float)uiFS(24)+uiPx(8.f);
    float topBarH=subY+(float)uiFS(12)+uiPx(10.f);
    float panelY=topBarH+uiPx(6.f);
    float panelH=(float)SCREEN_H-panelY-uiPx(6.f);
    if(panelH<uiPx(100.f)) panelH=uiPx(100.f);
    float scrollBarW=uiPx(14.f);
    float leftW=fminf(uiPx(320.f),(float)(SCREEN_W-20)*0.32f);
    if(leftW<uiPx(200.f)) leftW=uiPx(200.f);
    float contentLeftW=leftW-scrollBarW;
    float gap=uiPx(14.f);
    float rightX=leftW+gap;
    float rightW=(float)SCREEN_W-rightX-uiPx(8.f);
    float listRowH=uiPx(28.f);
    float listPad=uiPx(8.f);

    ClearBackground({8,10,18,255});
    for(int gx=0;gx<SCREEN_W;gx+=40) DrawLine(gx,0,gx,SCREEN_H,{18,20,40,70});
    for(int gy=0;gy<SCREEN_H;gy+=40) DrawLine(0,gy,SCREEN_W,gy,{18,20,40,70});

    DrawRectangle(0,0,SCREEN_W,(int)topBarH,{0,0,0,220});
    DrawText("UNIT EDITOR - SANDBOX",(int)uiPx(14.f),(int)titleY,24,C_GOLD);
    DrawText("Modify unit stats (changes persist until restart)",(int)uiPx(14.f),(int)subY,12,C_SECONDARY);
    float btnW=uiPx(116.f), btnH=uiPx(30.f);
    float backBtnY=titleY+(topBarH-titleY-btnH)*0.5f;
    if(drawSmBtn({(float)(SCREEN_W-btnW-uiPx(14.f)),backBtnY,btnW,btnH},"BACK",mouse,
                  {50,25,25,255},{80,40,40,255})){
        return STATE_MAIN_MENU;
    }

    DrawRectangle((int)uiPx(8.f),(int)panelY,(int)leftW,(int)panelH,{10,16,12,210});
    DrawRectangleLinesEx({uiPx(8.f),panelY,leftW,panelH},1,{50,80,50,200});
    DrawRectangle((int)rightX,(int)panelY,(int)rightW,(int)panelH,{10,12,28,210});
    DrawRectangleLinesEx({rightX,panelY,rightW,panelH},1,{50,50,110,200});

    static int s_editorLastTypeIdx = -1;
    if(g_editTypeIdx>=0&&g_editTypeIdx<unitTypeCount()){
        if(g_editTypeIdx!=s_editorLastTypeIdx||!g_editorEditCopyValid){ loadEditorEditCopy(); s_editorLastTypeIdx=g_editTypeIdx; }
    } else s_editorLastTypeIdx=-1;

    // ─── New unit name dialog (modal): wide input, cursor
    if(g_editorNewUnitDialogOpen){
        float dw=uiPx(900.f); if(dw>(float)SCREEN_W-uiPx(40.f)) dw=(float)SCREEN_W-uiPx(40.f);
        float dh=uiPx(140.f), dx=(SCREEN_W-dw)*0.5f, dy=(SCREEN_H-dh)*0.5f;
        DrawRectangle(0,0,SCREEN_W,SCREEN_H,{0,0,0,180});
        DrawRectangle((int)dx,(int)dy,(int)dw,(int)dh,{20,28,24,255});
        DrawRectangleLinesEx({dx,dy,dw,dh},2,C_GOLD);
        DrawText("New unit type name:",(int)(dx+uiPx(12.f)),(int)(dy+uiPx(12.f)),14,C_SECONDARY);
        float tbX=dx+uiPx(12.f), tbY=dy+uiPx(38.f), tbW=dw-uiPx(24.f), tbH=uiPx(28.f);
        Rectangle tbR={tbX,tbY,tbW,tbH};
        int key=GetCharPressed();
        while(key>0){ if((int)g_editorNewUnitNameBuf.size()<90&&key>=32) g_editorNewUnitNameBuf+=(char)key; key=GetCharPressed(); }
        if(IsKeyPressed(KEY_BACKSPACE)&&!g_editorNewUnitNameBuf.empty()) g_editorNewUnitNameBuf.pop_back();
        DrawRectangle((int)tbR.x,(int)tbR.y,(int)tbR.width,(int)tbR.height,{18,18,18,255});
        DrawRectangleLinesEx(tbR,1,C_GOLD);
        if(!g_editorNewUnitNameBuf.empty()) DrawText(g_editorNewUnitNameBuf.c_str(),(int)(tbX+uiPx(4.f)),(int)(tbY+uiPx(4.f)),12,WHITE);
        float cursorX=tbX+uiPx(4.f)+(float)MeasureText(g_editorNewUnitNameBuf.c_str(),uiFS(12));
        if((int)(cursorX+uiPx(2.f))<(int)(tbR.x+tbR.width)){ bool blink=((int)(GetTime()*2)&1)==0; if(blink) DrawRectangle((int)cursorX,(int)(tbY+uiPx(4.f)),2,uiFS(12),WHITE); }
        float okX=dx+dw-uiPx(24.f)-uiPx(100.f), okY=dy+dh-uiPx(40.f), cnX=dx+dw-uiPx(24.f)-uiPx(200.f);
        if(drawSmBtn({okX,okY,uiPx(90.f),uiPx(28.f)},"OK",mouse,{20,50,20,255},{40,90,40,255})){
            while(!g_editorNewUnitNameBuf.empty()&&g_editorNewUnitNameBuf.back()==' ') g_editorNewUnitNameBuf.pop_back();
            if(!g_editorNewUnitNameBuf.empty()){
                UnitTypeDef nu=(unitTypeCount()>0?g_unitTypes[0]:g_editorEditCopy);
                strncpy(nu.name,g_editorNewUnitNameBuf.c_str(),sizeof(nu.name)-1); nu.name[sizeof(nu.name)-1]='\0';
                nu.isBuiltin=false;
                g_unitTypes.push_back(nu);
                g_editTypeIdx=(int)g_unitTypes.size()-1;
                rebuildTexture(g_editTypeIdx);
                loadEditorEditCopy();
            }
            g_editorNewUnitDialogOpen=false; g_editorNewUnitNameBuf.clear();
        }
        if(drawSmBtn({cnX,okY,uiPx(90.f),uiPx(28.f)},"Cancel",mouse,{50,25,25,255},{80,40,40,255})){
            g_editorNewUnitDialogOpen=false; g_editorNewUnitNameBuf.clear();
        }
        return STATE_UNIT_EDITOR;
    }

    // ─── Left panel: list of unit types + Create new
    float listContentH=listPad+(float)unitTypeCount()*listRowH+uiPx(12.f)+uiPx(36.f)+listPad;
    float listScrollMax=fmaxf(0.f,listContentH-panelH);
    float listWheel=GetMouseWheelMove();
    if(listWheel!=0.f&&mouse.x>=uiPx(8.f)&&mouse.x<uiPx(8.f)+leftW&&mouse.y>=panelY&&mouse.y<panelY+panelH)
        g_editorListScrollY-=listWheel*uiPx(32.f);
    g_editorListScrollY=fmaxf(0.f,fminf(listScrollMax,g_editorListScrollY));

    BeginScissorMode((int)uiPx(8.f),(int)panelY,(int)contentLeftW,(int)panelH);
    float ly=panelY+listPad-g_editorListScrollY;
    for(int i=0;i<unitTypeCount();i++){
        Rectangle rowR={uiPx(8.f)+listPad,ly,contentLeftW-listPad*2.f,listRowH};
        bool sel=(i==g_editTypeIdx); bool hv=ptInRect(mouse,rowR);
        DrawRectangleRec(rowR,sel?Color{28,78,40,255}:hv?Color{20,48,30,255}:Color{12,28,20,240});
        DrawRectangleLinesEx(rowR,1,sel?C_GOLD:Color{40,68,45,255});
        DrawRectangle((int)(rowR.x+uiPx(4.f)),(int)(rowR.y+uiPx(4.f)),(int)uiPx(12.f),(int)uiPx(12.f),{g_unitTypes[i].r,g_unitTypes[i].g,g_unitTypes[i].b,255});
        DrawText(g_unitTypes[i].name,(int)(rowR.x+uiPx(22.f)),(int)(rowR.y+uiPx(6.f)),12,sel?C_GOLD:WHITE);
        if(hv&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)){ g_editTypeIdx=i; loadEditorEditCopy(); }
        ly+=listRowH;
    }
    ly+=uiPx(12.f);
    Rectangle createBtn={uiPx(8.f)+listPad,ly,contentLeftW-listPad*2.f,uiPx(32.f)};
    bool createHv=ptInRect(mouse,createBtn);
    DrawRectangleRec(createBtn,createHv?Color{28,68,48,255}:Color{16,48,28,255});
    DrawRectangleLinesEx(createBtn,1,C_GOLD);
    DrawText("+ Create new",(int)(createBtn.x+uiPx(8.f)),(int)(createBtn.y+uiPx(8.f)),13,WHITE);
    if(createHv&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)){ g_editorNewUnitDialogOpen=true; g_editorNewUnitNameBuf.clear(); }
    EndScissorMode();
    if(listScrollMax>0.f){
        float sbX=uiPx(8.f)+contentLeftW;
        DrawRectangle((int)sbX,(int)panelY,(int)scrollBarW,(int)panelH,{20,28,22,255});
        float thumbH=fmaxf(uiPx(24.f),panelH*panelH/fmaxf(listContentH,1.f));
        float trackH=panelH-thumbH;
        float thumbY=panelY+(trackH>0.f?(g_editorListScrollY/listScrollMax)*trackH:0.f);
        Rectangle thumbR={sbX,thumbY,(float)(int)scrollBarW,thumbH};
        DrawRectangle((int)thumbR.x,(int)thumbR.y,(int)thumbR.width,(int)thumbR.height,Color{45,70,50,255});
        DrawRectangleLinesEx(thumbR,1,Color{80,120,80,255});
    }

    // ─── Right panel: sprite + stats (scissored and scrollable); buttons at screen bottom-right
    if(g_editTypeIdx>=0&&g_editTypeIdx<unitTypeCount()&&g_editorEditCopyValid){
        UnitTypeDef& td=g_unitTypes[g_editTypeIdx];
        float rx=rightX, ry=panelY, pad=uiPx(10.f);
        float labelW=uiPx(140.f), boxW=uiPx(90.f), rowH=uiPx(24.f);
        float rightScrollBarW=uiPx(14.f);
        float rightContentW=rightW-rightScrollBarW;

        // Content height (sprite + all stat rows)
        float contentTop=ry+pad;
        float contentH=uiPx(32.f)+uiPx(120.f)+uiPx(20.f)+rowH+uiPx(4.f)+17.f*rowH+uiPx(20.f);
        float rightScrollMax=fmaxf(0.f,contentH-panelH);
        float rWheel=GetMouseWheelMove();
        if(rWheel!=0.f&&mouse.x>=rightX&&mouse.x<rightX+rightW&&mouse.y>=panelY&&mouse.y<panelY+panelH)
            g_editorRightScrollY-=rWheel*uiPx(32.f);
        g_editorRightScrollY=fmaxf(0.f,fminf(rightScrollMax,g_editorRightScrollY));
        auto rightDrawY=[](float y){ return y-g_editorRightScrollY; };

        BeginScissorMode((int)rightX,(int)panelY,(int)rightContentW,(int)panelH);
        DrawText(g_editorEditCopy.name,(int)(rx+pad),(int)rightDrawY(ry+pad),16,SKYBLUE);
        DrawLine((int)rx,(int)rightDrawY(ry+uiPx(32.f)),(int)(rx+rightContentW),(int)rightDrawY(ry+uiPx(32.f)),{50,50,110,80});

        if(g_editTypeIdx>=0&&g_editTypeIdx<unitTypeCount()){
            Vector2 center={rx+rightContentW/2.f, rightDrawY(ry+uiPx(120.f))};
            float feetY=center.y+unitHeadOffset(g_editTypeIdx,3.f)*0.5f;
            drawUnit(g_editTypeIdx,0,{center.x,feetY},g_editorPreviewAngle,3.f,
                     true,false,UA_IDLE,g_menuTime);
            DrawCircleLines((int)center.x,(int)center.y,(int)uiPx(60.f),{100,100,200,60});
        }

        float sy=ry+uiPx(200.f);
        DrawText("Stats (edit below)",(int)(rx+pad),(int)rightDrawY(sy),11,{100,180,255,255}); sy+=rowH+uiPx(4.f);
        float xLabel=rx+pad, xBox=rx+pad+labelW+uiPx(6.f);
        drawEditorTextBox(xLabel,xBox,rightDrawY(sy),boxW,rowH,0,"Name",mouse); sy+=rowH;
        drawEditorTextBox(xLabel,xBox,rightDrawY(sy),boxW,rowH,1,"Soldiers",mouse); sy+=rowH;
        drawEditorTextBox(xLabel,xBox,rightDrawY(sy),boxW,rowH,2,"HP/Soldier",mouse); sy+=rowH;
        drawEditorTextBox(xLabel,xBox,rightDrawY(sy),boxW,rowH,3,"Armor",mouse); sy+=rowH;
        drawEditorTextBox(xLabel,xBox,rightDrawY(sy),boxW,rowH,4,"Speed",mouse); sy+=rowH;
        drawEditorTextBox(xLabel,xBox,rightDrawY(sy),boxW,rowH,5,"Melee Atk",mouse); sy+=rowH;
        drawEditorTextBox(xLabel,xBox,rightDrawY(sy),boxW,rowH,6,"Melee Def",mouse); sy+=rowH;
        drawEditorTextBox(xLabel,xBox,rightDrawY(sy),boxW,rowH,7,"Mel Base Dmg",mouse); sy+=rowH;
        drawEditorTextBox(xLabel,xBox,rightDrawY(sy),boxW,rowH,8,"Mel AP Dmg",mouse); sy+=rowH;
        drawEditorTextBox(xLabel,xBox,rightDrawY(sy),boxW,rowH,9,"Mel Interval",mouse); sy+=rowH;
        drawEditorTextBox(xLabel,xBox,rightDrawY(sy),boxW,rowH,10,"Range",mouse); sy+=rowH;
        drawEditorTextBox(xLabel,xBox,rightDrawY(sy),boxW,rowH,11,"Miss Base",mouse); sy+=rowH;
        drawEditorTextBox(xLabel,xBox,rightDrawY(sy),boxW,rowH,12,"Miss AP",mouse); sy+=rowH;
        drawEditorTextBox(xLabel,xBox,rightDrawY(sy),boxW,rowH,13,"Miss Reload",mouse); sy+=rowH;
        drawEditorTextBox(xLabel,xBox,rightDrawY(sy),boxW,rowH,14,"R",mouse); sy+=rowH;
        drawEditorTextBox(xLabel,xBox,rightDrawY(sy),boxW,rowH,15,"G",mouse); sy+=rowH;
        drawEditorTextBox(xLabel,xBox,rightDrawY(sy),boxW,rowH,16,"B",mouse); sy+=rowH;
        EndScissorMode();

        if(rightScrollMax>0.f){
            float sbX=rightX+rightContentW;
            DrawRectangle((int)sbX,(int)panelY,(int)rightScrollBarW,(int)panelH,{20,22,35,255});
            float thumbH=fmaxf(uiPx(24.f),panelH*panelH/fmaxf(contentH,1.f));
            float trackH=panelH-thumbH;
            float thumbY=panelY+(trackH>0.f?(g_editorRightScrollY/rightScrollMax)*trackH:0.f);
            Rectangle thumbR={sbX,thumbY,(float)(int)rightScrollBarW,thumbH};
            DrawRectangle((int)thumbR.x,(int)thumbR.y,(int)thumbR.width,(int)thumbR.height,Color{50,60,90,255});
            DrawRectangleLinesEx(thumbR,1,Color{80,90,130,255});
        }

        // Buttons at bottom right of screen
        float btnY=(float)SCREEN_H-uiPx(44.f);
        float bW=uiPx(80.f), bGap=uiPx(10.f);
        float defaultW=uiPx(72.f);
        float btnRight=SCREEN_W-uiPx(14.f);
        float saveX=btnRight-bW-defaultW-bGap-bW-bGap-bW;
        float cancelX=btnRight-bW-defaultW-bGap-bW;
        float defaultX=btnRight-defaultW;
        if(drawSmBtn({saveX,btnY,bW,uiPx(30.f)},"Save",mouse,{20,50,20,255},{40,90,40,255})){
            td=g_editorEditCopy;
            rebuildTexture(g_editTypeIdx);
            loadEditorEditCopy();
        }
        if(drawSmBtn({cancelX,btnY,bW,uiPx(30.f)},"Cancel",mouse,{50,25,25,255},{80,40,40,255}))
            loadEditorEditCopy();
        int vanIdx=getVanillaIndexByName(g_editorEditCopy.name);
        if(g_editorEditCopy.isBuiltin&&vanIdx>=0&&drawSmBtn({defaultX,btnY,defaultW,uiPx(30.f)},"Default",mouse,{30,40,50,255},{50,60,90,255})){
            g_editorEditCopy=g_vanillaUnitTypes[vanIdx];
            g_editorEditCopy.name[sizeof(g_editorEditCopy.name)-1]='\0';
        }
    }

    return STATE_UNIT_EDITOR;
}

// ═══════════════════════════════════════════════════════════════════════════
//  STATE: QUICK BATTLE SETUP
// ═══════════════════════════════════════════════════════════════════════════
GameState updateDrawQuickBattleSetup(Vector2 mouse){
    int n=unitTypeCount();
    g_quickSetup.playerCounts.resize(n,0);
    g_quickSetup.enemyCounts.resize(n,0);

    // Scale-aware layout to avoid overlapping text
    float topBarH=uiPx(54.f);
    float titleY=uiPx(10.f);
    float subtitleY=titleY+(float)uiFS(26)+uiPx(10.f);
    float listTop=subtitleY+(float)uiFS(12)+uiPx(18.f);
    float rowH=uiPx(40.f);
    float headerH=(float)uiFS(13)+uiPx(8.f);
    float barH=uiPx(72.f);
    float barY=(float)SCREEN_H-barH;
    float ctrlBtn=uiPx(22.f);
    float ctrlGap=uiPx(50.f);

    ClearBackground({8,10,8,255});
    for(int gx=0;gx<SCREEN_W;gx+=40) DrawLine(gx,0,gx,SCREEN_H,{20,32,20,50});
    for(int gy=0;gy<SCREEN_H;gy+=40) DrawLine(0,gy,SCREEN_W,gy,{20,32,20,50});
    DrawRectangle(0,0,SCREEN_W,(int)topBarH,{0,0,0,220});
    DrawText("QUICK BATTLE SETUP",(int)uiPx(14.f),(int)titleY,26,C_GOLD);
    DrawText("Instant battle without campaign",(int)uiPx(14.f),(int)subtitleY,12,C_SECONDARY);

    float colW=(SCREEN_W-20)/2.f;
    int pTotal=0,eTotal=0;
    for(int v:g_quickSetup.playerCounts) pTotal+=v;
    for(int v:g_quickSetup.enemyCounts) eTotal+=v;

    DrawText("YOUR ARMY",(int)uiPx(14.f),(int)(listTop+uiPx(4.f)),13,{80,140,255,255});
    DrawText("ENEMY ARMY",(int)(colW+uiPx(14.f)),(int)(listTop+uiPx(4.f)),13,{255,100,100,255});
    float lineY=listTop+headerH;
    DrawLine(0,(int)lineY,SCREEN_W,(int)lineY,{50,70,40,100});
    listTop=lineY+uiPx(8.f);

    for(int i=0;i<n&&i<16;i++){
        const UnitTypeDef& td=g_unitTypes[i];
        float ry=listTop+i*rowH;
        if(ry+rowH>barY-uiPx(8.f)) break;

        float rowPad=(rowH-ctrlBtn)*0.5f;
        if(rowPad<2.f) rowPad=2.f;
        float textY=ry+(rowH-(float)uiFS(12))*0.5f;
        if(textY<ry+2.f) textY=ry+2.f;
        float iconY=ry+(rowH-uiPx(14.f))*0.5f;

        // Player side
        {
            Color rowbg=(i%2==0)?Color{10,18,28,200}:Color{8,14,22,200};
            DrawRectangle(0,(int)ry,(int)(colW-2),(int)rowH,rowbg);
            DrawRectangle((int)uiPx(8.f),(int)iconY,(int)uiPx(14.f),(int)uiPx(14.f),{td.r,td.g,td.b,255});
            DrawText(td.name,(int)uiPx(26.f),(int)textY,12,WHITE);
            float bx2=colW-uiPx(90.f);
            bool mhv=CheckCollisionPointRec(mouse,{bx2,ry+rowPad,ctrlBtn,ctrlBtn});
            bool phv=CheckCollisionPointRec(mouse,{bx2+ctrlGap,ry+rowPad,ctrlBtn,ctrlBtn});
            DrawRectangle((int)bx2,(int)(ry+rowPad),(int)ctrlBtn,(int)ctrlBtn,mhv?Color{80,60,30,255}:Color{40,30,15,255});
            DrawRectangleLinesEx({bx2,ry+rowPad,ctrlBtn,ctrlBtn},1,{100,80,40,255});
            DrawText("-",(int)(bx2+uiPx(7.f)),(int)(ry+rowPad+2),15,WHITE);
            if(mhv&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)&&g_quickSetup.playerCounts[i]>0)
                g_quickSetup.playerCounts[i]--;
            DrawText(TextFormat("%d",g_quickSetup.playerCounts[i]),(int)(bx2+ctrlBtn+uiPx(4.f)),(int)(ry+rowPad+2),14,WHITE);
            DrawRectangle((int)(bx2+ctrlGap),(int)(ry+rowPad),(int)ctrlBtn,(int)ctrlBtn,phv?Color{30,80,30,255}:Color{15,40,15,255});
            DrawRectangleLinesEx({bx2+ctrlGap,ry+rowPad,ctrlBtn,ctrlBtn},1,{40,100,40,255});
            DrawText("+",(int)(bx2+ctrlGap+uiPx(6.f)),(int)(ry+rowPad+2),15,GREEN);
            if(phv&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)&&pTotal<16) g_quickSetup.playerCounts[i]++;
        }
        // Enemy side
        {
            float ex2=colW+2;
            Color rowbg=(i%2==0)?Color{26,8,8,200}:Color{20,6,6,200};
            DrawRectangle((int)ex2,(int)ry,(int)(colW-2),(int)rowH,rowbg);
            unsigned char er=clampU8((int)(td.r*0.3f+150));
            DrawRectangle((int)(ex2+uiPx(8.f)),(int)iconY,(int)uiPx(14.f),(int)uiPx(14.f),{er,(unsigned char)(td.g/4),(unsigned char)(td.b/4),255});
            DrawText(td.name,(int)(ex2+uiPx(26.f)),(int)textY,12,WHITE);
            float bxe=ex2+colW-uiPx(90.f);
            bool mhv=CheckCollisionPointRec(mouse,{bxe,ry+rowPad,ctrlBtn,ctrlBtn});
            bool phv=CheckCollisionPointRec(mouse,{bxe+ctrlGap,ry+rowPad,ctrlBtn,ctrlBtn});
            DrawRectangle((int)bxe,(int)(ry+rowPad),(int)ctrlBtn,(int)ctrlBtn,mhv?Color{80,30,30,255}:Color{40,15,15,255});
            DrawRectangleLinesEx({bxe,ry+rowPad,ctrlBtn,ctrlBtn},1,{100,40,40,255});
            DrawText("-",(int)(bxe+uiPx(7.f)),(int)(ry+rowPad+2),15,WHITE);
            if(mhv&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)&&g_quickSetup.enemyCounts[i]>0)
                g_quickSetup.enemyCounts[i]--;
            DrawText(TextFormat("%d",g_quickSetup.enemyCounts[i]),(int)(bxe+ctrlBtn+uiPx(4.f)),(int)(ry+rowPad+2),14,WHITE);
            DrawRectangle((int)(bxe+ctrlGap),(int)(ry+rowPad),(int)ctrlBtn,(int)ctrlBtn,phv?Color{80,30,30,255}:Color{40,15,15,255});
            DrawRectangleLinesEx({bxe+ctrlGap,ry+rowPad,ctrlBtn,ctrlBtn},1,{120,40,40,255});
            DrawText("+",(int)(bxe+ctrlGap+uiPx(6.f)),(int)(ry+rowPad+2),15,RED);
            if(phv&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)&&eTotal<16) g_quickSetup.enemyCounts[i]++;
        }
    }

    DrawLine((int)colW,(int)listTop,(int)colW,(int)(barY-uiPx(4.f)),{50,70,40,120});

    DrawRectangle(0,(int)barY,SCREEN_W,(int)barH,{0,0,0,220});
    DrawLine(0,(int)barY,SCREEN_W,(int)barY,{80,65,30,120});
    float barLine1=barY+uiPx(14.f);
    float barLine2=barLine1+(float)uiFS(13)+uiPx(8.f);
    DrawText(TextFormat("Your: %d groups  |  Enemy: %d groups",pTotal,eTotal),
             (int)uiPx(14.f),(int)barLine1,13,C_SECONDARY);
    DrawText(TextFormat("Turn: %d",g_campaign.turn),(int)uiPx(14.f),(int)barLine2,13,C_SECONDARY);

    bool canStart=(pTotal>0&&eTotal>0);
    Color cn=canStart?Color{28,65,28,255}:Color{30,30,30,255};
    Color ch=canStart?Color{50,120,50,255}:Color{30,30,30,255};
    float btnH=uiPx(50.f);
    float btnY=barY+(barH-btnH)*0.5f;
    if(drawButton({(float)(SCREEN_W-uiPx(420.f)),btnY,uiPx(280.f),btnH},"START BATTLE",mouse,cn,ch)&&canStart){
        std::vector<std::pair<int,int>> pGroups,eGroups;
        // Un batallon por regimiento marcado en la UI (antes spawneaba solo 1 por tipo)
        for(int i=0;i<n;i++){
            for(int r=0;r<g_quickSetup.playerCounts[i];r++) pGroups.push_back({i,g_unitTypes[i].soldierCount});
            for(int r=0;r<g_quickSetup.enemyCounts[i];r++) eGroups.push_back({i,g_unitTypes[i].soldierCount});
        }
        initBattle(pGroups,eGroups,TERRAIN_PLAIN,-1,false,"Quick Battle");
        g_quickBattle=true;
        return STATE_BATTLE;
    }
    if(!canStart) DrawText("Add units to both sides",(int)(SCREEN_W-uiPx(420.f)),(int)(barY+barH-uiPx(12.f)),12,{180,90,40,255});
    if(drawButton({(float)(SCREEN_W-uiPx(128.f)),btnY,uiPx(114.f),btnH},"BACK",mouse,
                  {50,25,25,255},{80,40,40,255})) return STATE_MAIN_MENU;
    return STATE_QUICK_BATTLE_SETUP;
}

// ═══════════════════════════════════════════════════════════════════════════
//  STATE: MARKETPLACE (6.5: Resource trading system)
// ═══════════════════════════════════════════════════════════════════════════
GameState updateDrawMarketplace(Vector2 mouse){
    ClearBackground(C_BG);
    for(int gx=0;gx<SCREEN_W;gx+=40) DrawLine(gx,0,gx,SCREEN_H,{20,18,14,50});
    for(int gy=0;gy<SCREEN_H;gy+=40) DrawLine(0,gy,SCREEN_W,gy,{20,18,14,50});

    DrawRectangle(0,0,SCREEN_W,50,{0,0,0,220});
    DrawText("MARKETPLACE — Trade Resources",14,10,26,C_GOLD);
    DrawText(TextFormat("Turn: %d  |  Current reserves shown below",g_campaign.turn),14,38,12,C_SECONDARY);

    Resources& res=(g_campaign.pvpMode&&g_netSession.role==NET_ROLE_CLIENT)
                   ?g_campaign.clientRes:g_campaign.res;
    static const char* resNames[5]={"Gold","Food","Wood","Stone","Iron"};
    static const Color resColors[5]={{200,165,80,255},{140,100,50,255},{100,150,60,255},
                                       {160,150,140,255},{200,100,50,255}};
    float* resVals[5]={&res.gold,&res.food,&res.wood,&res.stone,&res.iron};

    float tabX=14.f, tabY=60.f, tabW=240.f, tabH=34.f;
    // Tabs for each resource
    for(int i=0;i<5;i++){
        float tx=tabX+i*(tabW+2);
        bool sel=(g_tradeTab==i);
        Color tc=sel?Color{40,80,80,255}:Color{20,40,40,255};
        Color th=sel?Color{60,120,120,255}:Color{35,65,65,255};
        if(drawSmBtn({tx,tabY,tabW,tabH},resNames[i],mouse,tc,th)) g_tradeTab=i;
        if(sel) DrawRectangleLinesEx({tx,tabY,tabW,tabH},2,C_GOLD);
    }

    tabY+=48.f;
    float panelH=(float)(SCREEN_H-100-50);

    // Trading panel for selected resource
    const char* resName=resNames[g_tradeTab];
    Color resCol=resColors[g_tradeTab];
    float* resVal=resVals[g_tradeTab];

    DrawRectangle(8,(int)tabY,SCREEN_W-16,(int)panelH,{12,10,8,200});
    DrawRectangleLinesEx({8,tabY,(float)(SCREEN_W-16),panelH},1,resCol);

    float cx=20.f, cy=tabY+15.f;

    // Current reserves
    DrawText(TextFormat("Current %s: %.0f",resName,*resVal),(int)cx,(int)cy,16,C_PARCHMENT);
    cy+=28;

    // Exchange rate info
    float rate=TRADE_RATES[g_tradeTab];
    DrawText(TextFormat("Exchange rate: 1 %s = %.2f gold (sell)",resName,rate),
             (int)cx,(int)cy,13,C_SECONDARY);
    cy+=22;
    DrawText(TextFormat("Buy: %.2f gold per unit",rate*1.3f),
             (int)cx,(int)cy,13,resCol);
    cy+=28;

    // Amount slider
    g_tradeAmount=drawIntSlider({cx,cy,400.f,14},g_tradeAmount,1,100,"Amount to trade:",mouse,resCol);
    cy+=30;

    float buyPrice=g_tradeAmount*rate*1.3f;
    float sellPrice=g_tradeAmount*rate;

    // Buy button
    bool canBuy=(res.gold>=buyPrice);
    Color buyCol=canBuy?Color{30,80,30,255}:Color{40,20,20,255};
    Color buyColH=canBuy?Color{55,120,55,255}:Color{50,25,25,255};
    if(drawButton({cx,cy,180,44},
                  TextFormat("BUY %d (%.0f g)",g_tradeAmount,buyPrice),
                  mouse,buyCol,buyColH)){
        if(canBuy&&netCanEdit()){ // Fase H: el cliente no muta recursos localmente
            res.gold-=buyPrice;
            *resVal+=g_tradeAmount;
            battleLogAdd(TextFormat("[Market] Bought %d %s for %.0f gold",g_tradeAmount,resName,buyPrice));
            playSfx(SFX_COIN,0.9f);
            netMarkDirty();
        } else playSfx(SFX_UI_ERROR,0.8f);
    }

    // Sell button
    bool canSell=(*resVal>=g_tradeAmount);
    Color sellCol=canSell?Color{30,30,80,255}:Color{40,20,20,255};
    Color sellColH=canSell?Color{55,55,120,255}:Color{50,25,25,255};
    if(drawButton({cx+190,cy,180,44},
                  TextFormat("SELL %d (%.0f g)",g_tradeAmount,sellPrice),
                  mouse,sellCol,sellColH)){
        if(canSell&&netCanEdit()){ // Fase H: el cliente no muta recursos localmente
            *resVal-=g_tradeAmount;
            res.gold+=sellPrice;
            battleLogAdd(TextFormat("[Market] Sold %d %s for %.0f gold",g_tradeAmount,resName,sellPrice));
            playSfx(SFX_COIN,0.9f,0.95f);
            netMarkDirty();
        } else playSfx(SFX_UI_ERROR,0.8f);
    }

    cy+=54;

    // All resources summary
    DrawLine(20,(int)(cy+6),(int)(SCREEN_W-20),(int)(cy+6),{50,45,35,100});
    cy+=14;
    DrawText("All Reserves:",(int)cx,(int)cy,13,C_GOLD); cy+=16;
    for(int i=0;i<5;i++){
        DrawText(TextFormat("%s: %.0f",resNames[i],*resVals[i]),(int)(cx+10),(int)(cy+i*16),12,
                 i==g_tradeTab?resColors[i]:Color{100,100,100,255});
    }

    // Bottom buttons
    DrawRectangle(0,SCREEN_H-50,SCREEN_W,50,{0,0,0,210});
    if(drawSmBtn({(float)(SCREEN_W-130),(float)(SCREEN_H-42),116,30},"BACK",mouse,
                  {50,25,25,255},{80,40,40,255}))
        return STATE_CAMPAIGN_MAP;
    return STATE_MARKETPLACE;
}

// ═══════════════════════════════════════════════════════════════════════════
//  STATE: DIPLOMACY (Fase I)
// ═══════════════════════════════════════════════════════════════════════════
// Estado del sub-panel de trueque (reseteado al abrir la pantalla)
static int g_diploTradeFaction=-1; // -1 = panel de trueque cerrado
static int g_diploGiveRes=0;       // Gold
static int g_diploRecvRes=1;       // Food
static int g_diploGiveAmt=300;
static int g_diploRecvAmt=100;
static const Color DIPLO_OK_C   ={120,220,120,255}; // estado "aliado" en filas
static const Color DIPLO_PACT_C ={ 80,160,255,255}; // estado "pacto" en filas

GameState updateDrawDiplomacy(Vector2 mouse){
    ClearBackground(C_BG);
    for(int gx=0;gx<SCREEN_W;gx+=40) DrawLine(gx,0,gx,SCREEN_H,{20,18,14,50});
    for(int gy=0;gy<SCREEN_H;gy+=40) DrawLine(0,gy,SCREEN_W,gy,{20,18,14,50});

    static const char* resNames[5]={"Gold","Food","Wood","Stone","Iron"};
    static const Color resColors[5]={{200,165,80,255},{140,100,50,255},{100,150,60,255},
                                       {160,150,140,255},{200,100,50,255}};

    DrawRectangle(0,0,SCREEN_W,50,{0,0,0,220});
    DrawText("DIPLOMACY - Alliances and Trade",14,6,26,C_GOLD);
    Resources& res=(g_campaign.pvpMode&&g_netSession.role==NET_ROLE_CLIENT)
                   ?g_campaign.clientRes:g_campaign.res;
    DrawText(TextFormat("Turn: %d  |  Gold: %.0f  Food: %.0f  Wood: %.0f  Stone: %.0f  Iron: %.0f",
             g_campaign.turn,res.gold,res.food,res.wood,res.stone,res.iron),
             14,36,12,C_SECONDARY);

    int total=totalMilitaryPower();

    // Una fila por faccion IA (1..4); el jugador y Neutral no se negocian
    for(int k=0;k<4;k++){
        int f=k+1;
        float y0=56.f+(float)k*88.f;
        DrawRectangle(8,(int)y0,SCREEN_W-16,84,{12,10,8,200});
        DrawRectangleLinesEx({8,y0,(float)(SCREEN_W-16),84},1,factionColors[f]);
        DrawText(factionNames[f],20,(int)y0+6,17,factionColors[f]);
        DrawText(TextFormat("Military power: %d  (world: %d)",
                 factionMilitaryPower(f),total),20,(int)y0+34,12,C_SECONDARY);
        // Estado diplomatico (colores fijos por asercion de test)
        DrawText(g_campaign.allied[f]?"Mil: ALLIED":"Mil: -",
                 340,(int)y0+8,13,g_campaign.allied[f]?DIPLO_OK_C:Color{110,110,110,255});
        DrawText(g_campaign.tradePact[f]?"Pact: YES":"Pact: -",
                 340,(int)y0+32,13,g_campaign.tradePact[f]?DIPLO_PACT_C:Color{110,110,110,255});
        // Botones: alianza militar / pacto comercial / trueque
        if(drawSmBtn({560,y0+22,150,36},g_campaign.allied[f]?"DISSOLVE MIL":"MIL ALLIANCE",
                     mouse,{55,45,20,255},{95,75,35,255})){
            if(!netCanEdit()){ /* Fase H: cliente no negocia */ }
            else{
                if(g_campaign.allied[f]) dissolveRelations(f,true);
                else proposeMilitaryAlliance(f);
                netMarkDirty();
            }
        }
        if(drawSmBtn({720,y0+22,150,36},g_campaign.tradePact[f]?"DISSOLVE PACT":"COMM PACT",
                     mouse,{25,45,65,255},{45,75,115,255})){
            if(!netCanEdit()){ /* Fase H: cliente no negocia */ }
            else{
                if(g_campaign.tradePact[f]) dissolveRelations(f,false);
                else proposeCommercialPact(f);
                netMarkDirty();
            }
        }
        if(drawSmBtn({880,y0+22,150,36},"TRADE",mouse,
                     g_campaign.tradePact[f]?Color{30,55,35,255}:Color{25,25,25,255},
                     g_campaign.tradePact[f]?Color{50,95,60,255}:Color{40,40,40,255})){
            g_diploTradeFaction=f;
        }
    }

    // Ultimo resultado diplomatico (rect fijo para tests de pixeles)
    DrawRectangle(8,412,SCREEN_W-16,30,{0,0,0,200});
    DrawRectangleLinesEx({8,412,(float)(SCREEN_W-16),30},1,{80,65,30,160});
    if(g_diploMsg[0]) DrawText(g_diploMsg,20,418,15,g_diploMsgCol);

    // Sub-panel de trueque (con faccion IA seleccionada)
    if(g_diploTradeFaction>=0){
        int f=g_diploTradeFaction;
        float py=450.f;
        DrawRectangle(8,(int)py,SCREEN_W-16,276,{12,10,8,210});
        DrawRectangleLinesEx({8,py,(float)(SCREEN_W-16),276},1,C_GOLD);
        DrawText(TextFormat("TRADE OFFER - %s",factionNames[f]),20,(int)py+6,15,C_GOLD);

        // Lo que el jugador DA
        DrawText("YOU GIVE:",20,(int)py+34,13,C_PARCHMENT);
        for(int i=0;i<5;i++){
            float tx=110.f+(float)i*134.f;
            bool sel=(g_diploGiveRes==i);
            if(drawSmBtn({tx,py+28,128,30},resNames[i],mouse,
                         sel?Color{60,60,30,255}:Color{30,30,20,255},
                         sel?Color{95,95,50,255}:Color{50,50,35,255})) g_diploGiveRes=i;
            if(sel) DrawRectangleLinesEx({tx,py+28,128,30},2,C_GOLD);
        }
        DrawText("Amount:",20,(int)py+72,13,C_SECONDARY);
        if(drawSmBtn({100,py+64,36,30},"-",mouse)) g_diploGiveAmt=std::max(25,g_diploGiveAmt-25);
        DrawText(TextFormat("%d",g_diploGiveAmt),148,(int)py+70,16,resColors[g_diploGiveRes]);
        if(drawSmBtn({200,py+64,36,30},"+",mouse)) g_diploGiveAmt=std::min(1000,g_diploGiveAmt+25);

        // Lo que el jugador PIDE
        DrawText("YOU RECEIVE:",20,(int)py+110,13,C_PARCHMENT);
        for(int i=0;i<5;i++){
            float tx=110.f+(float)i*134.f;
            bool sel=(g_diploRecvRes==i);
            if(drawSmBtn({tx,py+104,128,30},resNames[i],mouse,
                         sel?Color{60,60,30,255}:Color{30,30,20,255},
                         sel?Color{95,95,50,255}:Color{50,50,35,255})) g_diploRecvRes=i;
            if(sel) DrawRectangleLinesEx({tx,py+104,128,30},2,C_GOLD);
        }
        DrawText("Amount:",20,(int)py+148,13,C_SECONDARY);
        if(drawSmBtn({100,py+140,36,30},"-",mouse)) g_diploRecvAmt=std::max(25,g_diploRecvAmt-25);
        DrawText(TextFormat("%d",g_diploRecvAmt),148,(int)py+146,16,resColors[g_diploRecvRes]);
        if(drawSmBtn({200,py+140,36,30},"+",mouse)) g_diploRecvAmt=std::min(1000,g_diploRecvAmt+25);

        // Valor del trato segun la personalidad de la IA (misma tabla que
        // proposeTradeDeal en campaign.cpp)
        static const float factor[FACTION_COUNT]={1.f,1.3f,1.15f,1.f,1.25f,1.f};
        float gv=(float)g_diploGiveAmt*TRADE_RATES[g_diploGiveRes];
        float rv=(float)g_diploRecvAmt*TRADE_RATES[g_diploRecvRes];
        DrawText(TextFormat("Offer %.0f pts vs ask %.0f pts required (AI factor %.2f)",
                 gv,rv*factor[f],factor[f]),20,(int)py+186,13,C_SECONDARY);

        if(drawSmBtn({20,py+222,200,44},"OFFER DEAL",mouse,{30,70,35,255},{55,120,60,255})){
            if(netCanEdit()){ // Fase H: el cliente no negocia trueque
                proposeTradeDeal(f,g_diploGiveRes,(float)g_diploGiveAmt,
                                 g_diploRecvRes,(float)g_diploRecvAmt);
                netMarkDirty();
            } else playSfx(SFX_UI_ERROR,0.8f);
        }
        if(drawSmBtn({240,py+222,140,44},"CLOSE",mouse,{50,25,25,255},{80,40,40,255})){
            g_diploTradeFaction=-1;
        }
    }

    // Bottom bar
    DrawRectangle(0,SCREEN_H-50,SCREEN_W,50,{0,0,0,210});
    if(drawSmBtn({(float)(SCREEN_W-130),(float)(SCREEN_H-42),116,30},"BACK",mouse,
                  {50,25,25,255},{80,40,40,255})){
        g_diploTradeFaction=-1;
        return STATE_CAMPAIGN_MAP;
    }
    return STATE_DIPLOMACY;
}

// ═══════════════════════════════════════════════════════════════════════════
//  STATE: MAIN MENU
// ═══════════════════════════════════════════════════════════════════════════
GameState updateDrawMainMenu(Vector2 mouse){
    ClearBackground({6,8,6,255});
    // Fog rects
    static float fogScroll=0;
    fogScroll+=20.f*GetFrameTime();
    for(int i=0;i<12;i++){
        float fx=fmodf(fogScroll+i*150.f,(float)SCREEN_W+300)-150;
        float fy=100.f+sinf((float)i*0.7f+fogScroll*0.01f)*50.f;
        DrawRectangle((int)fx,(int)fy,120,(int)(SCREEN_H-fy),{15,20,12,(unsigned char)(8+i*2)});
    }
    // Grid
    for(int x2=0;x2<SCREEN_W;x2+=60) DrawLine(x2,0,x2,SCREEN_H,{22,30,18,40});
    for(int y2=0;y2<SCREEN_H;y2+=60) DrawLine(0,y2,SCREEN_W,y2,{22,30,18,40});
    // Scanlines
    for(int y2=0;y2<SCREEN_H;y2+=3) DrawLine(0,y2,SCREEN_W,y2,{0,0,0,18});

    // Title — scale-aware layout so subtitle and buttons don't overlap at 250%
    const char* title="MEDIEVAL CONQUEST";
    int tsz=58;
    int ttw=MeasureText(title,tsz);
    float titleY=uiPx(50.f);
    float titleX=(float)(SCREEN_W/2-ttw/2);
    DrawText(title,(int)(titleX+3),(int)(titleY+3),tsz,{0,60,0,180});
    DrawText(title,(int)titleX,(int)titleY,tsz,C_GOLD);
    // Subtitle well below title (scaled font height + gap)
    float subY=titleY+(float)uiFS(tsz)+uiPx(20.f);
    const char* sub="Campaign RTS - Forge an Empire";
    int subW=MeasureText(sub,17);
    DrawText(sub,SCREEN_W/2-subW/2,(int)subY,17,{160,140,80,255});
    float lineY=subY+(float)uiFS(17)+uiPx(14.f);
    float lineW=uiPx(230.f);
    DrawLine(SCREEN_W/2-(int)lineW,(int)lineY,SCREEN_W/2+(int)lineW,(int)lineY,{80,65,30,180});
    DrawLine(SCREEN_W/2-(int)uiPx(210.f),(int)(lineY+uiPx(4.f)),SCREEN_W/2+(int)uiPx(210.f),(int)(lineY+uiPx(4.f)),{50,40,20,120});

    // Buttons — campaign selection then continue / quick battle
    float menuTop=lineY+uiPx(28.f);
    float bw=uiPx(350.f), bh=uiPx(50.f);
    float bx=(float)SCREEN_W/2.f-bw/2.f;
    float step=uiPx(60.f);
    DrawText("New campaign:",(int)(bx),(int)(menuTop-uiPx(20.f)),14,{120,110,70,255});
    if(drawButton({bx,menuTop,bw,bh},CAMPAIGN_NAMES[0],mouse)) { newCampaign(0); netMarkDirty(); return STATE_CAMPAIGN_MAP; }
    if(drawButton({bx,menuTop+step,bw,bh},CAMPAIGN_NAMES[1],mouse)) { newCampaign(1); netMarkDirty(); return STATE_CAMPAIGN_MAP; }
    if(drawButton({bx,menuTop+step*2.f,bw,bh},CAMPAIGN_NAMES[2],mouse)) { newCampaign(2); netMarkDirty(); return STATE_CAMPAIGN_MAP; }
    if(drawButton({bx,menuTop+step*3.f,bw,bh},CAMPAIGN_NAMES[3],mouse)) { newCampaign(3); netMarkDirty(); return STATE_CAMPAIGN_MAP; }
    if(drawButton({bx,menuTop+step*4.f,bw,bh},"CONTINUE",mouse,
                  g_hasSave?Color{40,55,40,255}:Color{30,30,30,255},
                  g_hasSave?Color{70,110,60,255}:Color{30,30,30,255})&&g_hasSave){
        if(loadGame()){ netMarkDirty(); return STATE_CAMPAIGN_MAP; }
    }
    if(drawButton({bx,menuTop+step*5.f,bw,bh},"QUICK BATTLE",mouse)){
        g_quickSetup.playerCounts.assign(unitTypeCount(),0);
        g_quickSetup.enemyCounts.assign(unitTypeCount(),0);
        if(unitTypeCount()>0){
            // Perf stress: QB_STRESS=N regimientos por bando (medicion de rendimiento)
            int def=2;
            if(const char* e=getenv("QB_STRESS")){ int v=atoi(e); if(v>0) def=std::min(v,16); }
            g_quickSetup.playerCounts[0]=def;
            g_quickSetup.enemyCounts[0]=def;
        }
        return STATE_QUICK_BATTLE_SETUP;
    }
    if(drawButton({bx,menuTop+step*6.f,bw,bh},"UNIT EDITOR",mouse)){
        return STATE_UNIT_EDITOR;
    }
    if(drawButton({bx,menuTop+step*7.f,bw,bh},"MULTIPLAYER",mouse)){
        return STATE_MULTIPLAYER;
    }
    if(drawButton({bx,menuTop+step*8.f,bw,bh},"SETTINGS",mouse)) return STATE_SETTINGS;
    if(drawButton({bx,menuTop+step*9.f,bw,bh},"EXIT",mouse,{60,28,28,255},{100,45,45,255})){
        g_quitRequested=true;
    }

    // Bottom hint — keep fully on screen with scaled margin
    const char* hint="LClick/Drag: select  |  RClick: move/attack  |  WASD/Arrows: pan  |  Wheel: zoom  |  F11: fullscreen";
    int hintFs=10;
    int hintH=uiFS(hintFs);
    float hintMargin=uiPx(24.f);
    float hintY=(float)SCREEN_H-hintMargin-(float)hintH;
    int hintTw=MeasureText(hint,hintFs);
    DrawText(hint,SCREEN_W/2-hintTw/2,(int)hintY,hintFs,{60,80,50,255});
    return STATE_MAIN_MENU;
}

// ═══════════════════════════════════════════════════════════════════════════
//  STATE: VICTORY
// ═══════════════════════════════════════════════════════════════════════════
GameState updateDrawVictory(Vector2 mouse){
    ClearBackground({5,10,5,255});
    DrawRectangleGradientV(0,0,SCREEN_W,SCREEN_H,{0,40,10,255},{5,10,5,255});
    // Title
    const char* ttl="VICTORY!";
    int tsz=72;
    int ttw=MeasureText(ttl,tsz);
    DrawText(ttl,SCREEN_W/2-ttw/2+4,60+4,tsz,{0,60,0,160});
    DrawText(ttl,SCREEN_W/2-ttw/2,60,tsz,{80,220,80,255});
    DrawLine(SCREEN_W/2-250,145,SCREEN_W/2+250,145,C_GOLD);

    DrawText("Your empire now dominates the realm!",
             SCREEN_W/2-MeasureText("Your empire now dominates the realm!",18)/2,
             165,18,C_PARCHMENT);

    // Stats
    float sy=210.f;
    DrawText(TextFormat("Final Turn: %d",g_campaign.turn),(int)(SCREEN_W/2-200),(int)sy,15,C_SECONDARY); sy+=24;
    DrawText(TextFormat("Battles Won: %d",g_campaign.battlesWon),(int)(SCREEN_W/2-200),(int)sy,15,C_PARCHMENT); sy+=24;
    DrawText(TextFormat("Battles Lost: %d",g_campaign.battlesLost),(int)(SCREEN_W/2-200),(int)sy,15,C_SECONDARY); sy+=24;
    DrawText(TextFormat("Peak Provinces Held: %d",g_campaign.peakProvinces),(int)(SCREEN_W/2-200),(int)sy,15,C_GOLD); sy+=40;

    if(drawButton({(float)(SCREEN_W/2-180),(float)sy,360,54},"NEW CAMPAIGN",mouse,{30,65,30,255},{55,110,50,255})){
        newCampaign(g_campaign.campaignId);
        netMarkDirty(); // Fase H: nueva campaña -> retransmitir estado inicial
        return STATE_CAMPAIGN_MAP;
    }
    if(drawButton({(float)(SCREEN_W/2-180),(float)(sy+64),360,50},"MAIN MENU",mouse,{50,25,25,255},{80,40,40,255}))
        return STATE_MAIN_MENU;
    return STATE_VICTORY;
}

// ═══════════════════════════════════════════════════════════════════════════
//  STATE: DEFEAT
// ═══════════════════════════════════════════════════════════════════════════
GameState updateDrawDefeat(Vector2 mouse){
    ClearBackground({10,5,5,255});
    DrawRectangleGradientV(0,0,SCREEN_W,SCREEN_H,{40,0,0,255},{10,5,5,255});
    const char* ttl="DEFEAT";
    int tsz=72;
    int ttw=MeasureText(ttl,tsz);
    DrawText(ttl,SCREEN_W/2-ttw/2+4,60+4,tsz,{60,0,0,160});
    DrawText(ttl,SCREEN_W/2-ttw/2,60,tsz,{220,60,60,255});
    DrawLine(SCREEN_W/2-250,145,SCREEN_W/2+250,145,C_COPPER);

    DrawText("Your kingdom has fallen. The realm mourns.",
             SCREEN_W/2-MeasureText("Your kingdom has fallen. The realm mourns.",16)/2,
             165,16,C_SECONDARY);

    float sy=210.f;
    DrawText(TextFormat("Final Turn: %d",g_campaign.turn),(int)(SCREEN_W/2-200),(int)sy,15,C_SECONDARY); sy+=24;
    DrawText(TextFormat("Battles Won: %d",g_campaign.battlesWon),(int)(SCREEN_W/2-200),(int)sy,15,C_PARCHMENT); sy+=24;
    DrawText(TextFormat("Battles Lost: %d",g_campaign.battlesLost),(int)(SCREEN_W/2-200),(int)sy,15,C_ENEMY_COL); sy+=24;
    DrawText(TextFormat("Peak Provinces Held: %d",g_campaign.peakProvinces),(int)(SCREEN_W/2-200),(int)sy,15,C_SECONDARY); sy+=40;

    if(drawButton({(float)(SCREEN_W/2-180),(float)sy,360,54},"TRY AGAIN",mouse,{55,20,20,255},{90,35,35,255})){
        newCampaign(g_campaign.campaignId);
        netMarkDirty(); // Fase H: nueva campaña -> retransmitir estado inicial
        return STATE_CAMPAIGN_MAP;
    }
    if(drawButton({(float)(SCREEN_W/2-180),(float)(sy+64),360,50},"MAIN MENU",mouse,{50,25,25,255},{80,40,40,255}))
        return STATE_MAIN_MENU;
    return STATE_DEFEAT;
}

// ═══════════════════════════════════════════════════════════════════════════
//  FASE G — MULTIPLAYER LOBBY (host/join; sin sincronización de juego aun)
// ═══════════════════════════════════════════════════════════════════════════
GameState updateDrawMultiplayer(Vector2 mouse){
    // Fase H: el poll lo hace netSyncPump() en el main loop (una vez/frame)

    ClearBackground(C_BG);
    DrawRectangleGradientV(0,0,SCREEN_W,SCREEN_H,{10,8,6,255},C_BG);
    int titleFs=32;
    float titleY=uiPx(30.f);
    int tw2=MeasureText("MULTIPLAYER",titleFs);
    DrawText("MULTIPLAYER",SCREEN_W/2-tw2/2,(int)titleY,titleFs,C_GOLD);
    float lineY=titleY+(float)uiFS(titleFs)+uiPx(10.f);
    float lineW=uiPx(200.f);
    DrawLine(SCREEN_W/2-(int)lineW,(int)lineY,SCREEN_W/2+(int)lineW,(int)lineY,C_GOLD);

    float cw=fminf(uiPx(520.f),(float)SCREEN_W-uiPx(60.f));
    float cx=(float)SCREEN_W*0.5f - cw*0.5f;
    float cy=lineY+uiPx(28.f);

    // Status box
    float stH=uiPx(64.f);
    Rectangle stR={cx,cy,cw,stH};
    DrawRectangleRec(stR,Color{12,18,12,220});
    DrawRectangleLinesEx(stR,1,Color{60,90,40,255});
    DrawText("Status:",(int)(cx+uiPx(12.f)),(int)(cy+uiPx(10.f)),12,C_SECONDARY);
    DrawText(g_netSession.status,(int)(cx+uiPx(12.f)),(int)(cy+uiPx(32.f)),14,C_PARCHMENT);
    cy+=stH+uiPx(20.f);

    // Player count (host view)
    if(g_netSession.role==NET_ROLE_HOST){
        char pbuf[64];
        snprintf(pbuf,sizeof pbuf,"Players: %d / %d",g_netSession.playerCount,NET_MAX_CLIENTS+1);
        DrawText(pbuf,(int)cx,(int)cy,13,C_PARCHMENT);
        cy+=uiPx(28.f);
    }

    float bh=uiPx(44.f);
    // PvP toggle (solo antes de iniciar sesion; decide el modo al hacer HOST)
    {
        bool pvpOn=g_campaign.pvpMode;
        bool canTog=(g_netSession.role==NET_ROLE_NONE);
        const char* pvpLbl=pvpOn?"PVP: TWO KINGDOMS (ON)":"PVP: TWO KINGDOMS (OFF)";
        Color pvpN=canTog?(pvpOn?Color{50,40,20,255}:Color{30,30,30,255}):Color{22,22,22,255};
        Color pvpH=canTog?(pvpOn?Color{90,70,30,255}:Color{50,50,50,255}):Color{22,22,22,255};
        if(drawButton({cx,cy,cw,bh*0.85f},pvpLbl,mouse,pvpN,pvpH)&&canTog){
            g_campaign.pvpMode=!g_campaign.pvpMode;
        }
        cy+=bh*0.85f+uiPx(10.f);
    }
    // HOST / STOP
    const char* hostLbl = (g_netSession.role==NET_ROLE_HOST)?"STOP HOSTING":"HOST GAME";
    Color hostN = (g_netSession.role==NET_ROLE_HOST)?Color{55,25,25,255}:Color{40,55,40,255};
    Color hostH = (g_netSession.role==NET_ROLE_HOST)?Color{90,40,40,255}:Color{70,110,60,255};
    if(drawButton({cx,cy,cw,bh},hostLbl,mouse,hostN,hostH)){
        if(g_netSession.role==NET_ROLE_HOST){
            netSessionStop();
        }else if(g_netSession.role==NET_ROLE_NONE){
            g_netSession.pvpMode=g_campaign.pvpMode; // PvP: asignar faccion al cliente al JOIN
            netSessionHostStart();
        }
    }
    cy+=bh+uiPx(14.f);

    // JOIN row: IP field + button
    static char s_ip[64]="127.0.0.1";
    float ipW=cw*0.62f;
    float jnW=cw-ipW-uiPx(10.f);
    Rectangle ipR={cx,cy,ipW,bh};
    bool ipHv=ptInRect(mouse,ipR);
    DrawRectangleRec(ipR,ipHv?Color{18,28,18,220}:Color{12,18,12,220});
    DrawRectangleLinesEx(ipR,1,Color{60,90,40,255});
    // text input (solo si no estamos en sesion)
    if(g_netSession.role==NET_ROLE_NONE){
        int key=GetCharPressed();
        while(key>0){
            if(key>=32&&key<=126){
                size_t len=strlen(s_ip);
                if(len<sizeof(s_ip)-1){ s_ip[len]=(char)key; s_ip[len+1]='\0'; }
            }
            key=GetCharPressed();
        }
        if(IsKeyPressed(KEY_BACKSPACE)){
            size_t len=strlen(s_ip);
            if(len>0) s_ip[len-1]='\0';
        }
    }
    DrawText(s_ip,(int)(cx+uiPx(10.f)),(int)(cy+bh*0.5f-uiPx(8.f)),14,C_PARCHMENT);
    bool joinEnabled=(g_netSession.role==NET_ROLE_NONE);
    if(drawButton({cx+ipW+uiPx(10.f),cy,jnW,bh},"JOIN",mouse,
                  joinEnabled?Color{40,55,40,255}:Color{30,30,30,255},
                  joinEnabled?Color{70,110,60,255}:Color{30,30,30,255})&&joinEnabled){
        netSessionClientJoin(s_ip);
    }
    cy+=bh+uiPx(24.f);

    // Hint
    const char* hint="Host listens on UDP port 7777. Join needs the host IP.";
    if(g_netSession.role==NET_ROLE_HOST)
        hint="Hosting. Start or continue a campaign; clients follow your state.";
    else if(g_netSession.role==NET_ROLE_CLIENT&&g_netSession.connected)
        hint="Connected. The host plays the campaign; you follow via snapshots.";
    int hw=MeasureText(hint,12);
    DrawText(hint,SCREEN_W/2-hw/2,(int)cy,12,C_SECONDARY);

    // BACK (abajo a la derecha, como Diplomacia)
    // Fase H: el cliente cierra su sesión; el HOST la conserva (sigue
    // escuchando para que los clientes entren a la campana co-op).
    if(drawButton({(float)(SCREEN_W-160),(float)(SCREEN_H-44),140,36},"BACK",mouse)){
        if(g_netSession.role!=NET_ROLE_HOST) netSessionStop();
        return STATE_MAIN_MENU;
    }
    return STATE_MULTIPLAYER;
}

// ═══════════════════════════════════════════════════════════════════════════
//  MAIN
// ═══════════════════════════════════════════════════════════════════════════
int main(int argc, char** argv){
    // Fase E/F/G/H: -nettest ejecuta los self-tests de red (headless) y sale
    // con 0/1 (capa cruda + sesión + lobby + sincronización de campaña)
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"-nettest")==0){
            int rc=runNetTest();
            int rc2=runSessionTest();
            int rc3=runLobbyTest();
            int rc4=runSyncTest(); // Fase H
            return (rc==0&&rc2==0&&rc3==0&&rc4==0)?0:1;
        }
    }
    // Load persistent settings before creating the window (so resolution applies on startup)
    loadSettings();                  // 3.1: load persistent settings
    SCREEN_W = g_settings.screenW;
    SCREEN_H = g_settings.screenH;

    InitWindow(SCREEN_W,SCREEN_H,"Medieval Conquest");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    if(g_settings.fullscreen && !IsWindowFullscreen()) ToggleFullscreen();
    initAudio();
    atmosInit();

    srand((unsigned)time(nullptr)); // 1.2: seed for any remaining rand() calls
    g_battleLog.clear();

    initBuiltinTypes();

    // Check for any campaign save
    g_hasSave=false;
    for(int i=0;i<MAX_CAMPAIGNS;i++){
        FILE* tf=fopen(getCampaignSavePath(i),"rb");
        if(tf){ g_hasSave=true; fclose(tf); break; }
    }

    g_state=STATE_MAIN_MENU;

    // Quick battle defaults
    g_quickSetup.playerCounts.assign(unitTypeCount(),0);
    g_quickSetup.enemyCounts.assign(unitTypeCount(),0);
    if(unitTypeCount()>0){ g_quickSetup.playerCounts[0]=2; g_quickSetup.enemyCounts[0]=2; }

    // Pre-battle defaults
    generateCampaignMap(0);

    while(!WindowShouldClose()&&!g_quitRequested){
        // Fullscreen toggle
        if(IsKeyPressed(KEY_F11)||(IsKeyDown(KEY_LEFT_ALT)&&IsKeyPressed(KEY_ENTER)))
            ToggleFullscreen();
        if(IsKeyPressed(KEY_F10)) g_profOverlay=!g_profOverlay;

        SCREEN_W=GetScreenWidth();
        SCREEN_H=GetScreenHeight();
        g_uiScaleDraw = g_settings.uiScale;

        float dt=GetFrameTime();
        if(dt>0.1f) dt=0.1f; // cap delta time
        g_menuTime+=dt;

        Vector2 mouse=GetMousePosition();
        updateAudio(dt);
        netSyncPump(); // Fase H: drena comandos + retransmite snapshot (1×/frame)

        profFrameBegin();
        double tFrame=GetTime();
        BeginDrawing();
        switch(g_state){
            case STATE_MAIN_MENU:
                g_state=updateDrawMainMenu(mouse);
                break;
            case STATE_CAMPAIGN_MAP:{
                double tMap=GetTime();
                g_state=updateDrawCampaignMap(mouse,dt);
                profAdd(PROF_MAP,(GetTime()-tMap)*1000.0);
            }   break;
            case STATE_CITY_MANAGEMENT:
                g_state=updateDrawCityManagement(mouse);
                break;
            case STATE_RECRUITMENT:
                g_state=updateDrawRecruitment(mouse);
                break;
            case STATE_PRE_BATTLE:
                g_state=updateDrawPreBattle(mouse);
                break;
            case STATE_BATTLE:
                g_state=updateDrawBattle(mouse,dt);
                break;
            case STATE_BATTLE_RESULT:
                g_state=updateDrawBattleResult(mouse);
                break;
            case STATE_UNIT_CODEX:
                g_state=updateDrawUnitCodex(mouse,dt);
                break;
            case STATE_SETTINGS:
                g_state=updateDrawSettings(mouse);
                break;
            case STATE_UNIT_EDITOR:
                g_state=updateDrawUnitEditor(mouse,dt);
                break;
            case STATE_QUICK_BATTLE_SETUP:
                g_state=updateDrawQuickBattleSetup(mouse);
                break;
            case STATE_VICTORY:
                g_state=updateDrawVictory(mouse);
                break;
            case STATE_DEFEAT:
                g_state=updateDrawDefeat(mouse);
                break;
            case STATE_MARKETPLACE:
                g_state=updateDrawMarketplace(mouse);
                break;
            case STATE_DIPLOMACY:
                g_state=updateDrawDiplomacy(mouse);
                break;
            case STATE_MULTIPLAYER:
                g_state=updateDrawMultiplayer(mouse);
                break;
        }
        // 4.9: Global FPS (shown in non-battle states too)
        if(g_settings.showFPS&&g_state!=STATE_BATTLE) DrawFPS(8,8);
        if(g_profOverlay) profDrawOverlay();

        // 4.10: Custom medieval cursor (cross-hair style)
        HideCursor();
        Vector2 cur=GetMousePosition();
        Color curCol=C_GOLD;
        if(g_state==STATE_BATTLE||g_state==STATE_PRE_BATTLE) curCol=C_ALLY;
        else if(g_state==STATE_CAMPAIGN_MAP) curCol={80,220,80,255};
        int csz=6;
        DrawLine((int)cur.x-csz,(int)cur.y,(int)cur.x+csz,(int)cur.y,curCol);
        DrawLine((int)cur.x,(int)cur.y-csz,(int)cur.x,(int)cur.y+csz,curCol);
        DrawRectangleLines((int)cur.x-2,(int)cur.y-2,4,4,{curCol.r,curCol.g,curCol.b,180});

        EndDrawing();
        profAdd(PROF_FRAME,(GetTime()-tFrame)*1000.0);
        profFrameEnd();

        // 4.10: Custom medieval cursor drawn after EndDrawing is handled by Raylib's software cursor
    }

    ShowCursor();  // 4.10: restore cursor on exit

    // Cleanup textures
    for(auto& t:g_playerTextures) UnloadTexture(t);
    for(auto& t:g_enemyTextures) UnloadTexture(t);
    unloadUnitSheets();

    // Auto-save on exit if campaign active (any turn, not just >1)
    if(!g_campaign.provinces.empty()) saveGame();

    netSessionStop();   // Fase G: cierra socket de red si quedaba abierto
    shutdownAudio();
    atmosShutdown();
    CloseWindow();
    return 0;
}
