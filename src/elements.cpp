#include "elements.h"
#include "config.h"
#include "util.h"
#include "ui.h"
#include "sprite.h"
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

std::vector<UnitTypeDef> g_unitTypes;
std::vector<UnitTypeDef> g_vanillaUnitTypes;  // copy of builtin types at init (for Default button)
std::vector<Texture2D>   g_playerTextures;
std::vector<Texture2D>   g_enemyTextures;

// D8: formacion de la batalla actual (copiada de g_settings en initBattle)
int   g_formPerRow  = 5;
float g_formSpacing = 26.f;

void initBuiltinTypes(){
    g_unitTypes.clear();
    // Helper lambda
    auto add=[&](const char* name,int soldiers,float hp,int armor,int spd,
                 int matk,int mdef,float mbase,float map2,float mint,
                 int rng,float msbase,float msap,float mrel,
                 int rGold,int rFood,int rIron,int rTurns,int mGold,int mFood,
                 float morale,unsigned char r,unsigned char g,unsigned char b,
                 SpriteBase spr,int wh,int bReqs,const char* lore){
        UnitTypeDef t{};
        strncpy(t.name,name,sizeof(t.name)-1); t.name[sizeof(t.name)-1]='\0';
        t.soldierCount=soldiers; t.hpPerSoldier=hp; t.armor=armor; t.speed=spd;
        t.meleeAttack=matk; t.meleeDefense=mdef;
        t.meleeBaseDmg=mbase; t.meleeAPDmg=map2; t.meleeInterval=mint;
        t.range=rng; t.missileBaseDmg=msbase; t.missileAPDmg=msap; t.missileReload=mrel;
        t.recruitGold=rGold; t.recruitFood=rFood; t.recruitIron=rIron;
        t.recruitTurns=rTurns; t.maintGold=mGold; t.maintFood=mFood;
        t.morale=morale;
        t.r=r; t.g=g; t.b=b; t.spriteBase=spr; t.weaponHint=wh;
        t.isBuiltin=true; t.buildingReqs=bReqs;
        strncpy(t.lore,lore,sizeof(t.lore)-1); t.lore[sizeof(t.lore)-1]='\0';
        g_unitTypes.push_back(t);
    };

    // Bit flags for building reqs: 0=Barracks,1=Stable,2=Range,3=Smithy,4=Workshop
    //          name              sol hp  arm spd  matk mdef mbase map  mint  rng msbase msap mrel  rGold rFood rIron rT mG  mF  mor   r    g    b       spr          wh  breqs  lore
    add("Spear Levy",             60, 12, 0,  80,  18,  12,  8.f, 1.5f,1.8f, 0,  0.f,  0.f, 0.f,  180,  30,   0,   1,  20, 20, 80.f, 200,180,140, SPR_INFANTRY,  0,  1,   "Peasant levy armed with spears. Weak but cheap and numerous.");
    add("Axe Militia",            60, 14, 5,  75,  24,  16, 12.f, 3.f, 1.6f, 0,  0.f,  0.f, 0.f,  240,  40,   0,   1,  28, 25, 78.f, 160,110, 60, SPR_INFANTRY,  1,  1,   "Free citizens wielding axes. Better offense than spear levy.");
    add("Poleaxe Retinue",        40, 22, 14, 70,  36,  24, 18.f, 7.f, 1.4f, 0,  0.f,  0.f, 0.f,  400,  60,  60,   2,  45, 20, 85.f, 140,150,160, SPR_INFANTRY,  2,  9,   "Professional soldiers wielding poleaxes. Heavy armor, reliable.");
    add("Dismounted Knights",     30, 38, 22, 60,  44,  30, 22.f,13.f, 1.6f, 0,  0.f,  0.f, 0.f,  700, 100, 100,   3,  80, 15, 90.f, 200,210,220, SPR_INFANTRY,  3,  17,  "Elite knights fighting on foot. Exceptional armor and morale.");
    add("Bow Levy",               60, 11, 0,  85,  16,  10,  7.f, 1.f, 2.0f,260, 10.f, 1.5f,2.5f, 200,  30,   0,   1,  22, 20, 75.f, 190,170,100, SPR_RANGED,    0,  4,   "Peasant archers. Low rate of fire but plentiful and cheap.");
    add("Crossbow Retinue",       40, 18, 8,  70,  22,  18,  9.f, 2.f, 2.2f,300, 16.f, 7.f, 4.0f, 420,  60,  60,   2,  50, 20, 82.f,  80,130, 80, SPR_RANGED,    0,  12,  "Trained crossbowmen. Slow reload but punishing armor penetration.");
    add("Mounted Sergeants",      40, 20, 8, 160,  28,  20, 15.f, 4.f, 1.4f, 0,  0.f,  0.f, 0.f,  520,  80,  40,   2,  60, 30, 83.f, 140, 90, 60, SPR_CAVALRY,   0,  2,   "Light cavalry. Swift flankers and pursuit specialists.");
    add("Knights",                24, 42, 26,140,  50,  34, 26.f,18.f, 1.5f, 0,  0.f,  0.f, 0.f,  960, 120, 120,   4, 110, 25, 92.f, 220,220,200, SPR_CAVALRY,   0,  18,  "Heavy armored knights. Devastating charge, supreme melee prowess.");

    // Fase J: héroes. soldierCount=1 (unidad individual), recruit*=-1 => no
    // aparecen en la lista de reclutamiento (se contratan como generales).
    add("General",                 1,150, 25, 80,  45,  35, 20.f, 8.f, 1.2f, 0,  0.f,  0.f, 0.f,   -1, -1,  -1,  -1,   8,  4, 96.f, 210,175, 90, SPR_INFANTRY,  1,   0,  "A battlefield commander. Required to lead a field army.");
    add("King",                    1,300, 32, 80,  55,  45, 24.f,10.f, 1.1f, 0,  0.f,  0.f, 0.f,   -1, -1,  -1,  -1,  12,  6, 99.f, 240,200, 60, SPR_INFANTRY,  1,   0,  "The sovereign himself. If he falls in battle, the realm is lost.");

    g_vanillaUnitTypes = g_unitTypes;

    loadUnitSheets(); // hojas Kenney (assets/sprites); si faltan → procedural
    g_playerTextures.clear();
    g_enemyTextures.clear();
    for(int i=0;i<(int)g_unitTypes.size();i++) rebuildTexture(i);
}

int unitTypeCount(){ return (int)g_unitTypes.size(); }

// ───────────────────────────────────────────────────────────────────────────
void drawStatBars(float x,float y,float w,float h,const UnitTypeDef& td){
    struct Stat { const char* name; float val, maxv; Color col; };
    Stat stats[]={
        {"HP",     (float)(td.soldierCount)*td.hpPerSoldier, 3000.f, {60,220,60,255}},
        {"Armor",  (float)td.armor,  40.f,  {160,200,220,255}},
        {"Speed",  (float)td.speed, 180.f,  {60,180,220,255}},
        {"M.Atk",  (float)td.meleeAttack, 60.f, {220,160,40,255}},
        {"M.Def",  (float)td.meleeDefense,40.f, {180,120,40,255}},
        {"MelDmg", (float)(td.meleeBaseDmg+td.meleeAPDmg),50.f, {220,80,40,255}},
        {"Range",  (float)td.range, 320.f,  {200,200,80,255}},
        {"MisDmg", (float)(td.missileBaseDmg+td.missileAPDmg),30.f,{200,100,200,255}},
    };
    int n=8;
    float rh=h/(float)n;
    float labelW=uiPx(72.f);  // scale-aware so "Armor"/"Speed"/"MelDmg" etc. don't truncate
    float valW=uiPx(36.f);
    float bx=x+labelW, bw=w-labelW-valW;
    if(bw<20.f) bw=20.f;
    for(int i=0;i<n;i++){
        float fy=y+(float)i*rh;
        float barH=rh-uiPx(4.f); if(barH<4.f) barH=4.f;
        DrawText(stats[i].name,(int)x,(int)(fy+1),11,C_SECONDARY);
        DrawRectangle((int)bx,(int)(fy+1),(int)bw,(int)barH,{20,20,20,255});
        float ratio=std::min(1.f,stats[i].val/stats[i].maxv);
        DrawRectangle((int)bx,(int)(fy+1),(int)(bw*ratio),(int)barH,stats[i].col);
        DrawText(TextFormat("%.0f",stats[i].val),(int)(bx+bw+uiPx(4.f)),(int)(fy+1),11,WHITE);
    }
}

//  BATTLE FORMATION HELPERS (v7.0: LINE FORMATION - rectangular 360° rotation)
// ═══════════════════════════════════════════════════════════════════════════
// Dimensiones de la formacion: columnas = g_formPerRow (parametro por batalla;
// D8: el selector pre-batalla NO muta g_settings).
void formationDims(int count,int* cols,int* rows){
    if(count<1) count=1;
    int c=g_formPerRow;
    if(c<1) c=1;
    if(c>count) c=count;
    *cols=c;
    *rows=(count+c-1)/c;
}
std::vector<Vector2> calcFormationSlots(Vector2 anchor,int count,float facing){
    std::vector<Vector2> slots;

    // LINE FORMATION: wide rectangular front (width >> depth)
    // Creates a proper battle line that soldiers will maintain throughout combat
    int cols=1,rows=1;
    formationDims(count,&cols,&rows);

    // Spacing por batalla (g_formSpacing); default 26px para que las hojas
    // bakeadas (~24px de ancho) no se fundan en un blob.
    float spacing=g_formSpacing;

    // Facing direction vector (0° = right, 90° = down, 180° = left, 270° = up)
    float rad=facing*DEG2RAD;
    Vector2 fwd={cosf(rad),-sinf(rad)};      // forward direction
    Vector2 right={-fwd.y,fwd.x};             // right direction (perpendicular)

    // Generate slots: soldiers arranged in rows perpendicular to facing direction
    // This creates a rectangular line formation that rotates properly
    for(int r=0;r<rows;r++){
        for(int c=0;c<cols;c++){
            int idx=r*cols+c;
            if(idx>=count) break;

            // Offset from anchor:
            // - lateral (ox): across the front width
            // - depth (oy): depth into the formation
            float ox=(c-(cols-1)*0.5f)*spacing;  // lateral spacing across front
            float oy=r*spacing;                   // depth: full spacing (no overlap)

            // Calculate slot position: move from anchor along right and forward axes
            Vector2 slot={
                anchor.x + right.x*ox + fwd.x*oy,
                anchor.y + right.y*ox + fwd.y*oy
            };
            slots.push_back(slot);
        }
    }
    return slots;
}

// ═══════════════════════════════════════════════════════════════════════════
