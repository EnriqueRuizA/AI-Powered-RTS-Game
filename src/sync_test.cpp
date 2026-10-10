// src/sync_test.cpp — Fase H: self-test de sincronización de campaña
// Separado de net.cpp porque incluye campaign.h (-> raylib.h), que no puede
// coexistir con windows.h (vía winsock2) en la misma unidad de compilación.
// Usa solo la API opaca de net.h (NetSock/NetMsg/NetSession) y sleep por
// <thread> (sin windows.h).
#include "campaign.h"
#include "save.h"
#include "net.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>

static void syncSleepMs(int ms){
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Construye una CampaignState a mano, la serializa, la envía troceada por
// loopback, la reensambla (por la API real de sesión, en rol cliente), la
// deserializa y comprueba campos clave. También prueba el canal de comandos
// (host recibe NET_MSG_CMD y lo encola) y el broadcast de snapshot del host.
int runSyncTest(){
    printf("SYNC: snapshot de campana + comandos (loopback)\n");

    // 1) Estado de prueba (sin disco ni raylib): 3 provincias, 1 ejercito,
    //    cola de reclutamiento, diplomacia y recursos no default.
    CampaignState st;
    st.campaignId=0;
    st.turn=7;
    st.res.gold=1234.f; st.res.food=80.f; st.res.wood=40.f;
    st.res.stone=30.f; st.res.iron=15.f;
    st.playerProvince=1;
    st.pendingBattleProvince=-1;
    st.viewedCity=-1;
    auto mkProv=[&](const char* name,int own,TerrainType ter,bool city){
        Province p{};
        snprintf(p.name,sizeof p.name,"%s",name);
        p.nx=0.5f; p.ny=0.5f;
        p.terrain=ter; p.owner=(FactionId)own; p.hasCity=city;
        if(city){
            snprintf(p.city.name,sizeof p.city.name,"%s City",name);
            p.city.built[BLD_MARKET]=true;
            p.city.built[BLD_FARM]=true;
            p.city.constructing=-1;
            p.city.constructTurns=0;
            p.city.defBonus=1.5f;
        }
        return p;
    };
    Province p0=mkProv("Netford",FACTION_PLAYER,TERRAIN_PLAIN,true);
    p0.adjacent={1};
    p0.army={{0,50}};
    Province p1=mkProv("Linkton",FACTION_PLAYER,TERRAIN_FOREST,false);
    p1.adjacent={0,2};
    Province p2=mkProv("Socket Keep",FACTION_AGGRESSIVE,TERRAIN_MOUNTAIN,true);
    p2.adjacent={1};
    p2.army={{1,30}};
    st.provinces={p0,p1,p2};
    st.recruitQueue.push_back({2,3,5});
    st.readyUnits={{0,50}};
    st.playerArmy={{0,50}};
    FieldArmy fa;
    fa.id=9; fa.owner=FACTION_PLAYER; fa.province=1; fa.moved=true;
    fa.units={{0,40},{3,10}};
    st.armies.push_back(fa);
    st.reserve.push_back({1,20});
    st.allied[FACTION_DEFENSIVE]=true;
    st.tradePact[FACTION_COMMERCIAL]=true;
    st.battlesWon=3; st.battlesLost=1; st.peakProvinces=2;

    std::vector<char> blob;
    if(!serializeCampaignState(st,blob)){
        printf("SYNC FAIL - serializeCampaignState\n");
        return 1;
    }
    printf("SYNC: blob serializado = %d bytes\n",(int)blob.size());
    if(blob.size()>NET_SNAP_MAX){
        printf("SYNC FAIL - blob %d > NET_SNAP_MAX\n",(int)blob.size());
        return 1;
    }

    if(!netInit()){
        printf("SYNC FAIL - netInit\n");
        return 1;
    }

    // 2) Camino cliente: host crudo en :7777 responde WELCOME y envia el
    //    snapshot troceado; el NetSession real (rol cliente) lo ensambla.
    bool ok=true;
    {
        unsigned short hostPort=0, cliPort=0;
        NetSock host=netOpen(NET_PORT,&hostPort);
        if(host==NET_INVALID){
            printf("SYNC FAIL - bind host :%d\n",NET_PORT);
            netShutdown();
            return 1;
        }
        if(!netSessionClientJoin("127.0.0.1")){
            printf("SYNC FAIL - clientJoin (%s)\n",g_netSession.status);
            netClose(host); netSessionStop(); netShutdown();
            return 1;
        }
        // Esperar JOIN y responder WELCOME al puerto del cliente
        bool welcomed=false;
        for(int i=0;i<100&&!welcomed;i++){
            NetMsg r={};
            char srcIp[NET_NAME_LEN]={};
            unsigned short srcPort=0;
            int rc=netRecvMsgFrom(host,&r,50,srcIp,(int)sizeof srcIp,&srcPort);
            if(rc==1&&r.type==NET_MSG_JOIN){
                cliPort=srcPort;
                NetMsg w={};
                w.type=NET_MSG_WELCOME;
                w.seq=1;
                snprintf(w.payload,sizeof w.payload,"v%d",(int)NET_PROTO_VERSION);
                netSendMsg(host,"127.0.0.1",srcPort,w);
                welcomed=true;
            }
            netSessionPoll();
        }
        if(!welcomed){
            printf("SYNC FAIL - host crudo no vio JOIN\n");
            ok=false;
        }else{
            for(int i=0;i<100&&!g_netSession.connected;i++){ netSessionPoll(); syncSleepMs(10); }
            if(!g_netSession.connected){
                printf("SYNC FAIL - cliente no conecto\n");
                ok=false;
            }
        }
        if(ok){
            printf("SYNC: cliente conectado (cli:%u)\n",(unsigned)cliPort);
            if(!netSendSnapshot(host,"127.0.0.1",cliPort,blob.data(),(uint32_t)blob.size())){
                printf("SYNC FAIL - netSendSnapshot\n");
                ok=false;
            }
        }
        for(int i=0;i<200&&ok&&!g_netSession.snapReady;i++){
            netSessionPoll();
            syncSleepMs(10);
        }
        if(ok&&!g_netSession.snapReady){
            printf("SYNC FAIL - snapReady no llego (recv=%u/%u)\n",
                   g_netSession.snapRecv,g_netSession.snapSize);
            ok=false;
        }
        if(ok){
            if(g_netSession.snapSize!=blob.size()||
               memcmp(g_netSession.snapBuf,blob.data(),blob.size())!=0){
                printf("SYNC FAIL - snapshot ensamblado no coincide (%u vs %d)\n",
                       g_netSession.snapSize,(int)blob.size());
                ok=false;
            }else{
                printf("SYNC: ensamblado correcto (%u bytes, %d trozos)\n",
                       g_netSession.snapSize,(int)((blob.size()+NET_SNAP_CHUNK-1)/NET_SNAP_CHUNK));
            }
        }
        // Deserializar el snapshot recibido y verificar campos clave
        if(ok){
            CampaignState got;
            if(!deserializeCampaignState(got,g_netSession.snapBuf,g_netSession.snapSize)){
                printf("SYNC FAIL - deserializeCampaignState\n");
                ok=false;
            }else if(got.turn!=7||got.res.gold!=1234.f||(int)got.provinces.size()!=3||
                     strcmp(got.provinces[0].name,"Netford")!=0||
                     !got.provinces[0].hasCity||
                     got.provinces[0].city.defBonus!=1.5f||
                     (int)got.armies.size()!=1||got.armies[0].id!=9||!got.armies[0].moved||
                     (int)got.armies[0].units.size()!=2||
                     (int)got.reserve.size()!=1||
                     (int)got.recruitQueue.size()!=1||got.recruitQueue[0].turnsLeft!=3||
                     !got.allied[FACTION_DEFENSIVE]||!got.tradePact[FACTION_COMMERCIAL]||
                     got.playerProvince!=1){
                printf("SYNC FAIL - campos del snapshot no coinciden\n");
                ok=false;
            }else{
                printf("SYNC: deserializacion verificada (turn=%d gold=%.0f provs=%d armies=%d)\n",
                       got.turn,got.res.gold,(int)got.provinces.size(),(int)got.armies.size());
            }
            netSessionConsumeSnapshot();
            if(g_netSession.snapReady){ printf("SYNC FAIL - consumeSnapshot\n"); ok=false; }
        }
        netSessionStop();
        netClose(host);
    }

    // 3) Camino host: sesion real como host, JOIN crudo del cliente, comando
    //    NET_MSG_CMD encolado y broadcast de snapshot recibido por el cliente.
    if(ok){
        unsigned short cliPort=0;
        NetSock cli=netOpen(0,&cliPort);
        if(cli==NET_INVALID){
            printf("SYNC FAIL - socket cliente host-test\n");
            netShutdown();
            return 1;
        }
        if(!netSessionHostStart()){
            printf("SYNC FAIL - hostStart (%s)\n",g_netSession.status);
            netClose(cli); netShutdown();
            return 1;
        }
        NetMsg j={};
        j.type=NET_MSG_JOIN; j.seq=1;
        snprintf(j.payload,sizeof j.payload,"join");
        if(!netSendMsg(cli,"127.0.0.1",(unsigned short)NET_PORT,j)){
            printf("SYNC FAIL - send JOIN (host-test)\n");
            ok=false;
        }
        for(int i=0;i<100&&ok&&g_netSession.playerCount<2;i++){ netSessionPoll(); syncSleepMs(10); }
        if(ok&&g_netSession.playerCount<2){
            printf("SYNC FAIL - host no registro cliente\n");
            ok=false;
        }
        // Comando del cliente: NET_CMD_RECRUIT(prov=2,type=5).
        // La sesion de este bloque es HOST: netSessionSendCmd solo vale en
        // rol CLIENT, asi que el cliente crudo empaqueta el NetMsg a mano
        // (mismo layout que netSessionSendCmd: cmd@0, a@1, b@5, c@9).
        if(ok){
            NetMsg cm={};
            cm.type=NET_MSG_CMD; cm.seq=2;
            cm.payload[0]=(char)NET_CMD_RECRUIT;
            int a=2,b=5,c=0;
            memcpy(cm.payload+1,&a,4);
            memcpy(cm.payload+5,&b,4);
            memcpy(cm.payload+9,&c,4);
            if(!netSendMsg(cli,"127.0.0.1",(unsigned short)NET_PORT,cm)){
                printf("SYNC FAIL - send CMD (host-test)\n");
                ok=false;
            }
        }
        NetCmd got={};
        for(int i=0;i<100&&ok&&got.cmd==0;i++){
            netSessionPoll();
            if(netSessionTakeCmd(&got,1)==0) syncSleepMs(10);
        }
        if(ok){
            if(got.cmd!=NET_CMD_RECRUIT||got.a!=2||got.b!=5){
                printf("SYNC FAIL - comando recibido (cmd=%u a=%d b=%d)\n",
                       (unsigned)got.cmd,got.a,got.b);
                ok=false;
            }else{
                printf("SYNC: comando encolado correcto (RECRUIT prov=2 type=5)\n");
            }
        }
        // Broadcast del host -> cliente crudo reensambla
        if(ok&&!netSessionBroadcastSnapshot(blob.data(),(uint32_t)blob.size())){
            printf("SYNC FAIL - broadcastSnapshot\n");
            ok=false;
        }
        if(ok){
            std::vector<char> rb(blob.size());
            uint32_t recv=0;
            for(int i=0;i<200&&ok&&recv<blob.size();i++){
                NetSnapMsg sm={};
                int rc=netRecvSnapMsg(cli,&sm,10);
                if(rc==1){
                    if(sm.total!=blob.size()||sm.offset!=recv||sm.len==0){
                        printf("SYNC FAIL - trozo broadcast (off=%u len=%u total=%u)\n",
                               sm.offset,(unsigned)sm.len,sm.total);
                        ok=false;
                        break;
                    }
                    memcpy(rb.data()+sm.offset,sm.data,sm.len);
                    recv+=sm.len;
                }
            }
            if(ok&&recv!=blob.size()){
                printf("SYNC FAIL - broadcast incompleto (%u/%d)\n",recv,(int)blob.size());
                ok=false;
            }
            if(ok&&memcmp(rb.data(),blob.data(),blob.size())!=0){
                printf("SYNC FAIL - broadcast no coincide\n");
                ok=false;
            }
            if(ok) printf("SYNC: broadcast verificado (%d bytes)\n",(int)blob.size());
        }
        netClose(cli);
        netSessionStop();
    }

    // 5) PvP: al hacer JOIN con pvpMode, el host asigna faccion y la
    //    comunica en el WELCOME ("v1 f<N>"); el cliente la parsea.
    if(ok){
        g_netSession.pvpMode=true;
        if(!netSessionHostStart()){
            printf("SYNC FAIL - hostStart PvP (%s)\n",g_netSession.status);
            g_netSession.pvpMode=false;
            netShutdown();
            return 1;
        }
        unsigned short cliPort2=0;
        NetSock cli2=netOpen(0,&cliPort2);
        if(cli2==NET_INVALID){
            printf("SYNC FAIL - socket cliente PvP\n");
            g_netSession.pvpMode=false;
            netSessionStop(); netShutdown();
            return 1;
        }
        NetMsg j2={};
        j2.type=NET_MSG_JOIN; j2.seq=1;
        snprintf(j2.payload,sizeof j2.payload,"join");
        if(!netSendMsg(cli2,"127.0.0.1",(unsigned short)NET_PORT,j2)){
            printf("SYNC FAIL - send JOIN PvP\n");
            ok=false;
        }
        for(int i=0;i<100&&ok&&g_netSession.playerCount<2;i++){ netSessionPoll(); syncSleepMs(10); }
        if(ok&&g_netSession.playerCount<2){
            printf("SYNC FAIL - PvP: host no registro cliente\n");
            ok=false;
        }
        // Cliente crudo recibe WELCOME y parsea la faccion asignada
        int gotFaction=-1;
        for(int i=0;i<100&&ok&&gotFaction<0;i++){
            NetMsg wm={};
            if(netRecvMsg(cli2,&wm,10)==1&&wm.type==NET_MSG_WELCOME){
                int vf=0,ff=-1;
                if(sscanf(wm.payload,"v%d f%d",&vf,&ff)==2&&ff>=0&&ff<NET_FACTION_COUNT)
                    gotFaction=ff;
                else{
                    printf("SYNC FAIL - PvP: WELCOME sin faccion (payload='%s')\n",wm.payload);
                    ok=false;
                }
            }else syncSleepMs(10);
        }
        if(ok&&gotFaction<0){
            printf("SYNC FAIL - PvP: sin WELCOME/faccion\n");
            ok=false;
        }else if(ok){
            printf("SYNC: PvP faccion asignada = %d\n",gotFaction);
        }
        netClose(cli2);
        netSessionStop();
        g_netSession.pvpMode=false; // restaurar (co-op por defecto)
    }

    netShutdown();
    if(ok){
        printf("SYNC PASS\n");
        return 0;
    }
    printf("SYNC FAIL\n");
    return 1;
}
