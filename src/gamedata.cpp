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
