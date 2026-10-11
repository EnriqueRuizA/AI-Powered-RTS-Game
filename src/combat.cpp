#include "combat.h"
#include "config.h"
#include "util.h"
#include "ui.h"
#include "audio.h"
#include "fx.h"
#include "atmos.h"
#include "sprite.h"
#include "terrain.h"
#include "camera.h"
#include "campaign.h"
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

BattleState  g_battle;      // (:645)
BattleResult g_lastResult;  // (:661)

std::deque<std::string> g_battleLog;
void battleLogAdd(const std::string& s){
    g_battleLog.push_front(s);
    if((int)g_battleLog.size()>50) g_battleLog.pop_back();
}

// ═══════════════════════════════════════════════════════════════════════════
//  FASE D: AUTO-RESOLVE CON PREDICCIÓN (estilo RISK)
// ═══════════════════════════════════════════════════════════════════════════

// Poder de combate de un ejército: Σ cnt·(atk + def + hp·0.1)
static float armyPower(const std::vector<std::pair<int,int>>& groups){
    float p=0.f;
    for(const auto& g:groups){
        int ti=g.first, cnt=g.second;
        if(ti<0||ti>=unitTypeCount()||cnt<=0) continue;
        const UnitTypeDef& td=g_unitTypes[ti];
        p+=(float)cnt*(td.meleeAttack+td.meleeDefense+td.hpPerSoldier*0.1f);
    }
    return p;
}

// Bonus defensivo del terreno: Montaña +25%, Bosque +15%, Costa +10%
static float terrainDefBonus(TerrainType t){
    if(t==TERRAIN_MOUNTAIN) return 1.25f;
    if(t==TERRAIN_FOREST)   return 1.15f;
    if(t==TERRAIN_COAST)    return 1.10f;
    return 1.f;
}

// Probabilidad de victoria del jugador [0.05..0.95]
float predictBattle(const std::vector<std::pair<int,int>>& playerGroups,
                    const std::vector<std::pair<int,int>>& enemyGroups,
                    TerrainType terrain,bool playerDefends,float defBonus){
    float pw=armyPower(playerGroups);
    float ew=armyPower(enemyGroups);
    if(defBonus<1.f) defBonus=1.f;
    float dm=terrainDefBonus(terrain)*defBonus;   // solo el defensor lo recibe
    if(playerDefends) pw*=dm; else ew*=dm;
    float total=pw+ew;
    if(total<=0.f) return 0.5f;
    float chance=pw/total;
    if(chance<0.05f) chance=0.05f;
    if(chance>0.95f) chance=0.95f;
    return chance;
}

// Resuelve la batalla al instante y rellena g_lastResult (autoresolved=true)
void autoResolveBattle(const std::vector<std::pair<int,int>>& playerGroups,
                       const std::vector<std::pair<int,int>>& enemyGroups,
                       TerrainType terrain,int provinceIdx,bool isDefense,
                       float defBonus){
    float pw=armyPower(playerGroups);
    float ew=armyPower(enemyGroups);
    if(defBonus<1.f) defBonus=1.f;
    float dm=terrainDefBonus(terrain)*defBonus;
    if(isDefense) pw*=dm; else ew*=dm;
    float total=pw+ew;
    float playerShare=total>0.f?pw/total:0.5f;
    float chance=predictBattle(playerGroups,enemyGroups,terrain,isDefense,defBonus);
    bool win=frandMT()<chance;

    // Pérdidas: el ganador sufre menos; la intensidad sigue al bando más fuerte
    float pFrac,eFrac;
    if(win){
        pFrac=0.12f+0.45f*(1.f-playerShare)+frandMT()*0.10f;
        eFrac =0.55f+0.35f*playerShare    +frandMT()*0.15f;
    } else {
        pFrac=0.55f+0.35f*(1.f-playerShare)+frandMT()*0.15f;
        eFrac =0.12f+0.45f*playerShare    +frandMT()*0.10f;
    }
    auto clampFrac=[](float v){ return v<0.02f?0.02f:(v>0.97f?0.97f:v); };
    pFrac=clampFrac(pFrac); eFrac=clampFrac(eFrac);

    BattleResult& r=g_lastResult;
    r.playerWon=win;
    r.playerLosses=0; r.enemyLosses=0;
    r.survivors.clear();
    int totalAlive=0;
    for(const auto& g:playerGroups){
        int ti=g.first, cnt=g.second;
        if(ti<0||ti>=unitTypeCount()||cnt<=0) continue;
        float f=clampFrac(pFrac+(frandMT()-0.5f)*0.10f);
        int alive=cnt-(int)((float)cnt*f+0.5f);
        if(alive<0) alive=0;
        if(alive>cnt) alive=cnt;
        r.playerLosses+=cnt-alive;
        r.survivors.push_back({ti,alive});
        totalAlive+=alive;
    }
    // El vencedor conserva al menos un soldado (evita ejército vacío)
    if(win&&totalAlive<=0&&!r.survivors.empty()){
        r.survivors.front().second=1;
        if(r.playerLosses>0) r.playerLosses--;
    }
    for(const auto& g:enemyGroups){
        int ti=g.first, cnt=g.second;
        if(ti<0||ti>=unitTypeCount()||cnt<=0) continue;
        float f=clampFrac(eFrac+(frandMT()-0.5f)*0.10f);
        int dead=(int)((float)cnt*f+0.5f);
        if(dead<0) dead=0;
        if(dead>cnt) dead=cnt;
        r.enemyLosses+=dead;
    }
    r.lootGold=(win&&!isDefense)?(50.f+frandMT()*100.f):0.f;
    r.provinceIdx=provinceIdx;
    r.isDefense=isDefense;
    r.lootApplied=false;
    r.autoresolved=true;
    battleLogAdd(win?"[Auto] Resolved: VICTORY":"[Auto] Resolved: DEFEAT");
}

//  BATTLE INIT
// ═══════════════════════════════════════════════════════════════════════════
void initBattle(const std::vector<std::pair<int,int>>& playerGroups,
                        const std::vector<std::pair<int,int>>& enemyGroups,
                        TerrainType terrain,int provinceIdx,bool isDefense,
                        const char* scenarioName,int playerVet,int enemyVet){
    g_battle={};
    fxClear();
    atmosReset();
    g_battle.terrain=terrain;
    g_battle.battleProvince=provinceIdx;
    g_battle.isDefense=isDefense;
    g_battle.timeScale=1.f;
    g_battle.activeControlGroup=-1;
    strncpy(g_battle.scenarioName,scenarioName,sizeof(g_battle.scenarioName)-1); g_battle.scenarioName[sizeof(g_battle.scenarioName)-1]='\0';

    // Camera centered on player deployment zone (extraído a camera.cpp)
    initBattleCamera();

    // v11: fase de preparación con formaciones (si está activa en ajustes)
    g_battle.prepPhase=g_settings.usePrepPhase;
    g_battle.prepTimer=(float)g_settings.prepTimeSec;
    g_battle.prepFormation=0; // línea por defecto
    // D8: la formación de ESTA batalla copia los defaults del usuario; el
    // selector pre-batalla muta solo estas globales (g_settings intacto)
    g_formPerRow=g_settings.formationPerRow;
    g_formSpacing=g_settings.formationSpacing;

    // Generate terrain obstacles (extraído a terrain.cpp; v11: incluye ciudad si la hay)
    bool provHasCity=(provinceIdx>=0&&provinceIdx<(int)g_campaign.provinces.size()&&
                      g_campaign.provinces[provinceIdx].hasCity);
    generateTerrainObstacles(terrain, provinceIdx, g_battle.obstacles, provHasCity);

    // Player units — left side
    float py=BATTLE_H/2.f;
    float px=BATTLE_W*0.2f;
    // Separacion entre grupos = ancho maximo de su formacion (antes paso fijo
    // de 90px con formaciones de 104px de ancho: los slots de grupos vecinos
    // se solapaban y los soldados se fundian en el despliegue).
    int maxCols=1;
    for(const auto& pg:playerGroups){
        if(pg.first>=unitTypeCount()||pg.second<=0) continue;
        int n=std::min(pg.second,(int)g_unitTypes[pg.first].soldierCount);
        int cols=1,rows=1;
        formationDims(n,&cols,&rows);
        if(cols>maxCols) maxCols=cols;
    }
    float groupSpan=(float)(maxCols-1)*g_formSpacing+36.f;
    for(int gi=0;gi<(int)playerGroups.size();gi++){
        auto [typeIdx,cnt]=playerGroups[gi];
        if(typeIdx>=unitTypeCount()||cnt<=0) continue;
        const UnitTypeDef& td=g_unitTypes[typeIdx];
        BattleUnit bu{};
        bu.typeIdx=typeIdx; bu.isPlayer=true;
        bu.anchorPos={px, py+(gi-playerGroups.size()/2.f)*groupSpan};
        bu.orderTarget=bu.anchorPos;
        bu.orderAttack=-1;
        bu.morale=td.morale;
        bu.groupState=UGS_IDLE;
        bu.veterancy=playerVet; // D2: Range/Stable de la ciudad (0..3)
        bu.aiReactTimer=0.f;
        bu.orderType=0; // 6.6: attack order type
        bu.formationFacing=0.f; // 6.6: facing angle (0 = right)
        bu.lastFormationFacing=0.f; // v7.2: initialize facing tracker
        int soldierCount=std::min(cnt,(int)td.soldierCount);
        auto slots=calcFormationSlots(bu.anchorPos,soldierCount,0.f);
        for(int s=0;s<soldierCount;s++){
            Soldier sol{};
            sol.pos=slots[s];
            sol.formationSlot=slots[s];
            sol.hp=td.hpPerSoldier;
            sol.alive=true;
            sol.angle=0; sol.angleTarget=0;
            sol.attackTargetSoldier=-1; sol.attackTargetUnit=-1;
            sol.state=SS_IDLE;
            sol.chargeReady=(td.spriteBase==SPR_CAVALRY);
            sol.targetRefreshTimer=0.f;
            sol.kills=0;
            bu.soldiers.push_back(sol);
        }
        g_battle.playerUnits.push_back(bu);
    }

    // Enemy units — right side
    float ey=BATTLE_H/2.f;
    float ex2=BATTLE_W*0.8f;
    int maxColsE=1;
    for(const auto& pg:enemyGroups){
        if(pg.first>=unitTypeCount()||pg.second<=0) continue;
        int n=std::min(pg.second,(int)g_unitTypes[pg.first].soldierCount);
        int cols=1,rows=1;
        formationDims(n,&cols,&rows);
        if(cols>maxColsE) maxColsE=cols;
    }
    float groupSpanE=(float)(maxColsE-1)*g_formSpacing+36.f;
    for(int gi=0;gi<(int)enemyGroups.size();gi++){
        auto [typeIdx,cnt]=enemyGroups[gi];
        if(typeIdx>=unitTypeCount()||cnt<=0) continue;
        const UnitTypeDef& td=g_unitTypes[typeIdx];
        BattleUnit bu{};
        bu.typeIdx=typeIdx; bu.isPlayer=false;
        bu.anchorPos={ex2, ey+(gi-(int)enemyGroups.size()/2.f)*groupSpanE};
        bu.orderTarget=bu.anchorPos;
        bu.orderAttack=-1;
        bu.morale=td.morale;
        bu.groupState=UGS_IDLE;
        bu.veterancy=enemyVet; // D2: guarnición con Range/Stable (0..3)
        bu.aiReactTimer=0.f;
        bu.orderType=0; // 6.6: attack order type
        bu.formationFacing=180.f; // 6.6: facing angle (180 = left for enemies)
        bu.lastFormationFacing=180.f; // v7.2: initialize facing tracker
        int soldierCount=std::min(cnt,(int)td.soldierCount);
        auto slots=calcFormationSlots(bu.anchorPos,soldierCount,180.f);
        for(int s=0;s<soldierCount;s++){
            Soldier sol{};
            sol.pos=slots[s];
            sol.formationSlot=slots[s];
            sol.hp=td.hpPerSoldier;
            sol.alive=true;
            sol.angle=180; sol.angleTarget=180;
            sol.attackTargetSoldier=-1; sol.attackTargetUnit=-1;
            sol.state=SS_IDLE;
            sol.chargeReady=(td.spriteBase==SPR_CAVALRY);
            sol.targetRefreshTimer=0.f;
            sol.kills=0;
            bu.soldiers.push_back(sol);
        }
        g_battle.enemyUnits.push_back(bu);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  COMBAT HELPERS
// ═══════════════════════════════════════════════════════════════════════════
float soldierMeleeRadius(const UnitTypeDef& td){
    return td.spriteBase==SPR_CAVALRY?16.f:10.f;
}

float calcMeleeDmg(const UnitTypeDef& attTd,const UnitTypeDef& defTd,bool chargeBonus){
    float hitChance=std::max(8.f,std::min(92.f,30.f+(float)attTd.meleeAttack-(float)defTd.meleeDefense));
    if(frandMT()*100.f>=hitChance) return 0.f;
    float armRed=frandMT()*0.5f*(float)defTd.armor+(float)defTd.armor*0.5f;
    armRed=std::min(armRed,(float)defTd.armor);
    float dmg=std::max(0.5f,attTd.meleeBaseDmg*(1.f-armRed/100.f)+attTd.meleeAPDmg);
    if(chargeBonus) dmg*=2.5f;
    return dmg;
}

float calcMissileDmg(const UnitTypeDef& attTd,const UnitTypeDef& defTd){
    float armRed=frandMT()*0.5f*(float)defTd.armor+(float)defTd.armor*0.5f;
    armRed=std::min(armRed,(float)defTd.armor);
    return std::max(0.f,attTd.missileBaseDmg*(1.f-armRed/100.f)+attTd.missileAPDmg);
}

// 6.6: Improved obstacle avoidance with steering behavior
Vector2 avoidObstacles(Vector2 pos, Vector2 targetDir, float speed, const std::vector<Rectangle>& obstacles){
    Vector2 ahead=v2add(pos,v2scale(targetDir,speed*0.5f));
    Vector2 ahead2=v2add(pos,v2scale(targetDir,speed*0.25f));

    // Check if we're hitting an obstacle
    Rectangle checkBox={ahead.x-8,ahead.y-8,16,16};
    Rectangle checkBox2={ahead2.x-8,ahead2.y-8,16,16};

    for(auto& obs:obstacles){
        if(CheckCollisionRecs(checkBox,obs)||CheckCollisionRecs(checkBox2,obs)){
            // Try to steer around obstacle
            Vector2 obsCenter={obs.x+obs.width/2,obs.y+obs.height/2};
            Vector2 toObs=vnorm(v2sub(obsCenter,pos));
            Vector2 perp={-toObs.y,toObs.x};
            // Steer perpendicular to obstacle
            return vnorm(v2add(targetDir,v2scale(perp,0.7f)));
        }
    }
    return targetDir;
}

// LOS check for ranged: returns true if line is clear enough to shoot
bool losClean(Vector2 from,Vector2 to,const std::vector<Soldier>& friendlies,int selfIdx){
    Vector2 dir=vnorm(v2sub(to,from));
    float dist=vdist(from,to);
    int blocked=0;
    for(int i=0;i<(int)friendlies.size();i++){
        if(i==selfIdx||!friendlies[i].alive) continue;
        Vector2 rel=v2sub(friendlies[i].pos,from);
        float proj=v2dot(rel,dir);
        if(proj<0||proj>dist) continue;
        float perp=fabsf(rel.x*(-dir.y)+rel.y*dir.x);
        if(perp<10.f){ blocked++; if(blocked>=2) return false; }
    }
    // Check terrain obstacles
    for(auto& obs:g_battle.obstacles){
        // Simple: check if line intersects rectangle
        // Rough check: if rect centroid is within 30px of line
        Vector2 rc={obs.x+obs.width/2,obs.y+obs.height/2};
        Vector2 rel=v2sub(rc,from);
        float proj=v2dot(rel,dir);
        if(proj>0&&proj<dist){
            float perp=fabsf(rel.x*(-dir.y)+rel.y*dir.x);
            if(perp<(obs.width+obs.height)*0.25f) return false;
        }
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  BATTLE UPDATE
// ═══════════════════════════════════════════════════════════════════════════
// Rejilla espacial de baldosas (Fase A) para vecindad en O(1) por soldado.
// Baldosas + membresia incremental: al empujar un par, si un soldado cruza
// de celda se recoloca al momento (swap-with-last), asi que el membership
// NUNCA queda obsoleto dentro de una pasada — con CSR estatico los pares
// formados por empujadas se escapaban al query (same caia a 17.6px en
// mega-clash). cellSize >= minD garantiza vecinos <minD en celdas 3x3.
// Los queries iteran una COPIA (scratch) de las 9 baldosas para poder
// empujar+recolocar sin invalidar el iterador.
struct SepGrid {
    int gw=0,gh=0;
    float cell=26.f,invCell=1.f/26.f;
    std::vector<std::vector<int>> buckets; // indices de items por celda
    std::vector<int> slotOf;               // posicion dentro de su bucket
    std::vector<int> cellItem;             // celda actual de cada item
    std::vector<int> scratch;              // copia 3x3 para iterar seguro
    void build(const std::vector<Soldier*>& all,float cellSize){
        cell=cellSize; invCell=1.f/cellSize;
        gw=(int)((float)BATTLE_W/cellSize)+2;
        gh=(int)((float)BATTLE_H/cellSize)+2;
        buckets.assign((size_t)gw*gh,{});
        slotOf.resize(all.size());
        cellItem.resize(all.size());
        for(size_t i=0;i<all.size();i++){
            int c=cellOf(all[i]->pos);
            cellItem[i]=c;
            slotOf[i]=(int)buckets[c].size();
            buckets[c].push_back((int)i);
        }
    }
    inline int cx(float x)const{int c=(int)(x*invCell);return c<0?0:(c>=gw?gw-1:c);}
    inline int cy(float y)const{int c=(int)(y*invCell);return c<0?0:(c>=gh?gh-1:c);}
    inline int cellOf(Vector2 p)const{return cy(p.y)*gw+cx(p.x);}
    // Recoloca el item i si su soldado cruzo de celda (llamar tras empujar)
    inline void touch(int i,Soldier* s){
        int c1=cellOf(s->pos);
        if(c1==cellItem[i]) return;
        int c0=cellItem[i];
        auto& b0=buckets[c0];
        int slot=slotOf[i];
        int last=b0.back();
        b0[slot]=last; slotOf[last]=slot; b0.pop_back();
        auto& b1=buckets[c1];
        slotOf[i]=(int)b1.size();
        b1.push_back(i);
        cellItem[i]=c1;
    }
    // Copia a scratch los items de las 9 baldosas alrededor de (ccx,ccy)
    inline void collect(int ccx,int ccy){
        scratch.clear();
        for(int gy=ccy-1;gy<=ccy+1;gy++){
            if(gy<0||gy>=gh) continue;
            for(int gx2=ccx-1;gx2<=ccx+1;gx2++){
                if(gx2<0||gx2>=gw) continue;
                const auto& b=buckets[gy*gw+gx2];
                scratch.insert(scratch.end(),b.begin(),b.end());
            }
        }
    }
};
static SepGrid g_sepGrid;

bool separateSoldiers(std::vector<BattleUnit>& units){
    // Separation between ALL friendly soldiers, crossing groups (v9: antes
    // solo separaba dentro de cada grupo, asi que soldados de grupos
    // distintos del mismo bando podian fundirse en un mismo punto).
    // minD = espaciado de formacion del jugador (Settings -> formationSpacing).
    // V10: 4 pasadas con push 0.8 hasta converger; Fase A: vecindad via
    // rejilla espacial (antes O(n^2) completo: 88ms a 1920 soldados).
    // Fase A.2: hasta 8 pasadas — con ejercitos multi-grupo densos (16x60)
    // 4 dejaban pares a 19.5px; el early-break hace gratis lo que no necesita.
    float minD=g_formSpacing;
    std::vector<Soldier*> all;
    for(auto& bu:units)
        for(auto& s:bu.soldiers)
            if(s.alive) all.push_back(&s);
    if(all.empty()) return false;
    bool any=false;
    const float minD2=minD*minD;
    for(int pass=0;pass<8;pass++){
        g_sepGrid.build(all,minD); // cell=minD: vecinos <minD siempre en 3x3
        bool moved=false;
        for(int i=0;i<(int)all.size();i++){
            Soldier* a=all[i];
            g_sepGrid.collect(g_sepGrid.cx(a->pos.x),g_sepGrid.cy(a->pos.y));
            for(int k=0;k<(int)g_sepGrid.scratch.size();k++){
                int j=g_sepGrid.scratch[k];
                if(j<=i) continue; // par unico (i<j)
                Soldier* b=all[j];
                float dx=b->pos.x-a->pos.x, dy=b->pos.y-a->pos.y;
                float d2=dx*dx+dy*dy;
                if(d2<minD2&&d2>1e-6f){
                    float d=sqrtf(d2);
                    float push=(minD-d)*0.8f;
                    float nx=dx/d*push, ny=dy/d*push;
                    a->pos.x-=nx; a->pos.y-=ny;
                    b->pos.x+=nx; b->pos.y+=ny;
                    g_sepGrid.touch(i,a);
                    g_sepGrid.touch(j,b);
                    moved=true; any=true;
                }
            }
        }
        if(!moved) break;
    }
    for(auto* s:all){
        s->pos.x=std::max(8.f,std::min((float)BATTLE_W-8,s->pos.x));
        s->pos.y=std::max(8.f,std::min((float)BATTLE_H-8,s->pos.y));
    }
    return any;
}

bool separateContact(std::vector<BattleUnit>& a,std::vector<BattleUnit>& b){
    // Contacto entre bandos: distancia menor para que las lineas se toquen
    // sin fundirse (antes no habia ninguna: los cuerpos podian apilarse).
    // Fase A: rejilla combinada (ambos bandos), pares solo a->b.
    const float minD=14.f;
    const float minD2=minD*minD;
    static std::vector<Soldier*> all;
    static std::vector<char> team; // 0=a 1=b
    all.clear(); team.clear();
    for(auto& bu:a) for(auto& s:bu.soldiers) if(s.alive){ all.push_back(&s); team.push_back(0); }
    for(auto& bu:b) for(auto& s:bu.soldiers) if(s.alive){ all.push_back(&s); team.push_back(1); }
    if(all.empty()) return false;
    bool any=false;
    for(int pass=0;pass<4;pass++){ // Fase A.2: 4 pasadas (era 2) para cierres multi-grupo
        g_sepGrid.build(all,minD);
        for(int i=0;i<(int)all.size();i++){
            if(team[i]) continue; // solo fuente a (cada par a-b una vez)
            Soldier* sa=all[i];
            g_sepGrid.collect(g_sepGrid.cx(sa->pos.x),g_sepGrid.cy(sa->pos.y));
            for(int k=0;k<(int)g_sepGrid.scratch.size();k++){
                int j=g_sepGrid.scratch[k];
                if(!team[j]) continue; // solo b
                Soldier* sb=all[j];
                float dx=sb->pos.x-sa->pos.x, dy=sb->pos.y-sa->pos.y;
                float d2=dx*dx+dy*dy;
                if(d2<minD2&&d2>1e-6f){
                    float d=sqrtf(d2);
                    float push=(minD-d)*0.8f;
                    float nx=dx/d*push, ny=dy/d*push;
                    sa->pos.x-=nx; sa->pos.y-=ny;
                    sb->pos.x+=nx; sb->pos.y+=ny;
                    g_sepGrid.touch(i,sa);
                    g_sepGrid.touch(j,sb);
                    any=true;
                }
            }
        }
    }
    return any;
}

void separateAll(){
    // Proyecciones alternadas: mismo-bando (26) y cruzado (14) se perturban
    // mutuamente; alternar hasta que ninguno tenga violaciones alcanza un
    // estado factible con ambas restricciones satisfechas.
    // Fase A.2: 16 ciclos (era 12) — ejercitos densos necesitan mas alternancias.
    for(int k=0;k<16;k++){
        bool m=separateSoldiers(g_battle.playerUnits);
        m=separateSoldiers(g_battle.enemyUnits)||m;
        m=separateContact(g_battle.playerUnits,g_battle.enemyUnits)||m;
        if(!m) break;
    }
}

// F12: overlay de depuracion - punto por soldado + linea de orientacion.
// Permite verificar programaticamente solapamiento (distancia entre puntos)
// y giros espurios (cambio de linea entre frames consecutivos).
bool g_debugSoldiers=false;
static void paintTeam(std::vector<BattleUnit>& us,Color c){
    for(auto& bu:us){
        for(auto& s:bu.soldiers){
            if(!s.alive) continue;
            DrawCircleSector(s.pos,3.f,0.f,360.f,8,c); // F12: 8 segs, no 360
            float rad=s.angle*DEG2RAD;
            Vector2 tip={s.pos.x+cosf(rad)*14.f,s.pos.y-sinf(rad)*14.f};
            DrawLineEx(s.pos,tip,2.f,c);
        }
    }
}
void drawSoldierDebug(){
    if(!g_debugSoldiers) return;
    paintTeam(g_battle.playerUnits,Color{0,255,255,255}); // cian = jugador
    paintTeam(g_battle.enemyUnits,Color{255,0,255,255});   // magenta = enemigo
    // Valor efectivo de spacing + marcas blancas en pares <18px (solapamiento)
    DrawText(TextFormat("spacing=%.0f",g_formSpacing),10,110,16,WHITE);
    std::vector<Vector2> ps;
    std::vector<char> tm; // 0=jugador 1=enemigo
    for(auto& bu:g_battle.playerUnits) for(auto& s:bu.soldiers) if(s.alive){ ps.push_back(s.pos); tm.push_back(0); }
    for(auto& bu:g_battle.enemyUnits)  for(auto& s:bu.soldiers) if(s.alive){ ps.push_back(s.pos); tm.push_back(1); }
    float ms=1e9f,mc=1e9f; Vector2 msA={0,0},msB={0,0};
    for(int i=0;i<(int)ps.size();i++){
        for(int j=i+1;j<(int)ps.size();j++){
            float d=vdist(ps[i],ps[j]);
            if(tm[i]==tm[j]){ if(d<ms){ms=d;msA=ps[i];msB=ps[j];} }
            else if(d<mc) mc=d;
            // marcador blanco: mismo bando <20 o cruzado <12 (solapamiento real)
            bool bad=(tm[i]==tm[j])?d<20.f:d<12.f;
            if(bad&&d>0.001f){
                DrawCircleV({(ps[i].x+ps[j].x)*0.5f,(ps[i].y+ps[j].y)*0.5f},7,WHITE);
            }
        }
    }
    FILE* fp=fopen("build/dist_log.txt","a");
    if(fp){
        // maximo giro por frame entre frames consecutivos (spin de peonza)
        static std::vector<float> prevAng;
        std::vector<float> cur; cur.reserve(ps.size());
        std::vector<unsigned char> stv; stv.reserve(ps.size());
        for(auto& bu:g_battle.playerUnits) for(auto& s:bu.soldiers) if(s.alive){ cur.push_back(s.angle); stv.push_back((unsigned char)s.state); }
        for(auto& bu:g_battle.enemyUnits)  for(auto& s:bu.soldiers) if(s.alive){ cur.push_back(s.angle); stv.push_back((unsigned char)s.state); }
        float maxd=0.f; int big=0; int bst[8]={0,0,0,0,0,0,0,0};
        if(prevAng.size()==cur.size()){
            for(int i=0;i<(int)cur.size();i++){
                float d=fmodf(fabsf(cur[i]-prevAng[i]),360.f);
                if(d>180.f) d=360.f-d;
                if(d>maxd) maxd=d;
                if(d>15.f){ big++; if(stv[i]<8) bst[stv[i]]++; }
            }
        }
        prevAng=cur;
        fprintf(fp,"sp=%.0f row=%d zoom=%.2f same=%.1f cross=%.1f n=%d maxdA=%.1f big=%d st=%d,%d,%d,%d,%d,%d a=(%.0f,%.0f) b=(%.0f,%.0f)\n",
                    g_formSpacing,g_formPerRow,g_battle.cam.zoom,
                    ms,mc,(int)ps.size(),maxd,big,bst[0],bst[1],bst[2],bst[3],bst[4],bst[5],msA.x,msA.y,msB.x,msB.y);
        fclose(fp);
    }
}

// Find nearest alive soldier in an enemy BattleUnit list, returns {unitIdx, solIdx} or {-1,-1}
std::pair<int,int> findNearestEnemySoldier(Vector2 from,
        const std::vector<BattleUnit>& enemies){
    float best=1e9f; int bu2=-1,si=-1;
    for(int u=0;u<(int)enemies.size();u++){
        for(int s=0;s<(int)enemies[u].soldiers.size();s++){
            if(!enemies[u].soldiers[s].alive) continue;
            float d=vdist(from,enemies[u].soldiers[s].pos);
            if(d<best){best=d;bu2=u;si=s;}
        }
    }
    return {bu2,si};
}

void updateBattleUnits(std::vector<BattleUnit>& myUnits,
                               std::vector<BattleUnit>& foeUnits,
                               float dt){
    // Clamp del paso de movimiento: en frames largos (capturas de pantalla,
    // hitches) un soldado podia moverse mas de un cuadro por frame y superar
    // el empuje de separateSoldiers, colapsando el cuerpo a cuerpo.
    const float mdt=std::min(dt,0.05f);
    for(int ui=0;ui<(int)myUnits.size();ui++){
        BattleUnit& bu=myUnits[ui];
        if(bu.typeIdx>=unitTypeCount()) continue;
        const UnitTypeDef& td=g_unitTypes[bu.typeIdx];

        int aliveCnt=0;
        for(auto& sol:bu.soldiers) if(sol.alive) aliveCnt++;
        if(aliveCnt==0) continue;

        // Morale update
        float totalSol=(float)td.soldierCount;
        if((float)aliveCnt/totalSol < 0.4f){
            bu.morale-=1.f*dt;
        }
        bu.moraleTimer+=dt;
        if(bu.moraleTimer>4.f&&bu.morale<td.morale){
            bu.morale=std::min(td.morale,bu.morale+0.5f*60.f*dt);
        }
        bu.morale=std::max(0.f,std::min(100.f,bu.morale));

        // Routing logic
        if(bu.groupState==UGS_ROUTING){
            bu.routeTimer-=dt;
            if(bu.routeTimer<=0){
                if(bu.morale<5.f){
                    // Permanently rout — kill all soldiers
                    for(auto& sol:bu.soldiers) sol.alive=false;
                } else {
                    bu.groupState=UGS_IDLE;
                }
            }
            // Soldiers flee
            // Compute enemy centroid
            Vector2 foeCenter={0,0}; int foeCount=0;
            for(auto& fu:foeUnits) for(auto& fs:fu.soldiers) if(fs.alive){foeCenter=v2add(foeCenter,fs.pos);foeCount++;}
            if(foeCount>0){foeCenter=v2scale(foeCenter,1.f/foeCount);}
            for(auto& sol:bu.soldiers){
                if(!sol.alive) continue;
                sol.state=SS_FLEEING;
                Vector2 away=vnorm(v2sub(sol.pos,foeCenter));
                sol.pos=v2add(sol.pos,v2scale(away,td.speed*1.5f*mdt));
                sol.angleTarget=dirToAngle(away);
                sol.angle=turnAngle(sol.angle,sol.angleTarget,300.f*mdt);
            }
            continue;
        }
        if(bu.morale<20.f&&bu.groupState!=UGS_ROUTING){
            bu.groupState=UGS_ROUTING;
            bu.routeTimer=6.f;
        }

        // ═══════════════════════════════════════════════════════════════════════════
        //  MOVEMENT SYSTEM v8.1: ANCHOR FOLLOWS CENTROID (STABLE)
        // ═══════════════════════════════════════════════════════════════════════════

        // Determine group order target (explicit attack or move)
        bool hasAttackOrder=(bu.orderAttack>=0&&bu.orderAttack<(int)foeUnits.size());
        bool hasExplicitMoveOrder=(bu.hasExplicitOrder&&bu.orderType==1);

        // Calculate desired movement (toward order target or auto-advance to enemies)
        Vector2 desiredAnchor=bu.anchorPos;
        // ranged hold/retreat: anula el centroid-pull (si no, el tir de
        // 0.1xoffset POSICIONAL por frame domina los pasos speed*dt y el
        // ancla termina pegada al centroid enemigo en melee)
        bool anchorHold=false;

        if(hasExplicitMoveOrder){
            Vector2 moveDir=vnorm(v2sub(bu.orderTarget,bu.anchorPos));
            float distToTarget=vdist(bu.anchorPos,bu.orderTarget);

            if(distToTarget>15.f){
                desiredAnchor=v2add(bu.anchorPos,v2scale(moveDir,td.speed*mdt));
                bu.formationFacing=dirToAngle(moveDir);
            } else {
                // Arrived at target
                desiredAnchor=bu.orderTarget;
                bu.hasExplicitOrder=false;
            }
        } 
        else if(hasAttackOrder||(!hasExplicitMoveOrder&&!bu.hasExplicitOrder&&foeUnits.size()>0)){
            // Auto-advance toward nearest enemy when no explicit orders
            // FIX: find nearest enemy group, not always index 0
            int targetIdx=hasAttackOrder?bu.orderAttack:0;
            if(!hasAttackOrder){
                float bestDist=1e9f;
                for(int ei=0;ei<(int)foeUnits.size();ei++){
                    int eAlive=0; for(auto& es:foeUnits[ei].soldiers) if(es.alive) eAlive++;
                    if(eAlive==0) continue;
                    float d=vdist(bu.anchorPos,foeUnits[ei].anchorPos);
                    if(d<bestDist){bestDist=d;targetIdx=ei;}
                }
            }
            if(validIdx(targetIdx,foeUnits)){
                Vector2 toEnemy=vnorm(v2sub(foeUnits[targetIdx].anchorPos,bu.anchorPos));
                float distToEnemy=vdist(bu.anchorPos,foeUnits[targetIdx].anchorPos);

                int foeTi=(foeUnits[targetIdx].typeIdx>=0
                            &&foeUnits[targetIdx].typeIdx<unitTypeCount())
                           ?foeUnits[targetIdx].typeIdx:bu.typeIdx;

                if(td.range>0){
                    // Unidades ranged: SE QUEDAN A DISTANCIA. Avanzan solo
                    // hasta su alcance de tiro (85% + profundidad de las
                    // formaciones, baseGap) y ahi se QUEDAN SIN MOVERSE:
                    // no retroceden (kite infinito hasta el borde del
                    // mapa) ni cierran a cuerpo a cuerpo por iniciativa.
                    // Si el enemigo alcanza la linea, pelean en su sitio
                    // (melee de autodefensa, Priority 1).
                    int mrc=1,mrr=1,frc=1,frr=1;
                    formationDims((int)bu.soldiers.size(),&mrc,&mrr);
                    formationDims((int)foeUnits[targetIdx].soldiers.size(),&frc,&frr);
                    float sp=g_formSpacing;
                    float baseGap=(float)(mrr-1)*sp+(float)(frr-1)*sp
                        +soldierMeleeRadius(td)+soldierMeleeRadius(g_unitTypes[foeTi])+6.f;
                    float holdDist=baseGap+(float)td.range*0.85f;
                    if(distToEnemy>holdDist){
                        desiredAnchor=v2add(bu.anchorPos,v2scale(toEnemy,td.speed*mdt));
                    } else {
                        anchorHold=true;
                    }
                    bu.formationFacing=dirToAngle(toEnemy);
                } else {
                    // Melee: avanzar hasta que las lineas se encuentren a
                    // distancia de golpe SIN interpenetrarse (v9).
                    int mc=1,mr=1,fc=1,fr=1;
                    formationDims((int)bu.soldiers.size(),&mc,&mr);
                    formationDims((int)foeUnits[targetIdx].soldiers.size(),&fc,&fr);
                    float sp=g_formSpacing;
                    float engagementDistance=
                        (float)(mr-1)*sp+(float)(fr-1)*sp
                        +soldierMeleeRadius(td)+soldierMeleeRadius(g_unitTypes[foeTi])+6.f;
                    if(distToEnemy>engagementDistance){
                        desiredAnchor=v2add(bu.anchorPos,v2scale(toEnemy,td.speed*mdt));
                    }
                    bu.formationFacing=dirToAngle(toEnemy);
                }
            }
        }

        // FIX: ALWAYS recalculate slots using desiredAnchor (the intended new position).
        // The old guard (>5 degree facing change) caused slots to go stale during straight-line
        // movement: anchor advanced but soldiers chased old world positions → wrong direction / trembling.
        {
            bu.lastFormationFacing=bu.formationFacing;
            auto slots=calcFormationSlots(desiredAnchor,(int)bu.soldiers.size(),bu.formationFacing);
            for(int s=0;s<(int)bu.soldiers.size()&&s<(int)slots.size();s++){
                bu.soldiers[s].formationSlot=slots[s];
            }
        }

        // ═══════════════════════════════════════════════════════════════════════════
        //  INDIVIDUAL SOLDIER UPDATES (v8.0: SMOOTH GRAVITATIONAL PULL)
        // ═══════════════════════════════════════════════════════════════════════════

        // Calculate frontline threshold ONCE per unit (optimization + stability)
        float frontlineDistance=1e9f;
        for(auto& eneunit:foeUnits){
            for(auto& esol:eneunit.soldiers){
                if(!esol.alive) continue;
                for(auto& sol:bu.soldiers){
                    if(!sol.alive) continue;
                    float d=vdist(sol.pos,esol.pos);
                    frontlineDistance=std::min(frontlineDistance,d);
                }
            }
        }
        if(frontlineDistance==1e9f) frontlineDistance=200.f;
        float frontlineThreshold=frontlineDistance+50.f;

        // Each soldier updates individually
        for(int si=0;si<(int)bu.soldiers.size();si++){
            Soldier& sol=bu.soldiers[si];
            if(!sol.alive) continue;

            sol.meleeTimer-=dt;
            sol.rangeTimer-=dt;
            sol.inMelee=false;

            // Check if this soldier is in front line
            float distToClosestEnemy=1e9f;
            for(auto& eneunit:foeUnits){
                for(auto& esol:eneunit.soldiers){
                    if(!esol.alive) continue;
                    float d=vdist(sol.pos,esol.pos);
                    distToClosestEnemy=std::min(distToClosestEnemy,d);
                }
            }
            bool inFrontLine=(distToClosestEnemy<=frontlineThreshold);

            // Resolve attack target
            int targetUnit=sol.attackTargetUnit;
            int targetSol=sol.attackTargetSoldier;
            bool targetValid=(targetUnit>=0&&targetUnit<(int)foeUnits.size()
                              &&targetSol>=0&&targetSol<(int)foeUnits[targetUnit].soldiers.size()
                              &&foeUnits[targetUnit].soldiers[targetSol].alive);

            if(!targetValid){
                sol.targetRefreshTimer-=dt;
                bool needRefresh=(sol.targetRefreshTimer<=0.f);
                if(!needRefresh&&sol.attackTargetUnit>=0){
                    if(validIdx(sol.attackTargetUnit,foeUnits)&&
                       validIdx(sol.attackTargetSoldier,foeUnits[sol.attackTargetUnit].soldiers)&&
                       foeUnits[sol.attackTargetUnit].soldiers[sol.attackTargetSoldier].alive){
                        targetUnit=sol.attackTargetUnit;
                        targetSol=sol.attackTargetSoldier;
                        targetValid=true;
                    } else needRefresh=true;
                }
                if(needRefresh){
                    sol.targetRefreshTimer=0.3f;
                    if(hasAttackOrder){
                        float best=1e9f; int bs=-1;
                        for(int s2=0;s2<(int)foeUnits[bu.orderAttack].soldiers.size();s2++){
                            if(!foeUnits[bu.orderAttack].soldiers[s2].alive) continue;
                            float d=vdist(sol.pos,foeUnits[bu.orderAttack].soldiers[s2].pos);
                            if(d<best){best=d;bs=s2;}
                        }
                        if(bs>=0){targetUnit=bu.orderAttack;targetSol=bs;targetValid=true;}
                    }
                    if(!targetValid&&!bu.hasExplicitOrder){
                        auto [nu,ns]=findNearestEnemySoldier(sol.pos,foeUnits);
                        if(nu>=0){targetUnit=nu;targetSol=ns;targetValid=true;}
                    }
                    sol.attackTargetUnit=targetUnit;
                    sol.attackTargetSoldier=targetSol;
                }
            }

            // ═══════════════════════════════════════════════════════════════════════════
            //  SOLDIER MOVEMENT PRIORITY SYSTEM v8.0
            // ═══════════════════════════════════════════════════════════════════════════

            float slotDist=vdist(sol.pos,sol.formationSlot);
            const float SLOT_TOLERANCE=8.f; // When soldier is "in slot" (strict precision)

            // If no target, move to formation slot instead of idle
            if(!targetValid){
                sol.state=SS_MOVING_SLOT;

                if(slotDist>SLOT_TOLERANCE){
                    Vector2 toSlot=vnorm(v2sub(sol.formationSlot,sol.pos));
                    toSlot=avoidObstacles(sol.pos,toSlot,td.speed,g_battle.obstacles);
                    // v8.2: Fast movement - linear instead of deceleration curve
                    // Soldiers move at FULL speed toward their slot
                    sol.pos=v2add(sol.pos,v2scale(toSlot,td.speed*mdt));
                    sol.angleTarget=dirToAngle(toSlot);
                    sol.chargeMoveTime+=dt;
                    if(sol.chargeMoveTime>0.8f) sol.chargeReady=(td.spriteBase==SPR_CAVALRY);
                } else {
                    sol.state=SS_IDLE;
                    sol.chargeMoveTime=0.f;
                }
                sol.angle=turnAngle(sol.angle,sol.angleTarget,300.f*mdt);
                continue;  // Skip combat logic if no target
            }

            Soldier& tgt=foeUnits[targetUnit].soldiers[targetSol];
            const UnitTypeDef& fTd=g_unitTypes[foeUnits[targetUnit].typeIdx];
            float myRad=soldierMeleeRadius(td);
            float fRad=soldierMeleeRadius(fTd);
            float meleeD=myRad+fRad+6.f;
            float dist=vdist(sol.pos,tgt.pos);
            Vector2 dir2=vnorm(v2sub(tgt.pos,sol.pos));
            sol.angleTarget=dirToAngle(dir2);

            // Priority 1: MELEE COMBAT (front line only) - BREAK FORMATION
            if(inFrontLine&&dist<=meleeD){
                sol.inMelee=true;
                sol.state=SS_ATTACKING_MELEE;

                // Frente estable en cuerpo a cuerpo: cerrar a distancia de
                // golpe y aguantar la linea. Sin orbitacion (v9): antes cada
                // soldado giraba alrededor del objetivo a 2.5 rad/s y el sprite
                // lo seguia -> unidades girando como peonzas en medio del combate.
                if(dist<meleeD*0.7f){
                    Vector2 back=vnorm(v2sub(sol.pos,tgt.pos));
                    sol.pos=v2add(sol.pos,v2scale(back,td.speed*0.5f*mdt));
                }else if(dist>meleeD*0.95f){
                    sol.pos=v2add(sol.pos,v2scale(dir2,td.speed*0.5f*mdt));
                }

                if(sol.meleeTimer<=0){
                    bool chargeBonus=sol.chargeReady&&td.spriteBase==SPR_CAVALRY;
                    float dmg=calcMeleeDmg(td,fTd,chargeBonus);
                    dmg*=1.f+0.075f*(float)bu.veterancy; // D2: +7.5% daño/nivel (Range+Stable)
                    if(chargeBonus&&dmg>0){
                        sol.chargeReady=false;
                        tgt.pos=v2add(tgt.pos,v2scale(dir2,12.f));
                        foeUnits[targetUnit].morale-=10.f;
                    }
                    tgt.hp-=dmg;
                    playSfxVar(SFX_HIT_METAL0,3,0.75f);
                    fxSpawnSparks(tgt.pos,5);
                    fxSpawnBlood(tgt.pos,2);
                    if(tgt.hp<=0){
                        tgt.alive=false;
                        playSfx(SFX_DEATH,0.85f);
                        fxSpawnBlood(tgt.pos,9);
                        sol.kills++;
                        g_battle.dead.push_back({tgt.pos,1.f,8.f,
                            fTd.spriteBase==SPR_CAVALRY,tgt.angle,
                            foeUnits[targetUnit].typeIdx,
                            foeUnits[targetUnit].isPlayer?0:1});
                        if(fTd.soldierCount>0){
                            char logbuf[128];
                            snprintf(logbuf,127,"[T%d] %s soldier slain by %s",
                                g_campaign.turn,fTd.name,td.name);
                            battleLogAdd(logbuf);
                        }
                        for(int fui=0;fui<(int)foeUnits.size();fui++){
                            for(auto& fs:foeUnits[fui].soldiers){
                                if(!fs.alive) continue;
                                if(vdist(tgt.pos,fs.pos)<80.f) foeUnits[fui].morale-=3.f;
                            }
                        }
                        sol.attackTargetSoldier=-1; sol.attackTargetUnit=-1;
                        sol.targetRefreshTimer=0.f;
                    }
                    sol.meleeTimer=td.meleeInterval;
                }
                sol.chargeMoveTime=0.f;
            }
            // Priority 2: RANGED COMBAT (back line support, not in melee)
            else if(td.range>0&&dist<=(float)td.range&&!sol.inMelee&&sol.rangeTimer<=0){
                if(losClean(sol.pos,tgt.pos,bu.soldiers,si)){
                    sol.state=SS_ATTACKING_RANGED;
                    float dmg=calcMissileDmg(td,fTd);
                    dmg*=1.f+0.075f*(float)bu.veterancy; // D2: +7.5% daño/nivel
                    bool isCross=(td.missileReload>2.5f);
                    float projSpeed=isCross?380.f:320.f;
                    g_battle.projs.push_back({sol.pos,v2scale(dir2,projSpeed),dmg,true,bu.isPlayer,isCross});
                    playSfx(SFX_ARROW_SHOOT,0.5f);
                    sol.rangeTimer=td.missileReload;
                    sol.chargeMoveTime=0.f;
                }
            }
            // Priority 3: MOVE TOWARD SLOT IF ENEMY OUT OF RANGE
            else {
                sol.state=SS_MOVING_SLOT;
                float slotDist=vdist(sol.pos,sol.formationSlot);

                if(slotDist>8.f){
                    Vector2 toSlot=vnorm(v2sub(sol.formationSlot,sol.pos));
                    toSlot=avoidObstacles(sol.pos,toSlot,td.speed,g_battle.obstacles);
                    // v8.2: Fast movement - no deceleration curve
                    sol.pos=v2add(sol.pos,v2scale(toSlot,td.speed*mdt));
                    sol.angleTarget=dirToAngle(toSlot);
                    sol.chargeMoveTime+=dt;
                    if(sol.chargeMoveTime>0.8f) sol.chargeReady=(td.spriteBase==SPR_CAVALRY);
                } else {
                    sol.state=SS_IDLE;
                    sol.chargeMoveTime=0.f;
                }
            }

            sol.angle=turnAngle(sol.angle,sol.angleTarget,300.f*mdt);
        }

        // Update group state based on soldier activity
        bool anyMelee=false,anyMoving=false;
        for(auto& sol:bu.soldiers){
            if(!sol.alive) continue;
            if(sol.inMelee) anyMelee=true;
            if(sol.state==SS_MOVING_SLOT) anyMoving=true;
        }
        if(anyMelee) bu.groupState=UGS_ENGAGED;
        else if(anyMoving) bu.groupState=UGS_ADVANCING;
        else bu.groupState=UGS_IDLE;

        // ═══════════════════════════════════════════════════════════════════════════
        //  ANCHOR UPDATE v8.1: FOLLOW CENTROID OF SOLDIERS
        // ═══════════════════════════════════════════════════════════════════════════
        // Calculate actual centroid of all alive soldiers
        Vector2 actualCentroid={0,0};
        int aliveCnt2=0;
        for(auto& sol:bu.soldiers){
            if(sol.alive){
                actualCentroid=v2add(actualCentroid,sol.pos);
                aliveCnt2++;
            }
        }
        if(aliveCnt2>0) actualCentroid=v2scale(actualCentroid,1.f/(float)aliveCnt2);
        else actualCentroid=bu.anchorPos; // fallback if all dead

        // Move anchor smoothly toward desired position (toward movement target or auto-advance)
        // MINIMAL centroid correction - anchor leads, soldiers follow
        Vector2 targetAnchor=desiredAnchor;

        // v8.3: ULTRA-AGGRESSIVE ADVANCE - almost pure desired movement
        // Only use 10% centroid correction to prevent oscillation
        // This allows anchor to lead units toward enemy without drag
        // (anchorHold: ranged en hold => 0%, si no el pull posicional
        // barre el hold y acaban a cuerpo a cuerpo)
        float centroidWeight=anchorHold?0.f:0.1f;  // Minimal correction for all movement types
        targetAnchor.x=targetAnchor.x*(1.f-centroidWeight)+actualCentroid.x*centroidWeight;
        targetAnchor.y=targetAnchor.y*(1.f-centroidWeight)+actualCentroid.y*centroidWeight;

        // Snap to target anchor (v8.3: INSTANT RESPONSE)
        // At 60 FPS: 50.0 * 0.016 = 80% per frame (1-2 frames convergence)
        bu.anchorPos.x+=(targetAnchor.x-bu.anchorPos.x)*50.f*dt;
        bu.anchorPos.y+=(targetAnchor.y-bu.anchorPos.y)*50.f*dt;
    }
}

// Enemy AI: assign orders periodically (3.2: aiReactTimer, differentiated behaviors)
void updateEnemyAI(float dt){
    int nPlayerAlive=0;
    for(auto& pu:g_battle.playerUnits){
        int ac=0; for(auto& s:pu.soldiers) if(s.alive) ac++;
        if(ac>0) nPlayerAlive++;
    }
    if(nPlayerAlive==0) return;

    for(int ui=0;ui<(int)g_battle.enemyUnits.size();ui++){
        BattleUnit& bu=g_battle.enemyUnits[ui];
        if(bu.groupState==UGS_ROUTING) continue;
        int aliveCnt=0;
        for(auto& sol:bu.soldiers) if(sol.alive) aliveCnt++;
        if(aliveCnt==0) continue;

        // Tick AI react timer
        bu.aiReactTimer-=dt;
        if(bu.aiReactTimer>0) continue; // skip recalculation this frame
        bu.aiReactTimer=1.5f; // recalculate every 1.5s

        if(bu.typeIdx>=unitTypeCount()) continue;
        const UnitTypeDef& td=g_unitTypes[bu.typeIdx];

        // Find nearest player group
        float best=1e9f; int best_u=-1;
        for(int pu=0;pu<(int)g_battle.playerUnits.size();pu++){
            int pAlive=0;
            for(auto& ps:g_battle.playerUnits[pu].soldiers) if(ps.alive) pAlive++;
            if(pAlive==0) continue;
            float d=vdist(bu.anchorPos,g_battle.playerUnits[pu].anchorPos);
            if(d<best){best=d;best_u=pu;}
        }
        if(best_u<0) continue;

        Vector2 targetPos=g_battle.playerUnits[best_u].anchorPos;
        float distToTarget=vdist(bu.anchorPos,targetPos);

        if(td.range>0){
            // 3.2: Ranged — maintain 200px distance, retreat if <150px
            if(distToTarget<150.f){
                // Retreat
                Vector2 away=vnorm(v2sub(bu.anchorPos,targetPos));
                bu.orderTarget=v2add(bu.anchorPos,v2scale(away,200.f));
                bu.orderAttack=-1;
                bu.hasExplicitOrder=true;
                bu.orderType=1; // move
                // FIX: removed bu.anchorPos=bu.orderTarget (was teleporting)
            } else if(distToTarget>220.f){
                bu.orderAttack=best_u;
                bu.orderType=0; // attack
                bu.hasExplicitOrder=false;
            } else {
                // Hold — just attack in range
                bu.orderAttack=best_u;
                bu.orderType=2; // hold
                bu.hasExplicitOrder=false;
            }
        } else if(td.spriteBase==SPR_CAVALRY){
            // 3.2: Cavalry — flanking angle 45-90 degrees lateral
            Vector2 toTarget=vnorm(v2sub(targetPos,bu.anchorPos));
            float flankAngle=DEG2RAD*(45.f+frandMT()*45.f);
            float side=(ui%2==0)?1.f:-1.f;
            Vector2 flankDir={
                toTarget.x*cosf(flankAngle*side)-toTarget.y*sinf(flankAngle*side),
                toTarget.x*sinf(flankAngle*side)+toTarget.y*cosf(flankAngle*side)
            };
            bu.orderTarget=v2add(targetPos,v2scale(flankDir,80.f));
            bu.orderAttack=best_u;
            bu.orderType=3; // flank
            bu.hasExplicitOrder=true;
            bu.anchorPos=v2add(bu.anchorPos,v2scale(vnorm(v2sub(bu.orderTarget,bu.anchorPos)),5.f));
        } else {
            // 3.2: Infantry — distribute across different player groups if multiple exist
            if(nPlayerAlive>1){
                // Spread: pick target based on our index
                int targetIdx=ui % nPlayerAlive;
                int counted=0;
                for(int pu=0;pu<(int)g_battle.playerUnits.size();pu++){
                    int ac=0; for(auto& ps:g_battle.playerUnits[pu].soldiers) if(ps.alive) ac++;
                    if(ac>0){
                        if(counted==targetIdx){best_u=pu;break;}
                        counted++;
                    }
                }
            }
            bu.orderAttack=best_u;
            bu.orderType=0; // attack
            bu.hasExplicitOrder=false;
        }

        // Update formation slots toward target (6.6: use proper facing angle)
        if(bu.orderAttack>=0&&validIdx(bu.orderAttack,g_battle.playerUnits)){
            auto slots=calcFormationSlots(bu.anchorPos,(int)bu.soldiers.size(),bu.formationFacing);
            for(int s=0;s<(int)bu.soldiers.size()&&s<(int)slots.size();s++)
                bu.soldiers[s].formationSlot=slots[s];
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  BATTLE DRAW
// ═══════════════════════════════════════════════════════════════════════════
void drawBattlefield(){
    // Terrain background
    ClearBackground(C_TERRAIN);
    // Variation noise (static, initialized once)
    static bool terrainInit=false;
    static Image terrainNoise;
    static Texture2D terrainTex{};
    if(!terrainInit){
        terrainNoise=GenImageColor(BATTLE_W,BATTLE_H,C_TERRAIN);
        srand(42);
        for(int y2=0;y2<BATTLE_H;y2+=8){
            for(int x2=0;x2<BATTLE_W;x2+=8){
                int v=rand()%12-6;
                Color c2={clampU8((int)C_TERRAIN.r+v),clampU8((int)C_TERRAIN.g+v),clampU8((int)C_TERRAIN.b+v),255};
                ImageDrawRectangle(&terrainNoise,x2,y2,8,8,c2);
            }
        }
        terrainTex=LoadTextureFromImage(terrainNoise);
        UnloadImage(terrainNoise);
        terrainInit=true; // 1.1: was missing, caused re-generation every frame
    }
    DrawTexture(terrainTex,0,0,WHITE);

    // Grid
    for(int x2=0;x2<BATTLE_W;x2+=128)
        DrawLine(x2,0,x2,BATTLE_H,{35,46,26,80});
    for(int y2=0;y2<BATTLE_H;y2+=128)
        DrawLine(0,y2,BATTLE_W,y2,{35,46,26,80});

    // Center line
    DrawLine(BATTLE_W/2,0,BATTLE_W/2,BATTLE_H,{50,70,35,100});

    // Obstacles (v11: colores por terreno — ríos/lagos azul, montaña gris, bosque verde, ciudad marrón)
    for(auto& obs:g_battle.obstacles){
        Color oc, edge;
        switch(g_battle.terrain){
            case TERRAIN_FOREST:
                oc={25,55,20,200};  edge={20,45,15,255}; break;
            case TERRAIN_MOUNTAIN:
                oc={90,80,70,200};  edge={60,55,50,255}; break;
            case TERRAIN_COAST:
                // Costa: lagos/rocas — azul agua + arena
                oc={50,90,130,190}; edge={90,130,160,255}; break;
            default: // PLAIN
                // Río (rect muy largo y fino) → azul; colinas → marrón
                if(obs.width>400.f||obs.height>400.f){ oc={55,100,150,200}; edge={90,140,180,255}; }
                else { oc={105,95,75,200}; edge={75,68,52,255}; }
                break;
        }
        DrawRectangleRec(obs,oc);
        DrawRectangleLinesEx(obs,2,edge);
    }
    // Ciudades: indicador de puerta en el centro del muro (si hay muralla)
    if(g_battle.terrain==TERRAIN_PLAIN||g_battle.terrain==TERRAIN_COAST){
        // Puente: marcar el hueco dejado en el río con dos "pilares" de madera
        // (solo visual; el hueco ya es pasable porque no es obstáculo)
    }

    // Deployment zones (faint)
    DrawRectangle(0,0,(int)(BATTLE_W*0.35f),BATTLE_H,{80,120,220,12});
    DrawRectangle((int)(BATTLE_W*0.65f),0,(int)(BATTLE_W*0.35f),BATTLE_H,{220,60,50,12});
}

void drawAllUnits(){
    // Reloj de animación (se congela en pausa; con speed x2 corre al doble)
    static float s_animClock=0.f;
    s_animClock+=GetFrameTime()*(g_battle.paused?0.f:g_battle.timeScale);

    // AABB de la vista en mundo (4 esquinas; la camara tiene rotacion iso,
    // asi que el AABB del rect rotado cubre siempre la pantalla completa).
    // Fase A: culling de soldados/cadaveres fuera de pantalla.
    float vx0=1e9f,vy0=1e9f,vx1=-1e9f,vy1=-1e9f;
    {
        const Vector2 corners[4]={{0.f,0.f},{(float)SCREEN_W,0.f},
                                  {0.f,(float)SCREEN_H},{(float)SCREEN_W,(float)SCREEN_H}};
        for(const Vector2& sc:corners){
            Vector2 w=screenToWorldBattle(sc);
            if(w.x<vx0)vx0=w.x;
            if(w.x>vx1)vx1=w.x;
            if(w.y<vy0)vy0=w.y;
            if(w.y>vy1)vy1=w.y;
        }
        const float PAD=48.f;
        vx0-=PAD; vy0-=PAD; vx1+=PAD; vy1+=PAD;
    }

    // Dead markers
    for(auto& d:g_battle.dead){
        if(d.pos.x<vx0||d.pos.x>vx1||d.pos.y<vy0||d.pos.y>vy1) continue;
        // Hoja iso de muerte (Fase 2); si no, el blob generico
        if(drawUnitDeath(d.typeIdx,d.team,d.pos,d.angle,0.65f,8.f-d.timer,d.alpha))
            continue;
        int a=(int)(d.alpha*160);
        Color dc={60,20,20,(unsigned char)a};
        if(d.isCavalry){
            DrawEllipse((int)d.pos.x,(int)d.pos.y,12,8,dc);
        } else {
            DrawCircleV(d.pos,7,dc);
        }
        DrawLine((int)(d.pos.x-8),(int)(d.pos.y-8),(int)(d.pos.x+8),(int)(d.pos.y+8),{80,20,20,(unsigned char)a});
        DrawLine((int)(d.pos.x+8),(int)(d.pos.y-8),(int)(d.pos.x-8),(int)(d.pos.y+8),{80,20,20,(unsigned char)a});
    }

    // Projectiles
    for(auto& p:g_battle.projs){
        if(!p.alive) continue;
        Color pc=p.isCrossbow?Color{150,140,100,255}:Color{180,140,60,255};
        if(p.fromPlayer) pc={pc.r,(unsigned char)(pc.g+20),pc.b,255};
        DrawCircleV(p.pos,3,pc);
    }

    // Draw order lines for selected player units
    for(auto& bu:g_battle.playerUnits){
        if(!bu.selected) continue;
        // Show current orders visually
        if(bu.orderAttack>=0&&bu.orderAttack<(int)g_battle.enemyUnits.size()){
            // Attack order — red line to target
            DrawLineEx(bu.anchorPos,g_battle.enemyUnits[bu.orderAttack].anchorPos,2.5f,{255,80,80,160});
            // Arrow marker on enemy
            Vector2 dir=vnorm(v2sub(g_battle.enemyUnits[bu.orderAttack].anchorPos,bu.anchorPos));
            Vector2 markerPos=v2sub(g_battle.enemyUnits[bu.orderAttack].anchorPos,v2scale(dir,30.f));
            DrawCircleV(markerPos,8,{255,50,50,200});
            DrawText("⚔",(int)(markerPos.x-8),(int)(markerPos.y-8),16,{255,100,100,255});
        } else if(bu.hasExplicitOrder&&bu.orderType==1){
            // Move order — blue line to destination
            DrawLineEx(bu.anchorPos,bu.orderTarget,2.5f,{100,180,255,160});
            // Target marker
            DrawCircleLines((int)bu.orderTarget.x,(int)bu.orderTarget.y,12,{100,180,255,220});
            DrawText("→",(int)(bu.orderTarget.x-8),(int)(bu.orderTarget.y-6),16,{100,200,255,255});
        }
    }

    // Draw enemy units
    for(auto& bu:g_battle.enemyUnits){
        if(bu.typeIdx>=unitTypeCount()) continue;
        const UnitTypeDef& td=g_unitTypes[bu.typeIdx];
        int alive=0; for(auto& s:bu.soldiers) if(s.alive) alive++;
        if(alive==0) continue;
        bool anyVis=false;
        // Group bounding circle
        DrawCircleLines((int)bu.anchorPos.x,(int)bu.anchorPos.y,
                        (int)(soldierMeleeRadius(td)*(float)alive*0.4f+20.f),
                        {180,40,40,40});
        for(auto& sol:bu.soldiers){
            if(!sol.alive) continue;
            if(sol.pos.x<vx0||sol.pos.x>vx1||sol.pos.y<vy0||sol.pos.y>vy1) continue;
            anyVis=true;
            int anim=SS_MOVING_SLOT==sol.state||SS_MOVING_TARGET==sol.state||
                     SS_FLEEING==sol.state?UA_WALK:
                     (sol.state==SS_ATTACKING_MELEE||sol.state==SS_ATTACKING_RANGED)?
                     UA_ATTACK:UA_IDLE;
            float phase=fmodf(sol.pos.x*0.37f+sol.pos.y*0.73f,10.f); // desincroniza
            drawUnit(bu.typeIdx,1,sol.pos,sol.angle,0.65f,true,true,
                     anim,s_animClock+phase);
            // 3.5: HP bar only when damaged
            if(sol.hp<td.hpPerSoldier){
                float bw=8.f; float ratio=sol.hp/td.hpPerSoldier;
                DrawRectangle((int)(sol.pos.x-bw/2),(int)(sol.pos.y-unitHeadOffset(bu.typeIdx,0.65f)-3),(int)bw,2,DARKGRAY);
                Color hc=ratio>0.5f?Color{0,228,48,255}:ratio>0.25f?Color{253,249,0,255}:Color{230,41,55,255};
                DrawRectangle((int)(sol.pos.x-bw/2),(int)(sol.pos.y-unitHeadOffset(bu.typeIdx,0.65f)-3),(int)(bw*ratio),2,hc);
            }
        }
        // Morale bar
        if(anyVis){
        float mr=bu.morale/100.f;
        DrawRectangle((int)(bu.anchorPos.x-30),(int)(bu.anchorPos.y-28),60,5,DARKGRAY);
        DrawRectangle((int)(bu.anchorPos.x-30),(int)(bu.anchorPos.y-28),(int)(60*mr),5,
                      mr>0.5f?Color{255,161,0,255}:mr>0.2f?Color{253,249,0,255}:Color{230,41,55,255});
        }
    }

    // Draw player units
    for(auto& bu:g_battle.playerUnits){
        if(bu.typeIdx>=unitTypeCount()) continue;
        const UnitTypeDef& td=g_unitTypes[bu.typeIdx];
        int alive=0; for(auto& s:bu.soldiers) if(s.alive) alive++;
        if(alive==0) continue;
        if(bu.selected){
            // 3.10: Pulsing selection circle
            float pulse=soldierMeleeRadius(td)*(float)alive*0.4f+24.f+sinf(g_menuTime*4.f)*4.f;
            DrawCircleLines((int)bu.anchorPos.x,(int)bu.anchorPos.y,
                            (int)pulse,
                            {C_ALLY.r,C_ALLY.g,C_ALLY.b,150});
            // 3.10: Soldier count above anchor
            char cntbuf[16]; snprintf(cntbuf,15,"%d",alive);
            int ctw=MeasureText(cntbuf,11);
            DrawText(cntbuf,(int)(bu.anchorPos.x-ctw/2),(int)(bu.anchorPos.y-pulse-14),11,C_ALLY);
        }
        for(auto& sol:bu.soldiers){
            if(!sol.alive) continue;
            if(sol.pos.x<vx0||sol.pos.x>vx1||sol.pos.y<vy0||sol.pos.y>vy1) continue;
            int anim=SS_MOVING_SLOT==sol.state||SS_MOVING_TARGET==sol.state||
                     SS_FLEEING==sol.state?UA_WALK:
                     (sol.state==SS_ATTACKING_MELEE||sol.state==SS_ATTACKING_RANGED)?
                     UA_ATTACK:UA_IDLE;
            float phase=fmodf(sol.pos.x*0.37f+sol.pos.y*0.73f,10.f); // desincroniza
            drawUnit(bu.typeIdx,0,sol.pos,sol.angle,0.65f,bu.selected,true,
                     anim,s_animClock+phase);
            // 3.5: HP bar only when damaged
            if(sol.hp<td.hpPerSoldier){
                float bw=8.f; float ratio=sol.hp/td.hpPerSoldier;
                DrawRectangle((int)(sol.pos.x-bw/2),(int)(sol.pos.y-unitHeadOffset(bu.typeIdx,0.65f)-3),(int)bw,2,DARKGRAY);
                Color hc=ratio>0.5f?Color{0,228,48,255}:ratio>0.25f?Color{253,249,0,255}:Color{230,41,55,255};
                DrawRectangle((int)(sol.pos.x-bw/2),(int)(sol.pos.y-unitHeadOffset(bu.typeIdx,0.65f)-3),(int)(bw*ratio),2,hc);
            }
        }
        // 6.2: Veterancy stars above anchor
        if(bu.veterancy>0){
            for(int v=0;v<bu.veterancy;v++){
                DrawText("★",(int)(bu.anchorPos.x-10+v*10),(int)(bu.anchorPos.y-36),12,C_GOLD);
            }
        }
        // Control group badge: show which group(s) this unit belongs to
        if(bu.isPlayer){
            for(int g=0;g<9;g++){
                const ControlGroup& cg=g_battle.controlGroups[g];
                if(!cg.active) continue;
                for(int idx:cg.unitIndices){
                    if(validIdx(idx,g_battle.playerUnits)&&&g_battle.playerUnits[idx]==&bu){
                        // Draw group number badge
                        bool isActive=(g_battle.activeControlGroup==g);
                        Color bgc=isActive?Color{200,165,80,230}:Color{30,30,30,180};
                        Color fgc=isActive?Color{20,10,0,255}:Color{180,160,100,255};
                        int bx=(int)(bu.anchorPos.x+22);
                        int by=(int)(bu.anchorPos.y-42);
                        DrawRectangle(bx,by,14,14,bgc);
                        DrawRectangleLinesEx({(float)bx,(float)by,14,14},1,{150,120,50,200});
                        DrawText(TextFormat("%d",g+1),bx+4,by+2,10,fgc);
                        break;
                    }
                }
            }
        }
        // Morale bar
        float mr=bu.morale/100.f;
        DrawRectangle((int)(bu.anchorPos.x-30),(int)(bu.anchorPos.y-28),60,5,DARKGRAY);
        DrawRectangle((int)(bu.anchorPos.x-30),(int)(bu.anchorPos.y-28),(int)(60*mr),5,
                      mr>0.5f?Color{0,158,47,255}:mr>0.2f?Color{253,249,0,255}:Color{230,41,55,255});
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  HUD DRAW (battle)
// ═══════════════════════════════════════════════════════════════════════════
void drawBattleHUD(Vector2 mouse){
    int hudY=SCREEN_H-HUD_H;

    // ═══ v11: OVERLAY DE PREPARACIÓN (formaciones + cuenta atrás) ═══
    if(g_battle.prepPhase){
        DrawRectangle(0,0,SCREEN_W,SCREEN_H,{0,0,0,120});
        float boxW=uiPx(640.f), boxH=uiPx(320.f);
        float bx=(SCREEN_W-boxW)*0.5f, by=(SCREEN_H-boxH)*0.5f;
        DrawRectangle((int)bx,(int)by,(int)boxW,(int)boxH,{16,14,10,245});
        DrawRectangleLinesEx({bx,by,boxW,boxH},2,C_GOLD);
        DrawText("BATTLE PREPARATION",(int)(bx+20),(int)(by+14),24,C_GOLD);
        DrawText("Set your formation — movement is locked until the battle begins",
                 (int)(bx+20),(int)(by+48),11,C_SECONDARY);
        // Cuenta atrás grande
        int secs=(int)ceilf(g_battle.prepTimer);
        char tbuf[32]; snprintf(tbuf,31,"0:%02d",secs);
        int tw=MeasureText(tbuf,48);
        Color tcol=secs<=5?Color{230,90,70,255}:C_PARCHMENT;
        DrawText(tbuf,(int)(bx+boxW*0.5f-tw*0.5f),(int)(by+70),48,tcol);
        DrawText("seconds remaining",(int)(bx+boxW*0.5f-70),(int)(by+124),11,C_SECONDARY);

        // Selector de formación: 4 botones
        static const char* formNames[4]={"LINE","COLUMN","SQUARE","LOOSE"};
        static const char* formDesc[4]={"Wide frontage","Deep column","Balanced box","Spread out"};
        float fbtnW=(boxW-60.f)/4.f, fbtnH=uiPx(54.f);
        float fx0=bx+16.f, fy0=by+156.f;
        for(int f=0;f<4;f++){
            Rectangle fr={fx0+(float)f*(fbtnW+6.f),fy0,fbtnW,fbtnH};
            bool hovF=ptInRect(mouse,fr);
            bool selF=(g_battle.prepFormation==f);
            Color bg=selF?Color{50,70,30,255}:(hovF?Color{35,40,30,255}:Color{22,20,16,255});
            DrawRectangleRec(fr,bg);
            DrawRectangleLinesEx(fr,selF?2.f:1.f,selF?C_GOLD:Color{70,65,50,255});
            int nw=MeasureText(formNames[f],13);
            DrawText(formNames[f],(int)(fr.x+fbtnW*0.5f-nw*0.5f),(int)(fr.y+8),13,
                     selF?C_GOLD:C_PARCHMENT);
            int dw=MeasureText(formDesc[f],9);
            DrawText(formDesc[f],(int)(fr.x+fbtnW*0.5f-dw*0.5f),(int)(fr.y+30),9,C_SECONDARY);
            if(hovF&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)){
                g_battle.prepFormation=f;
                // Aplicar la formación ajustando parámetros (afecta a calcFormationSlots)
                switch(f){
                    case 0: g_formPerRow=8; g_formSpacing=24.f; break;
                    case 1: g_formPerRow=3; g_formSpacing=22.f; break;
                    case 2: g_formPerRow=5; g_formSpacing=26.f; break;
                    case 3: g_formPerRow=6; g_formSpacing=34.f; break;
                }
                // Recalcular slots de los soldados desplegados
                for(auto& bu:g_battle.playerUnits){
                    auto slots=calcFormationSlots(bu.anchorPos,(int)bu.soldiers.size(),
                                                  bu.formationFacing);
                    for(int s=0;s<(int)bu.soldiers.size()&&s<(int)slots.size();s++)
                        bu.soldiers[s].formationSlot=slots[s];
                }
                battleLogAdd(TextFormat("Formation set: %s",formNames[f]));
            }
        }
        // Botón BEGIN BATTLE
        Rectangle begR={bx+boxW*0.5f-uiPx(110.f),by+boxH-uiPx(64.f),uiPx(220.f),uiPx(44.f)};
        bool hovB=ptInRect(mouse,begR);
        DrawRectangleRec(begR,hovB?Color{55,110,45,255}:Color{35,75,30,255});
        DrawRectangleLinesEx(begR,2,C_GOLD);
        const char* blbl="BEGIN BATTLE!";
        int bw2=MeasureText(blbl,18);
        DrawText(blbl,(int)(begR.x+begR.width*0.5f-bw2*0.5f),(int)(begR.y+12),18,C_PARCHMENT);
        if(hovB&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)){
            g_battle.prepPhase=false;
            battleLogAdd("Battle begins!");
        }
        // No se dibuja el resto del HUD durante la preparación
        return;
    }

    // Top bar
    DrawRectangle(0,0,SCREEN_W,TOPBAR_H,{0,0,0,220});
    DrawText(g_battle.scenarioName,10,8,14,C_GOLD);
    DrawText(TextFormat("Turn: %d  |  [SPACE] Speed  |  [ESC] Pause  |  [Ctrl+1-9] Save Group  |  [1-9] Select Group",
             g_campaign.turn),
             SCREEN_W/2-360,8,10,C_SECONDARY);
    DrawText("Right-click unit/location to command — Group move preserves formation",
             SCREEN_W/2-280, 20, 9, Color{180, 160, 120, 200});
    // Draw active control group badges on right of top bar
    {
        int bx=SCREEN_W-10;
        for(int g=8;g>=0;g--){
            if(!g_battle.controlGroups[g].active) continue;
            bx-=20;
            bool isActive=(g_battle.activeControlGroup==g);
            Color bgc=isActive?Color{200,165,80,255}:Color{50,44,30,200};
            Color fgc=isActive?Color{10,5,0,255}:Color{180,160,90,255};
            DrawRectangle(bx,4,18,22,bgc);
            DrawRectangleLinesEx({(float)bx,4,18,22},1,{120,100,40,200});
            DrawText(TextFormat("%d",g+1),bx+5,7,12,fgc);
        }
    }

    // Bottom HUD
    DrawRectangle(0,hudY,SCREEN_W,HUD_H,{8,6,4,240});
    DrawLine(0,hudY,SCREEN_W,hudY,{80,65,30,200});

    // Selected unit portrait (left)
    BattleUnit* sel=nullptr;
    for(auto& bu:g_battle.playerUnits) if(bu.selected&&bu.typeIdx<unitTypeCount()){sel=&bu;break;}

    if(sel){
        const UnitTypeDef& td=g_unitTypes[sel->typeIdx];
        int alive=0; for(auto& s:sel->soldiers) if(s.alive) alive++;
        // Portrait box
        DrawRectangle(8,hudY+6,58,58,{td.r,td.g,td.b,100});
        DrawRectangleLinesEx({8,(float)(hudY+6),58,58},2,C_GOLD);
        drawUnit(sel->typeIdx,0,{37.f,(float)(hudY+61)},
                 g_menuTime*30.f,1.15f,true,false,UA_IDLE,g_menuTime);
        // Info
        DrawText(td.name,72,hudY+8,15,C_PARCHMENT);
        // HP bar
        float total=(float)(td.soldierCount)*td.hpPerSoldier;
        float curHp=0.f; for(auto& s:sel->soldiers) if(s.alive) curHp+=s.hp;
        float hpRat=curHp/total;
        DrawRectangle(72,hudY+28,220,10,DARKGRAY);
        DrawRectangle(72,hudY+28,(int)(220*hpRat),10,hpRat>0.5f?Color{0,228,48,255}:hpRat>0.25f?Color{253,249,0,255}:Color{230,41,55,255});
        // Morale bar
        DrawRectangle(72,hudY+42,220,8,DARKGRAY);
        float mr=sel->morale/100.f;
        DrawRectangle(72,hudY+42,(int)(220*mr),8,mr>0.5f?Color{0,158,47,255}:mr>0.2f?Color{253,249,0,255}:Color{230,41,55,255});
        DrawText(TextFormat("Morale: %.0f%%",sel->morale),298,hudY+41,12,C_SECONDARY);
        DrawText(TextFormat("Soldiers: %d / %d",alive,td.soldierCount),72,hudY+54,12,C_SECONDARY);
    } else {
        DrawText("No unit selected",12,hudY+34,14,{70,70,70,255});
    }

    // Center: group state and current orders
    if(sel){
        static const char* gsNames[]={"Idle","Advancing","Engaged","Routing"};
        const char* gsn=gsNames[(int)sel->groupState];
        Color gsc=sel->groupState==UGS_ENGAGED?C_ENEMY_COL:
                  sel->groupState==UGS_ROUTING?Color{230,41,55,255}:
                  sel->groupState==UGS_ADVANCING?C_ALLY:C_SECONDARY;
        int tw=MeasureText(gsn,16);
        DrawText(gsn,SCREEN_W/2-tw/2,hudY+25,16,gsc);

        // Show current orders
        if(sel->hasExplicitOrder){
            if(sel->orderAttack>=0&&sel->orderAttack<(int)g_battle.enemyUnits.size()){
                DrawText("📋 ATTACKING ENEMY UNIT",SCREEN_W/2-115,hudY+48,11,{255,100,100,255});
            } else if(sel->orderType==1){
                DrawText("📍 MOVING TO POSITION",SCREEN_W/2-110,hudY+48,11,{100,180,255,255});
            }
        } else {
            DrawText("🎯 IDLE (AUTO)",SCREEN_W/2-60,hudY+48,11,C_SECONDARY);
        }
    }

    // Right: counters + pause button
    int pa=0,ea=0;
    for(auto& bu:g_battle.playerUnits) for(auto& s:bu.soldiers) if(s.alive) pa++;
    for(auto& bu:g_battle.enemyUnits) for(auto& s:bu.soldiers) if(s.alive) ea++;
    DrawText(TextFormat("Allies: %d",pa),(int)(SCREEN_W-320),hudY+14,14,{80,160,255,255});
    DrawText(TextFormat("Enemies: %d",ea),(int)(SCREEN_W-320),hudY+34,14,C_ENEMY_COL);

    if(drawSmBtn({(float)(SCREEN_W-120),(float)(hudY+8),110,36},"PAUSE",mouse,{40,35,20,255},{70,60,35,255}))
        g_battle.paused=true;
    if(drawSmBtn({(float)(SCREEN_W-120),(float)(hudY+50),110,28},
                 g_battle.timeScale>1.f?"Speed: x2":"Speed: x1",mouse,
                 {30,30,50,255},{50,50,80,255}))
        g_battle.timeScale=(g_battle.timeScale>1.f)?1.f:2.f;

    // 3.6: Minimap (150x90px, bottom-right above HUD)
    int mmW=150,mmH=90;
    int mmX=SCREEN_W-160, mmY=hudY-mmH-8;
    DrawRectangle(mmX,mmY,mmW,mmH,{0,0,0,180});
    DrawRectangleLinesEx({(float)mmX,(float)mmY,(float)mmW,(float)mmH},1,{80,65,30,200});
    // Draw unit dots
    for(auto& bu:g_battle.playerUnits){
        int ac=0; for(auto& s:bu.soldiers) if(s.alive) ac++;
        if(ac==0) continue;
        int mx2=(int)(mmX+bu.anchorPos.x/(float)BATTLE_W*mmW);
        int my2=(int)(mmY+bu.anchorPos.y/(float)BATTLE_H*mmH);
        DrawRectangle(mx2-2,my2-2,4,4,C_ALLY);
    }
    for(auto& bu:g_battle.enemyUnits){
        int ac=0; for(auto& s:bu.soldiers) if(s.alive) ac++;
        if(ac==0) continue;
        int mx2=(int)(mmX+bu.anchorPos.x/(float)BATTLE_W*mmW);
        int my2=(int)(mmY+bu.anchorPos.y/(float)BATTLE_H*mmH);
        DrawRectangle(mx2-2,my2-2,4,4,C_ENEMY_COL);
    }
    // Draw viewport rect on minimap
    float vx,vy,vw,vh;
    battleViewRect(vx,vy,vw,vh);
    float vwW=vw/(float)BATTLE_W*mmW;
    float vhR=vh/(float)BATTLE_H*mmH;
    float vwX=mmX+vx/(float)BATTLE_W*mmW;
    float vwY=mmY+vy/(float)BATTLE_H*mmH;
    DrawRectangleLinesEx({vwX,vwY,vwW,vhR},1,WHITE);

    // 6.4: Battle log (last 5 entries, fade older ones)
    int logX=8, logY=hudY-8;
    int logCount=std::min(5,(int)g_battleLog.size());
    for(int i=0;i<logCount;i++){
        unsigned char a=(unsigned char)(200-i*35);
        DrawText(g_battleLog[i].c_str(),logX,logY-(i+1)*16,10,{180,160,120,a});
    }

    // 4.9: FPS counter
    if(g_settings.showFPS) DrawFPS(8,8);
}

// ═══════════════════════════════════════════════════════════════════════════
