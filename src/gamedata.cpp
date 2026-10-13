// src/gamedata.cpp - Carga de unidades data-driven (fase D1)
// Parser INI propio (clave = valor, comentarios en linea propia ';'/'#'),
// sin dependencias externas: sigue el precedente de config.cpp.
// Regla de fallback: una unidad SOLO se sustituye por su archivo si el
// archivo existe y TODAS sus claves son validas; si falta el archivo o una
// clave se conserva el builtin de elements.cpp y se avisa con archivo:linea.
#include "gamedata.h"
#include "combat.h"

#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <direct.h>   // _mkdir (Windows/MinGW; solo lo usa el self-test)

namespace {

// ── Tabla de unidades: archivo -> builtin de referencia ─────────────────────
struct UnitSlot { const char* file; const char* builtin; };
const UnitSlot kUnitSlots[] = {
    {"spear_levy.unit",        "Spear Levy"},
    {"axe_militia.unit",       "Axe Militia"},
    {"poleaxe_retinue.unit",   "Poleaxe Retinue"},
    {"dismounted_knights.unit","Dismounted Knights"},
    {"bow_levy.unit",          "Bow Levy"},
    {"crossbow_retinue.unit",  "Crossbow Retinue"},
    {"mounted_sergeants.unit", "Mounted Sergeants"},
    {"knights.unit",           "Knights"},
    {"general.unit",           "General"},
    {"king.unit",              "King"},
};
const int kUnitSlotCount = (int)(sizeof kUnitSlots / sizeof kUnitSlots[0]);

// ── Campos soportados: TODOS los de UnitTypeDef ─────────────────────────────
enum FieldId {
    F_NAME=0, F_SOLDIERS, F_HP, F_ARMOR, F_SPEED,
    F_MELEE_ATK, F_MELEE_DEF, F_MELEE_BASE, F_MELEE_AP, F_MELEE_INT,
    F_RANGE, F_MISSILE_BASE, F_MISSILE_AP, F_MISSILE_RELOAD,
    F_RECRUIT_GOLD, F_RECRUIT_FOOD, F_RECRUIT_IRON, F_RECRUIT_TURNS,
    F_MAINT_GOLD, F_MAINT_FOOD,
    F_MORALE, F_COLOR, F_SPRITE, F_WEAPON, F_REQUIRES, F_BUILTIN, F_LORE,
    F_COUNT
};
const char* kFieldKeys[F_COUNT] = {
    "name", "soldiers", "hp", "armor", "speed",
    "melee.attack", "melee.defense", "melee.base", "melee.ap", "melee.interval",
    "range", "missile.base", "missile.ap", "missile.reload",
    "recruit.gold", "recruit.food", "recruit.iron", "recruit.turns",
    "maint.gold", "maint.food",
    "morale", "color", "sprite", "weapon", "requires", "builtin", "lore"
};

// Mascara de bits de UnitTypeDef::buildingReqs (mismo orden que elements.cpp).
const char* kBuildingNames[5] = {"Barracks","Stable","Range","Smithy","Workshop"};
const char* kSpriteNames[3]   = {"Infantry","Cavalry","Ranged"};

void addMsg(std::vector<std::string>* msgs, const std::string& m){
    if(msgs) msgs->push_back(m);
}

std::string trim(const std::string& s){
    size_t a=0, b=s.size();
    while(a<b && isspace((unsigned char)s[a])) a++;
    while(b>a && isspace((unsigned char)s[b-1])) b--;
    return s.substr(a,b-a);
}

std::string cropEol(const char* s){
    std::string r(s);
    while(!r.empty() && (r[r.size()-1]=='\n' || r[r.size()-1]=='\r')) r.erase(r.size()-1);
    return r;
}

bool ieq(const std::string& a, const char* b){
    size_t i=0;
    for(; b[i]; i++){
        if(i>=a.size()) return false;
        if(tolower((unsigned char)a[i])!=tolower((unsigned char)b[i])) return false;
    }
    return i==a.size();
}

bool parseIntVal(const std::string& v, int& out){
    if(v.empty()) return false;
    const char* s=v.c_str(); char* e=nullptr;
    errno=0;
    long long l=strtoll(s,&e,10);
    if(e==s || *e!='\0' || errno==ERANGE) return false;
    if(l < (long long)INT_MIN || l > (long long)INT_MAX) return false;
    out=(int)l;
    return true;
}

bool parseFloatVal(const std::string& v, float& out){
    if(v.empty()) return false;
    const char* s=v.c_str(); char* e=nullptr;
    errno=0;
    float f=strtof(s,&e);
    if(e==s || *e!='\0' || errno==ERANGE || !std::isfinite(f)) return false;
    out=f;
    return true;
}

int findField(const std::string& key){
    for(int i=0;i<F_COUNT;i++) if(key==kFieldKeys[i]) return i;
    return -1;
}

// "clave = valor" (tambien acepta "clave valor", estilo config.cpp).
bool splitKV(const std::string& line, std::string& key, std::string& val){
    size_t eq=line.find('=');
    if(eq!=std::string::npos){
        key=trim(line.substr(0,eq));
        val=trim(line.substr(eq+1));
    }else{
        size_t sp=line.find_first_of(" \t");
        if(sp==std::string::npos) return false;
        key=trim(line.substr(0,sp));
        val=trim(line.substr(sp+1));
    }
    return !key.empty() && !val.empty();
}

bool setInt(const std::string& key,const std::string& v,int& out,std::string& err){
    if(!parseIntVal(v,out)){ err=key+": invalid integer '"+v+"'"; return false; }
    return true;
}
bool setFloat(const std::string& key,const std::string& v,float& out,std::string& err){
    if(!parseFloatVal(v,out)){ err=key+": invalid number '"+v+"'"; return false; }
    return true;
}

// Aplica un campo. Devuelve false con `err` relleno si el valor es invalido
// (en ese caso la unidad entera cae a fallback).
bool applyField(UnitTypeDef& t,int fi,const std::string& key,
                const std::string& val,std::string& err){
    switch(fi){
    case F_NAME:
        if(val.size()>=sizeof t.name){ err="name: too long (max 31 chars)"; return false; }
        snprintf(t.name,sizeof t.name,"%s",val.c_str());
        return true;
    case F_LORE:
        if(val.size()>=sizeof t.lore){ err="lore: too long (max 255 chars)"; return false; }
        snprintf(t.lore,sizeof t.lore,"%s",val.c_str());
        return true;
    case F_SOLDIERS: return setInt(key,val,t.soldierCount,err);
    case F_ARMOR:    return setInt(key,val,t.armor,err);
    case F_SPEED:    return setInt(key,val,t.speed,err);
    case F_MELEE_ATK:return setInt(key,val,t.meleeAttack,err);
    case F_MELEE_DEF:return setInt(key,val,t.meleeDefense,err);
    case F_RANGE:    return setInt(key,val,t.range,err);
    case F_RECRUIT_GOLD:  return setInt(key,val,t.recruitGold,err);
    case F_RECRUIT_FOOD:  return setInt(key,val,t.recruitFood,err);
    case F_RECRUIT_IRON:  return setInt(key,val,t.recruitIron,err);
    case F_RECRUIT_TURNS: return setInt(key,val,t.recruitTurns,err);
    case F_MAINT_GOLD:    return setInt(key,val,t.maintGold,err);
    case F_MAINT_FOOD:    return setInt(key,val,t.maintFood,err);
    case F_WEAPON:        return setInt(key,val,t.weaponHint,err);
    case F_HP:            return setFloat(key,val,t.hpPerSoldier,err);
    case F_MELEE_BASE:    return setFloat(key,val,t.meleeBaseDmg,err);
    case F_MELEE_AP:      return setFloat(key,val,t.meleeAPDmg,err);
    case F_MELEE_INT:     return setFloat(key,val,t.meleeInterval,err);
    case F_MISSILE_BASE:  return setFloat(key,val,t.missileBaseDmg,err);
    case F_MISSILE_AP:    return setFloat(key,val,t.missileAPDmg,err);
    case F_MISSILE_RELOAD:return setFloat(key,val,t.missileReload,err);
    case F_MORALE:        return setFloat(key,val,t.morale,err);
    case F_COLOR: {
        int r=0,g=0,b=0; char extra=0;
        if(sscanf(val.c_str()," %d %d %d %c",&r,&g,&b,&extra)!=3 ||
           r<0||r>255||g<0||g>255||b<0||b>255){
            err="color: expected 'R G B' (0..255) got '"+val+"'";
            return false;
        }
        t.r=(unsigned char)r; t.g=(unsigned char)g; t.b=(unsigned char)b;
        return true;
    }
    case F_SPRITE: {
        for(int i=0;i<3;i++) if(ieq(val,kSpriteNames[i])){ t.spriteBase=(SpriteBase)i; return true; }
        int n=-1;
        if(parseIntVal(val,n) && n>=0 && n<3){ t.spriteBase=(SpriteBase)n; return true; }
        err="sprite: expected Infantry|Cavalry|Ranged got '"+val+"'";
        return false;
    }
    case F_REQUIRES: {
        int mask=0;
        if(parseIntVal(val,mask)){ t.buildingReqs=mask; return true; }
        // Lista de nombres separada por comas: "Barracks, Smithy"
        size_t p=0;
        while(p<=val.size()){
            size_t c=val.find(',',p);
            if(c==std::string::npos) c=val.size();
            std::string tok=trim(val.substr(p,c-p));
            p=c+1;
            if(tok.empty()) continue;
            bool hit=false;
            for(int i=0;i<5;i++) if(ieq(tok,kBuildingNames[i])){ mask|=(1<<i); hit=true; break; }
            if(!hit){ err="requires: unknown building '"+tok+"'"; return false; }
        }
        t.buildingReqs=mask;
        return true;
    }
    case F_BUILTIN: {
        if(val=="1" || ieq(val,"true") || ieq(val,"yes")){ t.isBuiltin=true; return true; }
        if(val=="0" || ieq(val,"false") || ieq(val,"no")){ t.isBuiltin=false; return true; }
        err="builtin: expected 0|1 got '"+val+"'";
        return false;
    }
    default:
        err=key+": internal error";
        return false;
    }
}

} // namespace

// ───────────────────────────────────────────────────────────────────────────
int loadUnitDataFrom(const char* unitsDir, std::vector<UnitTypeDef>& target,
                     std::vector<std::string>* msgs){
    std::vector<UnitTypeDef> builtins = target;  // base de fallback
    int loaded=0;

    for(int s=0;s<kUnitSlotCount;s++){
        const UnitSlot& slot=kUnitSlots[s];
        std::string path=std::string(unitsDir)+"/"+slot.file;
        FILE* fp=fopen(path.c_str(),"r");
        if(!fp){
            addMsg(msgs,path+": file not found - keeping builtin \""+std::string(slot.builtin)+"\"");
            continue;
        }
        int bi=-1;
        for(int i=0;i<(int)builtins.size();i++)
            if(strcmp(builtins[i].name,slot.builtin)==0){ bi=i; break; }
        if(bi<0){
            fclose(fp);
            addMsg(msgs,path+": builtin \""+std::string(slot.builtin)+"\" not found - skipped");
            continue;
        }

        UnitTypeDef t=builtins[bi];
        bool seen[F_COUNT]={false};
        bool ok=true;
        std::string err;
        int  errLine=0, lastLine=0, lineNo=0;
        char buf[1024];

        while(ok && fgets(buf,(int)sizeof buf,fp)){
            lineNo++; lastLine=lineNo;
            std::string line=trim(cropEol(buf));
            if(line.empty() || line[0]==';' || line[0]=='#') continue;
            if(line[0]=='[') continue;   // cabecera de seccion INI (.unit planos)
            std::string key,val;
            if(!splitKV(line,key,val)){
                ok=false; errLine=lineNo; err="syntax error, expected 'key = value'";
                break;
            }
            if(key=="schema"){
                int sv=0;
                if(!parseIntVal(val,sv)){ ok=false; errLine=lineNo; err="schema: invalid integer '"+val+"'"; break; }
                if(sv!=1){ ok=false; errLine=lineNo; err="schema: unsupported version '"+val+"' (expected 1)"; }
                continue;
            }
            int fi=findField(key);
            if(fi<0){
                addMsg(msgs,path+":"+std::to_string(lineNo)+": unknown key '"+key+"' (ignored)");
                continue;
            }
            if(seen[fi]){ ok=false; errLine=lineNo; err="duplicate key '"+key+"'"; break; }
            if(!applyField(t,fi,key,val,err)){ ok=false; errLine=lineNo; break; }
            seen[fi]=true;
        }
        fclose(fp);

        if(ok){
            for(int i=0;i<F_COUNT;i++){
                if(!seen[i]){
                    ok=false;
                    err="missing key '"+std::string(kFieldKeys[i])+"'";
                    errLine=lastLine>0?lastLine:1;
                    break;
                }
            }
        }
        if(!ok){
            addMsg(msgs,path+":"+std::to_string(errLine)+": "+err+
                   " - keeping builtin \""+std::string(slot.builtin)+"\"");
            continue;
        }
        target[bi]=t;
        loaded++;
    }
    return loaded;
}

int loadUnitData(){
    std::vector<std::string> msgs;
    int n=loadUnitDataFrom(GAMEDATA_UNITS_DIR,g_unitTypes,&msgs);
    for(size_t i=0;i<msgs.size();i++){
        printf("[gamedata] %s\n",msgs[i].c_str());
        TraceLog(LOG_WARNING,"[gamedata] %s",msgs[i].c_str());
        battleLogAdd("[Data] "+msgs[i]);
    }
    if(!msgs.empty())
        printf("[gamedata] %d/%d unidades desde %s (%d en fallback)\n",
               n,kUnitSlotCount,GAMEDATA_UNITS_DIR,kUnitSlotCount-n);
    return n;
}

// ───────────────────────────────────────────────────────────────────────────
//  EDIFICIOS DATA-DRIVEN (D1 paso 2) - data/base/buildings/*.bld
// ───────────────────────────────────────────────────────────────────────────
namespace {

// Tabla de edificios: archivo -> builtin de referencia. Orden = enum
// BuildingType (los saves guardan índices; la sustitución es por nombre en el
// propio slot, nunca reordena).
struct BldSlot { const char* file; const char* builtin; };
const BldSlot kBldSlots[] = {
    {"market.bld",          "Market"},
    {"farm.bld",            "Farm"},
    {"sawmill.bld",         "Sawmill"},
    {"quarry.bld",          "Quarry"},
    {"smithy.bld",          "Smithy"},
    {"barracks.bld",        "Barracks"},
    {"stable.bld",          "Stable"},
    {"archery_range.bld",   "Archery Range"},
    {"armoury_workshop.bld","Armoury Workshop"},
    {"walls_lv1.bld",       "Walls Lv.1"},
    {"walls_lv2.bld",       "Walls Lv.2"},
    {"mage_tower.bld",      "Mage Tower"},
    {"temple.bld",          "Temple"},
    {"port.bld",            "Port"},
    {"watchtower.bld",      "Watchtower"},
};
const int kBldSlotCount = (int)(sizeof kBldSlots / sizeof kBldSlots[0]);

// Campos soportados de BuildingDef (schema se gestiona inline como en units)
enum BldFieldId {
    B_NAME=0, B_COST_GOLD, B_COST_WOOD, B_COST_STONE, B_COST_IRON,
    B_TURNS, B_PREREQ,
    B_YIELD_GOLD, B_YIELD_FOOD, B_YIELD_WOOD, B_YIELD_STONE, B_YIELD_IRON,
    B_FLAGS,
    B_COUNT
};
const char* kBldFieldKeys[B_COUNT] = {
    "name", "cost.gold", "cost.wood", "cost.stone", "cost.iron",
    "turns", "prereq",
    "yield.gold", "yield.food", "yield.wood", "yield.stone", "yield.iron",
    "flags"
};

int findBldField(const std::string& key){
    for(int i=0;i<B_COUNT;i++) if(key==kBldFieldKeys[i]) return i;
    return -1;
}

// Aplica un campo de edificio. Devuelve false con `err` relleno si el valor
// es invalido (en ese caso el edificio entero cae a fallback).
bool applyBldField(BuildingDef& t,int fi,const std::string& key,
                   const std::string& val,std::string& err){
    switch(fi){
    case B_NAME:
        if(val.size()>=sizeof t.name){ err="name: too long (max 31 chars)"; return false; }
        snprintf(t.name,sizeof t.name,"%s",val.c_str());
        return true;
    case B_COST_GOLD:  return setInt(key,val,t.goldCost,err);
    case B_COST_WOOD:  return setInt(key,val,t.woodCost,err);
    case B_COST_STONE: return setInt(key,val,t.stoneCost,err);
    case B_COST_IRON:  return setInt(key,val,t.ironCost,err);
    case B_TURNS:      return setInt(key,val,t.turns,err);
    case B_YIELD_GOLD: return setFloat(key,val,t.goldYield,err);
    case B_YIELD_FOOD: return setFloat(key,val,t.foodYield,err);
    case B_YIELD_WOOD: return setFloat(key,val,t.woodYield,err);
    case B_YIELD_STONE:return setFloat(key,val,t.stoneYield,err);
    case B_YIELD_IRON: return setFloat(key,val,t.ironYield,err);
    case B_PREREQ: {
        int v=0;
        if(!parseIntVal(val,v)){ err="prereq: invalid integer '"+val+"'"; return false; }
        if(v<-1||v>=BLD_COUNT){
            err="prereq: expected -1 or 0.."+std::to_string(BLD_COUNT-1)+" got '"+val+"'";
            return false;
        }
        t.prereq=v;
        return true;
    }
    case B_FLAGS: {
        // Lista por comas estilo units: "none" | "coastal" | "none,coastal"
        t.coastal=false;
        size_t p=0;
        while(p<=val.size()){
            size_t c=val.find(',',p);
            if(c==std::string::npos) c=val.size();
            std::string tok=trim(val.substr(p,c-p));
            p=c+1;
            if(tok.empty()) continue;
            if(ieq(tok,"none")) continue;
            if(ieq(tok,"coastal")){ t.coastal=true; continue; }
            err="flags: unknown flag '"+tok+"' (expected none|coastal)";
            return false;
        }
        return true;
    }
    default:
        err=key+": internal error";
        return false;
    }
}

} // namespace

int loadBuildingDataFrom(const char* buildingsDir, BuildingDef* target,
                         std::vector<std::string>* msgs){
    BuildingDef builtins[BLD_COUNT];
    for(int i=0;i<BLD_COUNT;i++) builtins[i]=target[i];  // base de fallback
    int loaded=0;

    for(int s=0;s<kBldSlotCount;s++){
        const BldSlot& slot=kBldSlots[s];
        std::string path=std::string(buildingsDir)+"/"+slot.file;
        FILE* fp=fopen(path.c_str(),"r");
        if(!fp){
            addMsg(msgs,path+": file not found - keeping builtin \""+std::string(slot.builtin)+"\"");
            continue;
        }
        int bi=-1;
        for(int i=0;i<BLD_COUNT;i++)
            if(strcmp(builtins[i].name,slot.builtin)==0){ bi=i; break; }
        if(bi<0){
            fclose(fp);
            addMsg(msgs,path+": builtin \""+std::string(slot.builtin)+"\" not found - skipped");
            continue;
        }

        BuildingDef t=builtins[bi];
        bool seen[B_COUNT]={false};
        bool ok=true;
        std::string err;
        int  errLine=0, lastLine=0, lineNo=0;
        char buf[1024];

        while(ok && fgets(buf,(int)sizeof buf,fp)){
            lineNo++; lastLine=lineNo;
            std::string line=trim(cropEol(buf));
            if(line.empty() || line[0]==';' || line[0]=='#') continue;
            if(line[0]=='[') continue;   // cabecera de seccion INI (.bld planos)
            std::string key,val;
            if(!splitKV(line,key,val)){
                ok=false; errLine=lineNo; err="syntax error, expected 'key = value'";
                break;
            }
            if(key=="schema"){
                int sv=0;
                if(!parseIntVal(val,sv)){ ok=false; errLine=lineNo; err="schema: invalid integer '"+val+"'"; break; }
                if(sv!=1){ ok=false; errLine=lineNo; err="schema: unsupported version '"+val+"' (expected 1)"; }
                continue;
            }
            int fi=findBldField(key);
            if(fi<0){
                addMsg(msgs,path+":"+std::to_string(lineNo)+": unknown key '"+key+"' (ignored)");
                continue;
            }
            if(seen[fi]){ ok=false; errLine=lineNo; err="duplicate key '"+key+"'"; break; }
            if(!applyBldField(t,fi,key,val,err)){ ok=false; errLine=lineNo; break; }
            seen[fi]=true;
        }
        fclose(fp);

        if(ok){
            for(int i=0;i<B_COUNT;i++){
                if(!seen[i]){
                    ok=false;
                    err="missing key '"+std::string(kBldFieldKeys[i])+"'";
                    errLine=lastLine>0?lastLine:1;
                    break;
                }
            }
        }
        if(!ok){
            addMsg(msgs,path+":"+std::to_string(errLine)+": "+err+
                   " - keeping builtin \""+std::string(slot.builtin)+"\"");
            continue;
        }
        target[bi]=t;
        loaded++;
    }
    return loaded;
}

int loadBuildingData(){
    BuildingDef defs[BLD_COUNT];
    defineBuiltinBuildings(defs);   // golden: la unica fuente de verdad
    std::vector<std::string> msgs;
    int n=loadBuildingDataFrom(GAMEDATA_BUILDINGS_DIR,defs,&msgs);
    applyBuildingDefs(defs);        // activa tablas globales (incluso fallback)
    for(size_t i=0;i<msgs.size();i++){
        printf("[gamedata] %s\n",msgs[i].c_str());
        TraceLog(LOG_WARNING,"[gamedata] %s",msgs[i].c_str());
        battleLogAdd("[Data] "+msgs[i]);
    }
    if(!msgs.empty())
        printf("[gamedata] %d/%d edificios desde %s (%d en fallback)\n",
               n,kBldSlotCount,GAMEDATA_BUILDINGS_DIR,kBldSlotCount-n);
    return n;
}

// ───────────────────────────────────────────────────────────────────────────
//  FORMACIONES DATA-DRIVEN (B1 paso 1) - data/battle/formations.txt
// ───────────────────────────────────────────────────────────────────────────
namespace {

// Claves por seccion: las 7 numericas son OBLIGATORIAS (una sola invalida
// deja la formacion entera en builtin); "desc" es opcional (cosmetico UI).
enum FormFieldId {
    FF_FRONT=0, FF_DEPTH, FF_SPACING, FF_SPEED,
    FF_FLANK, FF_TAKEN_FRONT, FF_TAKEN_PROJ,
    FF_DESC, FF_COUNT
};
const char* kFormKeys[FF_COUNT] = {
    "front.mult", "depth.mult", "spacing.mult", "speed.mult",
    "flank.vuln", "taken.front", "taken.proj",
    "desc"
};

int findFormField(const std::string& key){
    for(int i=0;i<FF_COUNT;i++) if(key==kFormKeys[i]) return i;
    return -1;
}

// Rango de validacion de todos los multiplicadores (defensa contra typos
// tipo "12" o "0.01" que romperian el balance sin ruido).
bool formValOk(float v){ return v>=0.25f&&v<=3.f; }

bool applyFormField(FormationTactic& t,int fi,const std::string& val,std::string& err){
    if(fi==FF_DESC){
        if(val.size()>=sizeof t.desc){ err="desc: too long (max 27 chars)"; return false; }
        snprintf(t.desc,sizeof t.desc,"%s",val.c_str());
        return true;
    }
    float v=0.f;
    if(!parseFloatVal(val,v)){
        err=std::string(kFormKeys[fi])+": invalid number '"+val+"'";
        return false;
    }
    if(!formValOk(v)){
        err=std::string(kFormKeys[fi])+": value "+val+" out of range [0.25..3.00]";
        return false;
    }
    switch(fi){
    case FF_FRONT:       t.frontMult=v;      return true;
    case FF_DEPTH:       t.depthMult=v;      return true;
    case FF_SPACING:     t.spacingMult=v;    return true;
    case FF_SPEED:       t.speedMult=v;      return true;
    case FF_FLANK:       t.flankVuln=v;      return true;
    case FF_TAKEN_FRONT: t.frontTakenMult=v; return true;
    case FF_TAKEN_PROJ:  t.projTakenMult=v;  return true;
    default:
        err=std::string(kFormKeys[fi])+": internal error";
        return false;
    }
}

} // namespace

int loadFormationDataFrom(const char* filePath, FormationTactic target[FORMATION_COUNT],
                          std::vector<std::string>* msgs){
    FormationTactic builtins[FORMATION_COUNT];
    for(int i=0;i<FORMATION_COUNT;i++) builtins[i]=target[i]; // base de fallback

    FILE* fp=fopen(filePath,"r");
    if(!fp){
        addMsg(msgs,std::string(filePath)+": file not found - keeping builtins");
        return 0;
    }

    int loaded=0;
    int committed=0;                      // bitmask de secciones ya aplicadas
    int curIdx=-1;                        // seccion abierta (-1 = fuera)
    bool curOk=true;
    std::string curErr;
    FormationTactic cur{};
    bool seen[FF_COUNT]={false};
    int errLine=0, lastLine=0, lineNo=0;
    bool fileFatal=false;                 // schema invalido => fallback total

    auto loc=[&](int ln){ return std::string(filePath)+":"+std::to_string(ln); };

    auto finishSection=[&](){
        if(curIdx<0) return;
        if(curOk){
            for(int i=0;i<FF_COUNT;i++){
                if(i==FF_DESC) continue;  // desc opcional
                if(!seen[i]){
                    curOk=false;
                    errLine=lastLine>0?lastLine:1;
                    curErr="missing key '"+std::string(kFormKeys[i])+"'";
                    break;
                }
            }
        }
        if(curOk){
            target[curIdx]=cur;
            loaded++;
        }else{
            addMsg(msgs,loc(errLine)+": "+curErr+
                   " - keeping builtin \""+std::string(builtins[curIdx].name)+"\"");
        }
        curIdx=-1;
    };

    char buf[1024];
    while(!fileFatal&&fgets(buf,(int)sizeof buf,fp)){
        lineNo++; lastLine=lineNo;
        std::string line=trim(cropEol(buf));
        if(line.empty()||line[0]==';'||line[0]=='#') continue;

        // Cabecera de seccion: [LINE], [COLUMN], [SQUARE], [LOOSE]
        if(line[0]=='['){
            finishSection();
            size_t closePos=line.find(']');
            std::string nm=trim(line.substr(1,closePos==std::string::npos
                                     ? std::string::npos : closePos-1));
            if(nm.empty()){
                addMsg(msgs,loc(lineNo)+": empty section name - ignored");
                continue;
            }
            int si=-1;
            for(int i=0;i<FORMATION_COUNT;i++) if(ieq(nm,builtins[i].name)){ si=i; break; }
            if(si<0){
                addMsg(msgs,loc(lineNo)+": unknown section '"+nm+"' (ignored)");
                continue;
            }
            if(committed&(1<<si)){
                addMsg(msgs,loc(lineNo)+": duplicate section '"+nm+"' (ignored)");
                continue;
            }
            committed|=(1<<si);
            curIdx=si;
            cur=builtins[si];           // base: claves ausentes conservan builtin
            curOk=true;
            curErr.clear();
            for(int i=0;i<FF_COUNT;i++) seen[i]=false;
            continue;
        }

        std::string key,val;
        if(!splitKV(line,key,val)){
            if(curIdx>=0){
                curOk=false; errLine=lineNo;
                curErr="syntax error, expected 'key = value'";
                curIdx=-1;
            }else addMsg(msgs,loc(lineNo)+": syntax error, expected 'key = value'");
            continue;
        }
        if(key=="schema"){
            if(curIdx>=0){
                addMsg(msgs,loc(lineNo)+": schema must appear before any section");
                continue;
            }
            int sv=0;
            if(!parseIntVal(val,sv)||sv!=1){
                addMsg(msgs,loc(lineNo)+": unsupported schema '"+val+
                       "' (expected 1) - keeping builtins");
                fileFatal=true;
            }
            continue;
        }
        int fi=findFormField(key);
        if(fi<0){
            addMsg(msgs,loc(lineNo)+": unknown key '"+key+"' (ignored)");
            continue;
        }
        if(curIdx<0){
            addMsg(msgs,loc(lineNo)+": key '"+key+"' outside any section (ignored)");
            continue;
        }
        if(seen[fi]){
            curOk=false; errLine=lineNo; curErr="duplicate key '"+key+"'";
            curIdx=-1; continue;
        }
        if(!applyFormField(cur,fi,val,curErr)){ errLine=lineNo; curIdx=-1; continue; }
        seen[fi]=true;
    }
    finishSection();
    fclose(fp);
    return fileFatal?0:loaded;
}

int loadFormationData(){
    FormationTactic defs[FORMATION_COUNT];
    defineBuiltinFormations(defs);   // golden: la unica fuente de verdad
    std::vector<std::string> msgs;
    int n=loadFormationDataFrom(GAMEDATA_FORMATIONS_FILE,defs,&msgs);
    setFormationTactics(defs);       // activa data o fallback (identico a builtins)
    for(size_t i=0;i<msgs.size();i++){
        printf("[gamedata] %s\n",msgs[i].c_str());
        TraceLog(LOG_WARNING,"[gamedata] %s",msgs[i].c_str());
        battleLogAdd("[Data] "+msgs[i]);
    }
    if(!msgs.empty())
        printf("[gamedata] %d/%d formaciones desde %s (%d en fallback)\n",
               n,FORMATION_COUNT,GAMEDATA_FORMATIONS_FILE,FORMATION_COUNT-n);
    return n;
}

// ───────────────────────────────────────────────────────────────────────────
//  SELF-TEST HEADLESS (-datatest) -> 0/1
// ───────────────────────────────────────────────────────────────────────────
namespace {

int compareUnits(const std::vector<UnitTypeDef>& a,const std::vector<UnitTypeDef>& b,
                 const char* tag){
    int diffs=0;
    if(a.size()!=b.size()){
        printf("%s MISMATCH size %d vs %d\n",tag,(int)a.size(),(int)b.size());
        return 1;
    }
    for(int i=0;i<(int)a.size();i++){
        const UnitTypeDef& x=a[i];
        const UnitTypeDef& y=b[i];
        auto d=[&](const char* field){
            printf("%s MISMATCH [%s] %s\n",tag,y.name,field);
            diffs++;
        };
        if(strcmp(x.name,y.name)!=0) d("name");
        if(x.soldierCount!=y.soldierCount) d("soldiers");
        if(x.hpPerSoldier!=y.hpPerSoldier) d("hp");
        if(x.armor!=y.armor) d("armor");
        if(x.speed!=y.speed) d("speed");
        if(x.meleeAttack!=y.meleeAttack) d("melee.attack");
        if(x.meleeDefense!=y.meleeDefense) d("melee.defense");
        if(x.meleeBaseDmg!=y.meleeBaseDmg) d("melee.base");
        if(x.meleeAPDmg!=y.meleeAPDmg) d("melee.ap");
        if(x.meleeInterval!=y.meleeInterval) d("melee.interval");
        if(x.range!=y.range) d("range");
        if(x.missileBaseDmg!=y.missileBaseDmg) d("missile.base");
        if(x.missileAPDmg!=y.missileAPDmg) d("missile.ap");
        if(x.missileReload!=y.missileReload) d("missile.reload");
        if(x.recruitGold!=y.recruitGold) d("recruit.gold");
        if(x.recruitFood!=y.recruitFood) d("recruit.food");
        if(x.recruitIron!=y.recruitIron) d("recruit.iron");
        if(x.recruitTurns!=y.recruitTurns) d("recruit.turns");
        if(x.maintGold!=y.maintGold) d("maint.gold");
        if(x.maintFood!=y.maintFood) d("maint.food");
        if(x.morale!=y.morale) d("morale");
        if(x.r!=y.r||x.g!=y.g||x.b!=y.b) d("color");
        if(x.spriteBase!=y.spriteBase) d("sprite");
        if(x.weaponHint!=y.weaponHint) d("weapon");
        if(x.isBuiltin!=y.isBuiltin) d("builtin");
        if(x.buildingReqs!=y.buildingReqs) d("requires");
        if(strcmp(x.lore,y.lore)!=0) d("lore");
    }
    return diffs;
}

bool hasMsg(const std::vector<std::string>& v,const char* needle){
    for(size_t i=0;i<v.size();i++) if(v[i].find(needle)!=std::string::npos) return true;
    return false;
}

bool writeTextFile(const char* path,const char* body){
    FILE* f=fopen(path,"wb");
    if(!f) return false;
    fputs(body,f);
    fclose(f);
    return true;
}

} // namespace

int runUnitDataTest(){
    int fails=0;
    auto check=[&](const char* name,bool ok){
        printf("%s %s\n",ok?"PASS":"FAIL",name);
        if(!ok) fails++;
    };

    printf("DATATEST: unidades data-driven (%s)\n",GAMEDATA_UNITS_DIR);

    // ── A) data/ presente -> golden identico a los builtins ────────────────
    std::vector<UnitTypeDef> builtins;
    defineBuiltinTypes(builtins);
    std::vector<UnitTypeDef> target=builtins;
    std::vector<std::string> msgs;
    int n=loadUnitDataFrom(GAMEDATA_UNITS_DIR,target,&msgs);
    for(size_t i=0;i<msgs.size();i++) printf("  [A] %s\n",msgs[i].c_str());
    check("A: 10/10 unidades cargadas desde data/",n==kUnitSlotCount);
    check("A: sin avisos (archivos y claves completos)",msgs.empty());
    int diffs=compareUnits(target,builtins,"A");
    check("A: stats identicos a los builtins (golden)",diffs==0);

    // ── B) data/ ausente -> fallback total, mismos valores ─────────────────
    target=builtins; msgs.clear();
    n=loadUnitDataFrom("build/_gamedata_missing",target,&msgs);
    check("B: 0/10 unidades cargadas (directorio inexistente)",n==0);
    check("B: un aviso por cada archivo que falta",(int)msgs.size()==kUnitSlotCount);
    check("B: avisos con archivo:linea de fallback",
          (int)msgs.size()==kUnitSlotCount && hasMsg(msgs,"file not found"));
    diffs=compareUnits(target,builtins,"B");
    check("B: valores identicos a builtins (fallback)",diffs==0);

    // ── C) archivo invalido -> error legible + fallback, sin crash ────────
    _mkdir("build");
    _mkdir("build/_gamedata_bad");
    const char* bad1="build/_gamedata_bad/spear_levy.unit";
    const char* bad2="build/_gamedata_bad/axe_militia.unit";
    writeTextFile(bad1,
        "schema = 1\n"
        "name = Spear Levy\n"
        "soldiers = 60\n"
        "speed = abc\n");
    writeTextFile(bad2,
        "schema = 1\n"
        "name = Axe Militia\n"
        "soldiers = 60\n");
    target=builtins; msgs.clear();
    n=loadUnitDataFrom("build/_gamedata_bad",target,&msgs);
    for(size_t i=0;i<msgs.size();i++) printf("  [C] %s\n",msgs[i].c_str());
    check("C: 0/10 aplicadas (los archivos validos no alcanzan)",n==0);
    check("C: valor invalido con archivo:linea",
          hasMsg(msgs,"spear_levy.unit:4:"));
    check("C: clave ausente con archivo:linea",
          hasMsg(msgs,"axe_militia.unit:3: missing key 'hp'"));
    diffs=compareUnits(target,builtins,"C");
    check("C: sin cambios parciales (fallback completo)",diffs==0);
    remove(bad1);
    remove(bad2);

    printf("----\n");
    printf("DATATEST %s\n",fails==0?"PASS":"FAIL");
    return fails==0?0:1;
}

// ───────────────────────────────────────────────────────────────────────────
//  SELF-TEST HEADLESS DE EDIFICIOS (-datatest seccion buildings) -> 0/1
// ───────────────────────────────────────────────────────────────────────────
namespace {

int compareBuildings(const BuildingDef* a,const BuildingDef* b,const char* tag){
    int diffs=0;
    for(int i=0;i<BLD_COUNT;i++){
        auto d=[&](const char* field){
            printf("%s MISMATCH [%s] %s\n",tag,b[i].name,field);
            diffs++;
        };
        if(strcmp(a[i].name,b[i].name)!=0) d("name");
        if(a[i].goldCost!=b[i].goldCost)   d("cost.gold");
        if(a[i].woodCost!=b[i].woodCost)   d("cost.wood");
        if(a[i].stoneCost!=b[i].stoneCost) d("cost.stone");
        if(a[i].ironCost!=b[i].ironCost)   d("cost.iron");
        if(a[i].turns!=b[i].turns)         d("turns");
        if(a[i].prereq!=b[i].prereq)       d("prereq");
        if(a[i].goldYield!=b[i].goldYield) d("yield.gold");
        if(a[i].foodYield!=b[i].foodYield) d("yield.food");
        if(a[i].woodYield!=b[i].woodYield) d("yield.wood");
        if(a[i].stoneYield!=b[i].stoneYield) d("yield.stone");
        if(a[i].ironYield!=b[i].ironYield) d("yield.iron");
        if(a[i].coastal!=b[i].coastal)     d("flags.coastal");
    }
    return diffs;
}

} // namespace

int runBuildingDataTest(){
    int fails=0;
    auto check=[&](const char* name,bool ok){
        printf("%s %s\n",ok?"PASS":"FAIL",name);
        if(!ok) fails++;
    };

    printf("BLDDATATEST: edificios data-driven (%s)\n",GAMEDATA_BUILDINGS_DIR);

    BuildingDef builtins[BLD_COUNT];
    BuildingDef target[BLD_COUNT];
    std::vector<std::string> msgs;
    int n;

    // ── A) data/ presente -> 15/15, golden campo a campo + orden ───────────
    defineBuiltinBuildings(builtins);
    for(int i=0;i<BLD_COUNT;i++) target[i]=builtins[i];
    n=loadBuildingDataFrom(GAMEDATA_BUILDINGS_DIR,target,&msgs);
    for(size_t i=0;i<msgs.size();i++) printf("  [A] %s\n",msgs[i].c_str());
    check("A: 15/15 edificios cargados desde data/",n==BLD_COUNT);
    check("A: sin avisos (archivos y claves completos)",msgs.empty());
    int diffs=compareBuildings(target,builtins,"A");
    check("A: valores identicos a los builtins (golden)",diffs==0);
    check("A: orden de slots preservado (enum BuildingType)",
          strcmp(target[0].name,"Market")==0 &&
          strcmp(target[13].name,"Port")==0 &&
          strcmp(target[14].name,"Watchtower")==0);

    // ── B) data/ ausente -> fallback total, mismos valores ─────────────────
    for(int i=0;i<BLD_COUNT;i++) target[i]=builtins[i];
    msgs.clear();
    n=loadBuildingDataFrom("build/_gamedata_missing",target,&msgs);
    check("B: 0/15 edificios cargados (directorio inexistente)",n==0);
    check("B: un aviso por cada archivo que falta",(int)msgs.size()==BLD_COUNT);
    check("B: avisos con fallback legible",
          (int)msgs.size()==BLD_COUNT && hasMsg(msgs,"file not found"));
    diffs=compareBuildings(target,builtins,"B");
    check("B: valores identicos a builtins (fallback)",diffs==0);

    // ── C) archivo invalido -> error legible + fallback, sin crash ────────
    _mkdir("build");
    _mkdir("build/_gamedata_badb");
    const char* bad1="build/_gamedata_badb/market.bld";
    const char* bad2="build/_gamedata_badb/farm.bld";
    writeTextFile(bad1,
        "schema = 1\n"
        "name = Market\n"
        "cost.gold = 200\n"
        "turns = abc\n");
    writeTextFile(bad2,
        "schema = 1\n"
        "name = Farm\n"
        "cost.gold = 120\n");
    for(int i=0;i<BLD_COUNT;i++) target[i]=builtins[i];
    msgs.clear();
    n=loadBuildingDataFrom("build/_gamedata_badb",target,&msgs);
    for(size_t i=0;i<msgs.size();i++) printf("  [C] %s\n",msgs[i].c_str());
    check("C: 0/15 aplicadas (los archivos validos no alcanzan)",n==0);
    check("C: valor invalido con archivo:linea",
          hasMsg(msgs,"market.bld:4:"));
    check("C: clave ausente con archivo:linea",
          hasMsg(msgs,"farm.bld:3: missing key 'cost.wood'"));
    diffs=compareBuildings(target,builtins,"C");
    check("C: sin cambios parciales (fallback completo)",diffs==0);
    remove(bad1);
    remove(bad2);

    printf("----\n");
    printf("BLDDATATEST %s\n",fails==0?"PASS":"FAIL");
    return fails==0?0:1;
}
