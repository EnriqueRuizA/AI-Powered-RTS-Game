#include "save.h"
#include "config.h"
#include "util.h"
#include "ui.h"
#include "campaign.h"
#include "elements.h"
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

const char* getCampaignSavePath(int id){
    static char buf[32];
    if(id<0) id=0; if(id>=MAX_CAMPAIGNS) id=MAX_CAMPAIGNS-1;
    sprintf(buf, "campaign_%d.dat", id);
    return buf;
}
//  SAVE / LOAD — Campaign persistence per campaign file
// ═══════════════════════════════════════════════════════════════════════════
// Fase H: la escritura campo a campo vive ahora en serializeCampaignState
// (buffer en memoria); saveGame solo vuelca ese buffer a disco. El formato
// binario es IDENTICO al anterior (SAVE_VERSION=8), asi que los saves viejos
// siguen cargando.

static void bw(std::vector<char>& out, const void* p, size_t n){
    const char* c=(const char*)p;
    out.insert(out.end(),c,c+n);
}
template <typename T>
static void bw1(std::vector<char>& out, const T& v){ bw(out,&v,sizeof v); }

bool serializeCampaignState(const CampaignState& st, std::vector<char>& out){
    out.clear();
    bw1(out,SAVE_VERSION);
    bw1(out,st.campaignId);
    bw1(out,st.turn);
    bw1(out,st.res);
    bw1(out,st.playerProvince);
    bw1(out,st.pendingBattleProvince);
    bw1(out,st.pendingBattleIsDefense);
    bw1(out,st.viewedCity);

    int np=(int)st.provinces.size();
    bw1(out,np);
    for(const auto& p:st.provinces){
        bw(out,p.name,32);
        bw1(out,p.center.x);
        bw1(out,p.center.y);
        bw1(out,p.nx);
        bw1(out,p.ny);
        int ter=(int)p.terrain; bw1(out,ter);
        int own=(int)p.owner;   bw1(out,own);
        bw1(out,p.hasCity);
        if(p.hasCity){
            bw(out,p.city.name,32);
            bw(out,p.city.built,sizeof(bool)*BLD_COUNT);
            bw1(out,p.city.constructing);
            bw1(out,p.city.constructTurns);
            bw1(out,p.city.defBonus);
        }
        int nadj=(int)p.adjacent.size();
        bw1(out,nadj);
        for(int a : p.adjacent) bw1(out,a);
        int narmy=(int)p.army.size();
        bw1(out,narmy);
        for(const auto& ap : p.army){ bw1(out,ap.first); bw1(out,ap.second); }
    }

    int nq=(int)st.recruitQueue.size();
    bw1(out,nq);
    for(const auto& e:st.recruitQueue){
        bw1(out,e.typeIdx);
        bw1(out,e.turnsLeft);
    }

    int na=(int)st.playerArmy.size();
    bw1(out,na);
    for(const auto& a:st.playerArmy){ bw1(out,a.first); bw1(out,a.second); }

    int nr=(int)st.readyUnits.size();
    bw1(out,nr);
    for(const auto& r:st.readyUnits){ bw1(out,r.first); bw1(out,r.second); }

    // Fase J: ejércitos de campo + reserva
    int nA=(int)st.armies.size();
    bw1(out,nA);
    for(const auto& a:st.armies){
        int id=a.id, prov=a.province, own=(int)a.owner;
        unsigned char mv=a.moved?1:0;
        bw1(out,id); bw1(out,own); bw1(out,prov); bw1(out,mv);
        int nu=(int)a.units.size();
        bw1(out,nu);
        for(const auto& u:a.units){ bw1(out,u.first); bw1(out,u.second); }
    }
    int nR=(int)st.reserve.size();
    bw1(out,nR);
    for(const auto& r:st.reserve){ bw1(out,r.first); bw1(out,r.second); }

    // Fase I: diplomacia — alianzas militares + pactos comerciales
    bw(out,st.allied,sizeof(bool)*FACTION_COUNT);
    bw(out,st.tradePact,sizeof(bool)*FACTION_COUNT);
    // PvP (v9): dos reinos separados
    unsigned char pvp=st.pvpMode?1:0;
    int cf=(int)st.clientFaction;
    bw1(out,pvp);
    bw1(out,cf);
    bw1(out,st.clientRes);
    // PvP (v10): reserva por facción — clientReserve + faccion de la cola
    // (al FINAL del stream para mantener compat de lectura con v9)
    int nCR=(int)st.clientReserve.size();
    bw1(out,nCR);
    for(const auto& r:st.clientReserve){ bw1(out,r.first); bw1(out,r.second); }
    int nqF=(int)st.recruitQueue.size();
    bw1(out,nqF);
    for(const auto& e:st.recruitQueue) bw1(out,e.faction);
    return true;
}

// Lector de buffer con validación de límites (mismos límites que loadGame)
namespace {
struct BufReader {
    const char* p; size_t len, off=0; bool ok=true;
    void r(void* d, size_t n){
        if(!ok||off+n>len){ ok=false; memset(d,0,n); return; }
        memcpy(d,p+off,n); off+=n;
    }
    template <typename T> T r1(){ T v{}; r(&v,sizeof v); return v; }
};
}

bool deserializeCampaignState(CampaignState& st, const char* data, size_t size){
    if(!data||size<sizeof(int)) return false;
    BufReader b{data,size};
    int ver=b.r1<int>();
    // PvP: aceptar v8 (defaults PvP), v9 (campos PvP) y v10 (reserva por facción)
    if(!b.ok||(ver!=SAVE_VERSION&&ver!=8&&ver!=9)) return false;

    st.campaignId=b.r1<int>();
    st.turn=b.r1<int>();
    st.res=b.r1<Resources>();
    st.playerProvince=b.r1<int>();
    st.pendingBattleProvince=b.r1<int>();
    st.pendingBattleIsDefense=b.r1<bool>();
    st.viewedCity=b.r1<int>();

    int np=b.r1<int>();
    if(!b.ok||np<=0||np>256) return false;
    st.provinces.clear();
    st.provinces.resize(np);
    for(int i=0;i<np;i++){
        Province& p=st.provinces[i];
        b.r(p.name,32);
        p.center.x=b.r1<float>();
        p.center.y=b.r1<float>();
        p.nx=b.r1<float>();
        p.ny=b.r1<float>();
        int ter=b.r1<int>(), own=b.r1<int>();
        p.terrain=(TerrainType)ter;
        p.owner=(FactionId)own;
        p.hasCity=b.r1<bool>();
        if(p.hasCity){
            b.r(p.city.name,32);
            b.r(p.city.built,sizeof(bool)*BLD_COUNT);
            p.city.constructing=b.r1<int>();
            p.city.constructTurns=b.r1<int>();
            p.city.defBonus=b.r1<float>();
        }
        int nadj=b.r1<int>();
        if(!b.ok||nadj<0||nadj>64) return false;
        p.adjacent.clear();
        for(int j=0;j<nadj;j++){ int a=b.r1<int>(); p.adjacent.push_back(a); }
        int narmy=b.r1<int>();
        if(!b.ok||narmy<0||narmy>128) return false;
        p.army.clear();
        for(int j=0;j<narmy;j++){
            int ti=b.r1<int>(), cnt=b.r1<int>();
            p.army.push_back({ti,cnt});
        }
        if(!b.ok) return false;
    }

    int nq=b.r1<int>();
    if(!b.ok||nq<0||nq>256) return false;
    st.recruitQueue.clear();
    for(int i=0;i<nq;i++){
        int ti=b.r1<int>(), tl=b.r1<int>();
        st.recruitQueue.push_back({ti,tl,tl});
        if(!b.ok) return false;
    }

    int na=b.r1<int>();
    if(!b.ok||na<0||na>256) return false;
    st.playerArmy.clear();
    for(int i=0;i<na;i++){
        int ti=b.r1<int>(), cnt=b.r1<int>();
        st.playerArmy.push_back({ti,cnt});
        if(!b.ok) return false;
    }

    int nr=b.r1<int>();
    if(!b.ok||nr<0||nr>256) return false;
    st.readyUnits.clear();
    for(int i=0;i<nr;i++){
        int ti=b.r1<int>(), cnt=b.r1<int>();
        st.readyUnits.push_back({ti,cnt});
        if(!b.ok) return false;
    }

    // Fase J: ejércitos de campo + reserva
    st.armies.clear();
    st.reserve.clear();
    st.nextArmyId=1;
    int nA=b.r1<int>();
    if(!b.ok||nA<0||nA>64) return false;
    for(int i=0;i<nA;i++){
        FieldArmy a;
        int id=b.r1<int>(), own=b.r1<int>(), prov=b.r1<int>();
        unsigned char mv=b.r1<unsigned char>();
        int nu=b.r1<int>();
        if(!b.ok||nu<0||nu>256) return false;
        a.id=id; a.owner=(FactionId)own; a.province=prov; a.moved=(mv!=0);
        for(int j=0;j<nu;j++){
            int ti=b.r1<int>(), cnt=b.r1<int>();
            a.units.push_back({ti,cnt});
            if(!b.ok) return false;
        }
        st.armies.push_back(a);
        if(id>=st.nextArmyId) st.nextArmyId=id+1;
    }
    int nR=b.r1<int>();
    if(!b.ok||nR<0||nR>512) return false;
    for(int i=0;i<nR;i++){
        int ti=b.r1<int>(), cnt=b.r1<int>();
        st.reserve.push_back({ti,cnt});
        if(!b.ok) return false;
    }

    // Fase I: diplomacia — alianzas militares + pactos comerciales
    b.r(st.allied,sizeof(bool)*FACTION_COUNT);
    b.r(st.tradePact,sizeof(bool)*FACTION_COUNT);
    // PvP (v9): dos reinos separados (v8 = defaults: co-op)
    if(ver==9||ver==10){
        unsigned char pvp=b.r1<unsigned char>();
        int cf=b.r1<int>();
        st.pvpMode=(pvp!=0);
        st.clientFaction=(FactionId)cf;
        st.clientRes=b.r1<Resources>();
    } else {
        st.pvpMode=false;
        st.clientFaction=FACTION_AGGRESSIVE;
        st.clientRes=Resources();
    }
    // PvP (v10): reserva por facción — clientReserve + faccion de la cola
    // (v9 u older = defaults: reserva común, facciones de la cola = PLAYER)
    if(ver==10){
        int nCR=b.r1<int>();
        if(!b.ok||nCR<0||nCR>512) return false;
        for(int i=0;i<nCR;i++){
            int ti=b.r1<int>(), cnt=b.r1<int>();
            st.clientReserve.push_back({ti,cnt});
            if(!b.ok) return false;
        }
        int nqF=b.r1<int>();
        if(!b.ok||nqF<0||nqF>512) return false;
        if(nqF!=(int)st.recruitQueue.size()) return false; // longitud debe casar
        for(int i=0;i<nqF;i++){
            st.recruitQueue[i].faction=b.r1<int>();
            if(!b.ok) return false;
        }
    } else {
        st.clientReserve.clear();
        for(auto& e:st.recruitQueue) e.faction=FACTION_PLAYER;
    }
    return b.ok;
}

bool saveGame(){
    const char* path=getCampaignSavePath(g_campaign.campaignId);
    std::vector<char> blob;
    if(!serializeCampaignState(g_campaign,blob)) return false;
    FILE* f=fopen(path,"wb");
    if(!f) return false;
    size_t written=fwrite(blob.data(),1,blob.size(),f);
    fclose(f);
    if(written!=blob.size()) return false;
    g_hasSave=true;
    // Remember last played campaign for Continue
    FILE* lf=fopen(CAMPAIGN_LAST_FILE,"wb");
    if(lf){ fwrite(&g_campaign.campaignId,sizeof(int),1,lf); fclose(lf); }
    return true;
}

bool loadGame(){
    int loadId=0;
    FILE* lf=fopen(CAMPAIGN_LAST_FILE,"rb");
    if(lf&&fread(&loadId,sizeof(int),1,lf)==1){ fclose(lf); }
    else if(lf) fclose(lf);
    if(loadId<0||loadId>=MAX_CAMPAIGNS) loadId=0;

    const char* path=getCampaignSavePath(loadId);
    FILE* f=fopen(path,"rb");
    if(!f) return false;
    fseek(f,0,SEEK_END);
    long sz=ftell(f);
    fseek(f,0,SEEK_SET);
    if(sz<=0){ fclose(f); return false; }
    std::vector<char> blob((size_t)sz);
    if(fread(blob.data(),1,(size_t)sz,f)!=(size_t)sz){ fclose(f); return false; }
    fclose(f);
    if(!deserializeCampaignState(g_campaign,blob.data(),blob.size())) return false;

    updateProvinceCenters();
    g_campaign.playerArmy = g_campaign.readyUnits; // 1.4: sync after load
    // Fase J: readyUnits es una vista del ejército seleccionado
    if(!g_campaign.armies.empty()) selectArmy(0);
    else { g_campaign.selectedArmy=-1; }
    g_campaign.playerArmy = g_campaign.readyUnits;
    return true;
}

// Fase H: aplicar un snapshot de red al estado local (mismo post-proceso que
// loadGame, pero sin tocar disco). Devuelve false si el blob es invalido.
bool applyCampaignSnapshot(const char* data, size_t size){
    if(!deserializeCampaignState(g_campaign,data,size)) return false;
    updateProvinceCenters();
    if(!g_campaign.armies.empty()){
        if(g_campaign.selectedArmy<0||g_campaign.selectedArmy>=(int)g_campaign.armies.size())
            g_campaign.selectedArmy=0;
        selectArmy(g_campaign.selectedArmy);
    } else {
        g_campaign.selectedArmy=-1;
        g_campaign.playerArmy=g_campaign.readyUnits;
    }
    // Cancelar cualquier animación de movimiento en curso
    g_campaign.armyMoveFrom=-1;
    g_campaign.armyMoveTo=-1;
    g_campaign.armyMoveIdx=-1;
    g_campaign.armyMoveT=0.f;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
