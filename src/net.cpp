// src/net.cpp — Base de red UDP (Fase E)
// Winsock2 se incluye PRIMERO (antes de raylib/windows.h) para evitar el
// conflicto clasico winsock2.h despues de windows.h. NO incluir campaign.h
// ni save.h aqui (traen raylib.h, que choca con windows.h en la misma TU);
// runSyncTest vive en src/sync_test.cpp.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#endif
#include "net.h"
#include <cstdio>
#include <cstring>
#include <vector>

static bool g_netReady=false;

// Delay simple para tests (Sleep en Windows, usleep en POSIX)
#ifdef _WIN32
static void netSleepMs(int ms){ Sleep(ms); }
#else
static void netSleepMs(int ms){ usleep((useconds_t)ms*1000); }
#endif

#ifdef _WIN32
typedef SOCKET netsock_t;
#else
typedef int netsock_t;
#endif

static void sockClose(NetSock s){
    if(s==NET_INVALID) return;
#ifdef _WIN32
    closesocket((SOCKET)s);
#else
    close((int)s);
#endif
}

bool netInit(){
    if(g_netReady) return true;
#ifdef _WIN32
    WSADATA wsa;
    if(WSAStartup(MAKEWORD(2,2),&wsa)!=0) return false;
#endif
    g_netReady=true;
    return true;
}

void netShutdown(){
    if(!g_netReady) return;
    g_netReady=false;
#ifdef _WIN32
    WSACleanup();
#endif
}

NetSock netOpen(unsigned short bindPort, unsigned short* outPort){
    if(!netInit()) return NET_INVALID;
    NetSock s=(NetSock)socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(s==NET_INVALID) return NET_INVALID;
    // No bloqueante
#ifdef _WIN32
    u_long nb=1;
    if(ioctlsocket((SOCKET)s,FIONBIO,&nb)!=0){ sockClose(s); return NET_INVALID; }
#else
    int fl=fcntl((int)s,F_GETFL,0);
    if(fl<0 || fcntl((int)s,F_SETFL,fl|O_NONBLOCK)<0){ sockClose(s); return NET_INVALID; }
#endif
    sockaddr_in a;
    memset(&a,0,sizeof a);
    a.sin_family=AF_INET;
    a.sin_port=htons(bindPort);
    a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(s,(struct sockaddr*)&a,sizeof a)!=0){ sockClose(s); return NET_INVALID; }
    if(outPort){
        sockaddr_in la;
        memset(&la,0,sizeof la);
#ifdef _WIN32
        int sl=(int)sizeof la;
#else
        socklen_t sl=(socklen_t)sizeof la;
#endif
        if(getsockname(s,(struct sockaddr*)&la,&sl)==0) *outPort=ntohs(la.sin_port);
        else *outPort=bindPort;
    }
    return s;
}

void netClose(NetSock s){ sockClose(s); }

int netSendTo(NetSock s, const char* ip, unsigned short port,
              const void* data, int len){
    if(s==NET_INVALID||!g_netReady) return -1;
    sockaddr_in d;
    memset(&d,0,sizeof d);
    d.sin_family=AF_INET;
    d.sin_port=htons(port);
    if(inet_pton(AF_INET,ip,&d.sin_addr)!=1) return -1;
    int r=(int)sendto((netsock_t)s,(const char*)data,len,0,(struct sockaddr*)&d,sizeof d);
    return r;
}

int netRecvFrom(NetSock s, void* buf, int maxLen, int timeoutMs){
    if(s==NET_INVALID||!g_netReady||maxLen<=0) return -1;
    fd_set rf;
    FD_ZERO(&rf);
#ifdef _WIN32
    FD_SET((SOCKET)s,&rf);
#else
    FD_SET((int)s,&rf);
#endif
    timeval tv;
    tv.tv_sec=timeoutMs/1000;
    tv.tv_usec=(timeoutMs%1000)*1000;
#ifdef _WIN32
    int r=select(0,&rf,NULL,NULL,&tv);
#else
    int r=select((int)s+1,&rf,NULL,NULL,&tv);
#endif
    if(r==0) return 0;   // timeout
    if(r<0) return -1;   // error
    sockaddr_in from;
    memset(&from,0,sizeof from);
#ifdef _WIN32
    int sl=(int)sizeof from;
#else
    socklen_t sl=(socklen_t)sizeof from;
#endif
    int n=(int)recvfrom((netsock_t)s,(char*)buf,maxLen,0,(struct sockaddr*)&from,&sl);
    return n;
}

// Fase H: como netRecvFrom pero devuelve ademas ip:port de origen (para que
// el drain de netSessionPoll pueda distinguir clientes y responderles).
static int netRecvFromRaw(NetSock s, void* buf, int maxLen, int timeoutMs,
                          char* ipOut, int ipMax, unsigned short* portOut){
    if(s==NET_INVALID||!g_netReady||maxLen<=0) return -1;
    fd_set rf;
    FD_ZERO(&rf);
#ifdef _WIN32
    FD_SET((SOCKET)s,&rf);
#else
    FD_SET((int)s,&rf);
#endif
    timeval tv;
    tv.tv_sec=timeoutMs/1000;
    tv.tv_usec=(timeoutMs%1000)*1000;
#ifdef _WIN32
    int r=select(0,&rf,NULL,NULL,&tv);
#else
    int r=select((int)s+1,&rf,NULL,NULL,&tv);
#endif
    if(r==0) return 0;
    if(r<0) return -1;
    sockaddr_in from;
    memset(&from,0,sizeof from);
#ifdef _WIN32
    int sl=(int)sizeof from;
#else
    socklen_t sl=(socklen_t)sizeof from;
#endif
    int n=(int)recvfrom((netsock_t)s,(char*)buf,maxLen,0,(struct sockaddr*)&from,&sl);
    if(n<0) return -1;
    if(ipOut&&ipMax>0){
        const char* ip=inet_ntoa(from.sin_addr);
        snprintf(ipOut,(size_t)ipMax,"%s",ip?ip:"?");
    }
    if(portOut) *portOut=ntohs(from.sin_port);
    return n;
}

// ── Fase F: protocolo de sesión ─────────────────────────────────────────────

bool netSendMsg(NetSock s, const char* ip, unsigned short port,
                const NetMsg& m){
    NetMsg out=m;
    out.magic=NET_MAGIC;
    out.version=(unsigned char)NET_PROTO_VERSION;
    return netSendTo(s,ip,port,&out,(int)sizeof out)==(int)sizeof out;
}

int netRecvMsg(NetSock s, NetMsg* out, int timeoutMs){
    return netRecvMsgFrom(s,out,timeoutMs,nullptr,0,nullptr);
}

int netRecvMsgFrom(NetSock s, NetMsg* out, int timeoutMs,
                   char* ipOut, int ipMax, unsigned short* portOut){
    if(!out) return -1;
    if(s==NET_INVALID||!g_netReady) return -1;
    // Espera readable (select) con timeout
    fd_set rf;
    FD_ZERO(&rf);
#ifdef _WIN32
    FD_SET((SOCKET)s,&rf);
#else
    FD_SET((int)s,&rf);
#endif
    timeval tv;
    tv.tv_sec=timeoutMs/1000;
    tv.tv_usec=(timeoutMs%1000)*1000;
#ifdef _WIN32
    int sel=select(0,&rf,NULL,NULL,&tv);
#else
    int sel=select((int)s+1,&rf,NULL,NULL,&tv);
#endif
    if(sel==0) return 0;                 // timeout
    if(sel<0) return -1;                 // error
    sockaddr_in from;
    memset(&from,0,sizeof from);
#ifdef _WIN32
    int sl=(int)sizeof from;
#else
    socklen_t sl=(socklen_t)sizeof from;
#endif
    int n=(int)recvfrom((netsock_t)s,(char*)out,(int)sizeof *out,0,
                        (struct sockaddr*)&from,&sl);
    if(n==0) return 0;
    if(n!=(int)sizeof *out) return -1;          // datagrama truncado/tamaño raro
    if(out->magic!=NET_MAGIC) return -1;        // no es nuestro protocolo
    if(out->version!=NET_PROTO_VERSION) return -1; // build incompatible
    if(ipOut&&ipMax>0){
        const char* ip=inet_ntoa(from.sin_addr);
        snprintf(ipOut,(size_t)ipMax,"%s",ip?ip:"?");
    }
    if(portOut) *portOut=ntohs(from.sin_port);
    return 1;
}

// ── Fase H: snapshots de campaña + comandos ────────────────────────────────

bool netSendSnapMsg(NetSock s, const char* ip, unsigned short port,
                    const NetSnapMsg& m){
    NetSnapMsg out=m;
    out.magic=NET_MAGIC;
    out.version=(unsigned char)NET_PROTO_VERSION;
    return netSendTo(s,ip,port,&out,(int)sizeof out)==(int)sizeof out;
}

int netRecvSnapMsg(NetSock s, NetSnapMsg* out, int timeoutMs){
    if(!out) return -1;
    if(s==NET_INVALID||!g_netReady) return -1;
    fd_set rf;
    FD_ZERO(&rf);
#ifdef _WIN32
    FD_SET((SOCKET)s,&rf);
#else
    FD_SET((int)s,&rf);
#endif
    timeval tv;
    tv.tv_sec=timeoutMs/1000;
    tv.tv_usec=(timeoutMs%1000)*1000;
#ifdef _WIN32
    int sel=select(0,&rf,NULL,NULL,&tv);
#else
    int sel=select((int)s+1,&rf,NULL,NULL,&tv);
#endif
    if(sel==0) return 0;
    if(sel<0) return -1;
    sockaddr_in from;
    memset(&from,0,sizeof from);
#ifdef _WIN32
    int sl=(int)sizeof from;
#else
    socklen_t sl=(socklen_t)sizeof from;
#endif
    int n=(int)recvfrom((netsock_t)s,(char*)out,(int)sizeof *out,0,
                        (struct sockaddr*)&from,&sl);
    if(n==0) return 0;
    if(n!=(int)sizeof *out) return -1;
    if(out->magic!=NET_MAGIC) return -1;
    if(out->version!=NET_PROTO_VERSION) return -1;
    if(out->type!=NET_MSG_SNAPSHOT) return -1;
    if(out->len>NET_SNAP_CHUNK) return -1;
    return 1;
}

bool netSendSnapshot(NetSock s, const char* ip, unsigned short port,
                     const void* blob, uint32_t size){
    if(!blob||size==0||size>NET_SNAP_MAX) return false;
    uint16_t seq=(uint16_t)(size&0xFFFF); // determinista por tamano (solo validacion)
    uint32_t off=0;
    while(off<size){
        NetSnapMsg m={};
        m.type=NET_MSG_SNAPSHOT;
        m.seq=seq;
        m.offset=off;
        m.total=size;
        uint32_t chunk=size-off;
        if(chunk>NET_SNAP_CHUNK) chunk=NET_SNAP_CHUNK;
        m.len=(uint16_t)chunk;
        memcpy(m.data,(const char*)blob+off,chunk);
        if(!netSendSnapMsg(s,ip,port,m)) return false;
        off+=chunk;
    }
    return true;
}

int runSessionTest(){
    printf("SESSION: handshake loopback 127.0.0.1:%d v%d\n",
           NET_PORT,(int)NET_PROTO_VERSION);
    if(!netInit()){
        printf("SESSION FAIL - netInit\n");
        return 1;
    }
    unsigned short hostPort=0, cliPort=0;
    NetSock host=netOpen(NET_PORT,&hostPort);
    if(host==NET_INVALID){
        printf("SESSION FAIL - bind host :%d\n",NET_PORT);
        netShutdown();
        return 1;
    }
    NetSock cli=netOpen(0,&cliPort);
    if(cli==NET_INVALID){
        printf("SESSION FAIL - socket cliente\n");
        netClose(host);
        netShutdown();
        return 1;
    }

    bool ok=true;
    uint16_t cseq=0;   // secuencia del cliente
    uint16_t hseq=0;   // secuencia del host

    // 1) JOIN -> WELCOME
    NetMsg m={};
    m.type=NET_MSG_JOIN;
    m.seq=++cseq;
    snprintf(m.payload,sizeof m.payload,"join");
    if(!netSendMsg(cli,"127.0.0.1",NET_PORT,m)){ printf("SESSION FAIL - send JOIN\n"); ok=false; goto done; }
    {
        NetMsg r={};
        int rc=netRecvMsg(host,&r,500);
        if(rc!=1||r.type!=NET_MSG_JOIN||r.seq!=cseq){
            printf("SESSION FAIL - host recv JOIN (rc=%d type=%u seq=%u)\n",
                   rc,(unsigned)r.type,(unsigned)r.seq);
            ok=false; goto done;
        }
    }
    m={};
    m.type=NET_MSG_WELCOME;
    m.seq=++hseq;
    snprintf(m.payload,sizeof m.payload,"v%d",(int)NET_PROTO_VERSION);
    if(!netSendMsg(host,"127.0.0.1",cliPort,m)){ printf("SESSION FAIL - send WELCOME\n"); ok=false; goto done; }
    {
        NetMsg r={};
        int rc=netRecvMsg(cli,&r,500);
        if(rc!=1||r.type!=NET_MSG_WELCOME||r.seq!=hseq){
            printf("SESSION FAIL - cliente recv WELCOME (rc=%d type=%u)\n",rc,(unsigned)r.type);
            ok=false; goto done;
        }
    }
    printf("SESSION: JOIN -> WELCOME ok (cli:%u host:%u)\n",
           (unsigned)cliPort,(unsigned)hostPort);

    // 2) 3x PING/PONG con seq estricto
    for(int i=0;i<3&&ok;i++){
        m={};
        m.type=NET_MSG_PING;
        m.seq=++cseq;
        snprintf(m.payload,sizeof m.payload,"ping%d",i);
        if(!netSendMsg(cli,"127.0.0.1",NET_PORT,m)){ printf("SESSION FAIL - send PING %d\n",i); ok=false; break; }
        NetMsg r={};
        int rc=netRecvMsg(host,&r,500);
        if(rc!=1||r.type!=NET_MSG_PING||r.seq!=cseq){
            printf("SESSION FAIL - host recv PING %d (rc=%d seq=%u!=%u)\n",
                   i,rc,(unsigned)r.seq,(unsigned)cseq);
            ok=false; break;
        }
        m={};
        m.type=NET_MSG_PONG;
        m.seq=++hseq;
        memcpy(m.payload,r.payload,sizeof m.payload);
        if(!netSendMsg(host,"127.0.0.1",cliPort,m)){ printf("SESSION FAIL - send PONG %d\n",i); ok=false; break; }
        rc=netRecvMsg(cli,&r,500);
        if(rc!=1||r.type!=NET_MSG_PONG||r.seq!=hseq){
            printf("SESSION FAIL - cliente recv PONG %d (rc=%d type=%u)\n",i,rc,(unsigned)r.type);
            ok=false; break;
        }
        printf("SESSION: ping/pong %d ok (seq c=%u h=%u)\n",
               i,(unsigned)cseq,(unsigned)hseq);
    }
    if(!ok) goto done;

    // 3) BYE
    m={};
    m.type=NET_MSG_BYE;
    m.seq=++cseq;
    if(!netSendMsg(cli,"127.0.0.1",NET_PORT,m)){ printf("SESSION FAIL - send BYE\n"); ok=false; goto done; }
    {
        NetMsg r={};
        int rc=netRecvMsg(host,&r,500);
        if(rc!=1||r.type!=NET_MSG_BYE){
            printf("SESSION FAIL - host recv BYE (rc=%d)\n",rc);
            ok=false; goto done;
        }
    }
    printf("SESSION: BYE ok\n");

done:
    netClose(cli);
    netClose(host);
    netShutdown();
    if(ok){
        printf("SESSION PASS\n");
        return 0;
    }
    printf("SESSION FAIL\n");
    return 1;
}

int runNetTest(){
    const int ROUNDS=10;
    printf("NETTEST: UDP loopback 127.0.0.1:7777\n");
    if(!netInit()){
        printf("NETTEST FAIL - netInit (WSAStartup)\n");
        return 1;
    }
    unsigned short srvPort=0, cliPort=0;
    NetSock srv=netOpen(7777,&srvPort);
    if(srv==NET_INVALID){
        printf("NETTEST FAIL - bind 127.0.0.1:7777 (puerto ocupado?)\n");
        netShutdown();
        return 1;
    }
    NetSock cli=netOpen(0,&cliPort);
    if(cli==NET_INVALID){
        printf("NETTEST FAIL - socket cliente\n");
        netClose(srv);
        netShutdown();
        return 1;
    }
    printf("NETTEST: servidor :%u cliente :%u\n",
           (unsigned)srvPort,(unsigned)cliPort);
    int ok=0;
    for(int i=0;i<ROUNDS;i++){
        char msg[64];
        int mlen=snprintf(msg,sizeof msg,"PING %d",i);
        if(mlen<=0||netSendTo(cli,"127.0.0.1",7777,msg,mlen)!=mlen){
            printf("NETTEST round %d: send fallo\n",i);
            continue;
        }
        char buf[256];
        int rl=netRecvFrom(srv,buf,sizeof buf-1,500);
        if(rl<=0){
            printf("NETTEST round %d: servidor sin datagrama (recv=%d)\n",i,rl);
            continue;
        }
        buf[rl]='\0';
        if(netSendTo(srv,"127.0.0.1",cliPort,buf,rl)!=rl){
            printf("NETTEST round %d: eco fallo\n",i);
            continue;
        }
        rl=netRecvFrom(cli,buf,sizeof buf-1,500);
        if(rl<=0){
            printf("NETTEST round %d: cliente sin eco (recv=%d)\n",i,rl);
            continue;
        }
        buf[rl]='\0';
        if(strncmp(buf,"PING",4)==0){
            ok++;
            printf("NETTEST round %d: OK (%d bytes)\n",i,rl);
        }else{
            printf("NETTEST round %d: payload inesperado '%s'\n",i,buf);
        }
    }
    netClose(cli);
    netClose(srv);
    netShutdown();
    if(ok==ROUNDS){
        printf("NETTEST PASS (%d/%d)\n",ok,ROUNDS);
        return 0;
    }
    printf("NETTEST FAIL (%d/%d)\n",ok,ROUNDS);
    return 1;
}

// ── Fase G: sesión en runtime (lobby) ───────────────────────────────────────

NetSession g_netSession;

// Estado interno del host (no expuesto en la UI todavia)
struct NetHostClient {
    bool     used=false;
    char     ip[NET_NAME_LEN]={};
    uint16_t port=0;       // Fase H: puerto efimero del cliente (para snapshots)
    uint16_t lastSeq=0;
    int      faction=0;    // PvP: faccion que controla este cliente (0=FACTION_PLAYER en co-op)
};
static NetHostClient g_hostClients[NET_MAX_CLIENTS];

static void sessionReset(){
    if(g_netSession.sock!=NET_INVALID) netClose(g_netSession.sock);
    g_netSession = NetSession();          // vuelve a defaults (role NONE, status Idle)
    for(auto& c : g_hostClients) c = NetHostClient();
}

bool netSessionHostStart(){
    if(g_netSession.role!=NET_ROLE_NONE) return false;
    if(!netInit()){ snprintf(g_netSession.status,sizeof g_netSession.status,"netInit failed"); return false; }
    unsigned short port=0;
    NetSock s=netOpen((unsigned short)NET_PORT,&port);
    if(s==NET_INVALID){
        snprintf(g_netSession.status,sizeof g_netSession.status,"bind :%d failed (port busy?)",NET_PORT);
        netShutdown();
        return false;
    }
    g_netSession.sock=s;
    g_netSession.role=NET_ROLE_HOST;
    g_netSession.connected=true;
    g_netSession.playerCount=1;           // solo el host hasta que alguien joine
    snprintf(g_netSession.status,sizeof g_netSession.status,"Hosting on :%d (waiting)...",NET_PORT);
    return true;
}

void netSessionStop(){
    if(g_netSession.role==NET_ROLE_NONE) return;
    // Cierre ordenado si estamos cliente y conectados
    if(g_netSession.role==NET_ROLE_CLIENT&&g_netSession.connected){
        NetMsg m={};
        m.type=NET_MSG_BYE;
        m.seq=++g_netSession.seq;
        netSendMsg(g_netSession.sock,g_netSession.peerIp,(unsigned short)NET_PORT,m);
    }
    sessionReset();
}

bool netSessionClientJoin(const char* ip){
    if(g_netSession.role!=NET_ROLE_NONE) return false;
    if(!ip||!ip[0]) ip="127.0.0.1";
    if(!netInit()){ snprintf(g_netSession.status,sizeof g_netSession.status,"netInit failed"); return false; }
    unsigned short port=0;
    NetSock s=netOpen(0,&port);
    if(s==NET_INVALID){
        snprintf(g_netSession.status,sizeof g_netSession.status,"socket failed");
        netShutdown();
        return false;
    }
    g_netSession.sock=s;
    g_netSession.role=NET_ROLE_CLIENT;
    snprintf(g_netSession.peerIp,sizeof g_netSession.peerIp,"%s",ip);
    g_netSession.joinTries=0;
    // JOIN inmediato (y netSessionPoll reintentara si no hay respuesta)
    NetMsg m={};
    m.type=NET_MSG_JOIN;
    m.seq=++g_netSession.seq;
    snprintf(m.payload,sizeof m.payload,"join");
    if(!netSendMsg(s,ip,(unsigned short)NET_PORT,m)){
        snprintf(g_netSession.status,sizeof g_netSession.status,"send JOIN failed");
        sessionReset();
        return false;
    }
    g_netSession.joinTries=1;
    snprintf(g_netSession.status,sizeof g_netSession.status,"Joining %s:%d...",ip,NET_PORT);
    return true;
}

void netSessionPoll(){
    NetSession& S=g_netSession;
    if(S.role==NET_ROLE_NONE||S.sock==NET_INVALID) return;
    S.pollTick++;

    // Procesar todos los mensajes pendientes (no bloqueante: timeout 0).
    // Fase H: el drain recibe en un buffer generoso para poder distinguir
    // NetMsg (72 B) de NetSnapMsg (1032 B) por el tamaño del datagrama.
    for(int drain=0;drain<32;drain++){
        unsigned char raw[sizeof(NetSnapMsg)];
        char srcIp[NET_NAME_LEN]={};
        unsigned short srcPort=0;
        int n=netRecvFromRaw(S.sock,raw,sizeof raw,0,srcIp,(int)sizeof srcIp,&srcPort);
        if(n==0) break;                 // no hay mas datagramas
        if(n<0) continue;               // error
        if(n<(int)(sizeof(uint32_t)+2)) continue; // demasiado corto
        uint32_t magic=0; memcpy(&magic,raw,4);
        if(magic!=NET_MAGIC) continue;  // no es nuestro protocolo
        unsigned char ver=raw[5];
        if(ver!=NET_PROTO_VERSION) continue; // build incompatible
        unsigned char type=raw[4];

        // ── Fase H: snapshot (solo cliente lo recibe) ──
        if(type==NET_MSG_SNAPSHOT&&n==(int)sizeof(NetSnapMsg)&&S.role==NET_ROLE_CLIENT){
            NetSnapMsg sm; memcpy(&sm,raw,sizeof sm);
            if(sm.len>NET_SNAP_CHUNK) continue;
            if(sm.total==0||sm.total>NET_SNAP_MAX) continue;
            if(sm.offset>sm.total||(uint64_t)sm.offset+sm.len>sm.total) continue;
            if(sm.total!=S.snapSize||sm.offset!=S.snapRecv){
                // trozo fuera de orden o de otra sesion: reiniciar ensamblado
                S.snapSize=sm.total;
                S.snapRecv=0;
            }
            memcpy(S.snapBuf+sm.offset,sm.data,sm.len);
            S.snapRecv+=sm.len;
            if(S.snapRecv>=S.snapSize){
                S.snapReady=true;
                S.snapSize=S.snapRecv;
            }
            continue;
        }

        if(n!=(int)sizeof(NetMsg)) continue; // tamaño raro
        NetMsg r={}; memcpy(&r,raw,sizeof r);
        S.lastMsgType=(int)r.type;

        if(S.role==NET_ROLE_HOST){
            if(r.type==NET_MSG_JOIN){
                // Registrar cliente (o actualizar) y responder WELCOME al origen
                NetHostClient* slot=nullptr;
                for(auto& c : g_hostClients){
                    if(c.used&&c.lastSeq==r.seq&&strncmp(c.ip,srcIp,NET_NAME_LEN)==0){ slot=&c; break; }
                }
                bool isNew=false;
                for(auto& c : g_hostClients){
                    if(!slot&&!c.used){ slot=&c; isNew=true; break; }
                }
                if(slot){
                    slot->used=true;
                    slot->lastSeq=r.seq;
                    slot->port=srcPort;
                    snprintf(slot->ip,sizeof slot->ip,"%s",srcIp);
                    // PvP: asignar faccion al cliente (primera libre no-PLAYER/NEUTRAL)
                    if(S.pvpMode){
                        bool taken[NET_FACTION_COUNT]={};
                        taken[NET_FACTION_PLAYER]=true; taken[NET_FACTION_NEUTRAL]=true; // host=PLAYER, NEUTRAL no jugable
                        for(auto& c : g_hostClients)
                            if(c.used&&c.faction>=0&&c.faction<NET_FACTION_COUNT) taken[c.faction]=true;
                        int f=-1;
                        for(int i=1;i<NET_FACTION_COUNT;i++) if(!taken[i]){ f=i; break; }
                        slot->faction=(f>=0)?f:NET_FACTION_AGGRESSIVE; // fallback
                    } else {
                        slot->faction=0; // co-op: todos en FACTION_PLAYER
                    }
                    int cnt=1;
                    for(auto& c : g_hostClients) if(c.used) cnt++;
                    S.playerCount=cnt;
                    snprintf(S.status,sizeof S.status,"Hosting on :%d (%d players)",NET_PORT,S.playerCount);
                    if(isNew) S.needSnapshot=true; // Fase H: enviar snapshot al nuevo
                }
                NetMsg w={};
                w.type=NET_MSG_WELCOME;
                w.seq=++S.seq;
                // Payload: "v<ver> f<faccion>" (faccion solo en PvP; en co-op se omite)
                if(S.pvpMode&&slot)
                    snprintf(w.payload,sizeof w.payload,"v%d f%d",(int)NET_PROTO_VERSION,slot->faction);
                else
                    snprintf(w.payload,sizeof w.payload,"v%d",(int)NET_PROTO_VERSION);
                netSendMsg(S.sock,srcIp,srcPort,w);
            }else if(r.type==NET_MSG_PING){
                NetMsg p={};
                p.type=NET_MSG_PONG;
                p.seq=++S.seq;
                memcpy(p.payload,r.payload,sizeof p.payload);
                netSendMsg(S.sock,srcIp,srcPort,p);
            }else if(r.type==NET_MSG_BYE){
                for(auto& c : g_hostClients){
                    if(c.used&&strncmp(c.ip,srcIp,NET_NAME_LEN)==0) c.used=false;
                }
                int cnt=1;
                for(auto& c : g_hostClients) if(c.used) cnt++;
                S.playerCount=cnt;
                snprintf(S.status,sizeof S.status,"Hosting on :%d (%d players)",NET_PORT,S.playerCount);
            }else if(r.type==NET_MSG_CMD){
                // Fiabilidad: deduplicar por seq (el cliente puede retransmitir
                // si no llego el ACK). Buscar el slot del cliente por ip:port.
                NetHostClient* slot=nullptr;
                for(auto& c : g_hostClients){
                    if(c.used&&c.port==srcPort&&strncmp(c.ip,srcIp,NET_NAME_LEN)==0){ slot=&c; break; }
                }
                bool dup=(slot&&r.seq==slot->lastSeq);
                if(slot) slot->lastSeq=r.seq;
                // Fase H: comando de juego del cliente -> cola local (si no es dup)
                if(!dup&&S.cmdCount<(int)(sizeof S.cmdQueue/sizeof S.cmdQueue[0])){
                    NetCmd& c=S.cmdQueue[S.cmdCount++];
                    c.cmd=(unsigned char)r.payload[0];
                    memcpy(&c.a,r.payload+1,4);
                    memcpy(&c.b,r.payload+5,4);
                    memcpy(&c.c,r.payload+9,4);
                    c.faction=slot?slot->faction:0; // PvP: faccion del remitente
                }
                // Fiabilidad: ACK siempre (aunque sea dup; el cliente puede
                // haber perdido el ACK original).
                NetMsg ack={};
                ack.type=NET_MSG_ACK;
                ack.seq=++S.seq;
                memcpy(ack.payload,&r.seq,2);
                netSendMsg(S.sock,srcIp,srcPort,ack);
            }
        }else if(S.role==NET_ROLE_CLIENT){
            if(r.type==NET_MSG_WELCOME&&!S.connected){
                S.connected=true;
                S.playerCount=2;
                S.lastPongTick=S.pollTick; // missPong: reiniciar contador
                // PvP: parsear faccion asignada del payload "v<ver> f<faccion>"
                S.assignedFaction=-1; // co-op por defecto
                int vf=0, ff=-1;
                if(sscanf(r.payload,"v%d f%d",&vf,&ff)==2&&ff>=0&&ff<NET_FACTION_COUNT)
                    S.assignedFaction=ff;
                if(S.assignedFaction>=0)
                    snprintf(S.status,sizeof S.status,"Connected to %s (v%d, faction %d)",S.peerIp,(int)NET_PROTO_VERSION,S.assignedFaction);
                else
                    snprintf(S.status,sizeof S.status,"Connected to %s (v%d)",S.peerIp,(int)NET_PROTO_VERSION);
            }else if(r.type==NET_MSG_REJECT){
                snprintf(S.status,sizeof S.status,"Rejected: %.60s",r.payload);
                S.connected=false;
            }else if(r.type==NET_MSG_PONG){
                S.lastPongTick=S.pollTick; // keepalive OK
            }else if(r.type==NET_MSG_ACK){
                // Fiabilidad: el host confirmo un CMD; si coincide con el
                // pendiente, limpiar (ya no hace falta retransmitir).
                uint16_t ackSeq=0; memcpy(&ackSeq,r.payload,2);
                if(S.pendingCmdSeq!=0&&ackSeq==S.pendingCmdSeq){
                    S.pendingCmdSeq=0;
                    S.pendingCmdTry=0;
                }
            }
        }
    }

    // Lógica de reintentos/keepalive según tick (~60 fps asumido)
    if(S.role==NET_ROLE_CLIENT&&!S.connected){
        if(S.joinTries<5&&S.pollTick%60==0){
            NetMsg m={};
            m.type=NET_MSG_JOIN;
            m.seq=++S.seq;
            snprintf(m.payload,sizeof m.payload,"join");
            netSendMsg(S.sock,S.peerIp,(unsigned short)NET_PORT,m);
            S.joinTries++;
            snprintf(S.status,sizeof S.status,"Joining %s (try %d)...",S.peerIp,S.joinTries);
        }else if(S.joinTries>=5&&S.pollTick%60==0){
            snprintf(S.status,sizeof S.status,"No answer from %s",S.peerIp);
        }
    }else if(S.role==NET_ROLE_CLIENT&&S.connected){
        if(S.pollTick%120==0){           // keepalive cada ~2 s
            NetMsg m={};
            m.type=NET_MSG_PING;
            m.seq=++S.seq;
            netSendMsg(S.sock,S.peerIp,(unsigned short)NET_PORT,m);
            // missPong: si llevamos ~10 s sin PONG, el host cerro sin BYE
            if(S.pollTick-S.lastPongTick>=600){
                S.connected=false;
                snprintf(S.status,sizeof S.status,"Host lost (no answer from %s)",S.peerIp);
            }
        }
        // Fiabilidad CMD: retransmitir comando pendiente si no llego ACK
        // en ~1 s (60 ticks). Max 5 intentos; despues se descarta.
        if(S.pendingCmdSeq!=0&&S.pollTick-S.pendingCmdTick>=60){
            if(S.pendingCmdTry<5){
                NetMsg m={};
                m.type=NET_MSG_CMD;
                m.seq=S.pendingCmdSeq; // misma seq (el host deduplica por seq)
                m.payload[0]=(char)S.pendingCmd.cmd;
                memcpy(m.payload+1,&S.pendingCmd.a,4);
                memcpy(m.payload+5,&S.pendingCmd.b,4);
                memcpy(m.payload+9,&S.pendingCmd.c,4);
                netSendMsg(S.sock,S.peerIp,(unsigned short)NET_PORT,m);
                S.pendingCmdTick=S.pollTick;
                S.pendingCmdTry++;
            }else{
                S.pendingCmdSeq=0; // agotado; descartar
                S.pendingCmdTry=0;
            }
        }
    }
}

// ── Fase H: comandos + broadcast de snapshot ────────────────────────────────

bool netSessionSendCmd(const NetCmd& c){
    NetSession& S=g_netSession;
    if(S.role!=NET_ROLE_CLIENT||!S.connected||S.sock==NET_INVALID) return false;
    NetMsg m={};
    m.type=NET_MSG_CMD;
    m.seq=++S.seq;
    m.payload[0]=(char)c.cmd;
    memcpy(m.payload+1,&c.a,4);
    memcpy(m.payload+5,&c.b,4);
    memcpy(m.payload+9,&c.c,4);
    // Fiabilidad: guardar como pendiente para retransmitir si no hay ACK
    S.pendingCmd=c;
    S.pendingCmdSeq=m.seq;
    S.pendingCmdTick=S.pollTick;
    S.pendingCmdTry=1;
    return netSendMsg(S.sock,S.peerIp,(unsigned short)NET_PORT,m);
}

int netSessionTakeCmd(NetCmd* out, int maxOut){
    NetSession& S=g_netSession;
    if(S.role!=NET_ROLE_HOST||!out||maxOut<=0) return 0;
    int n=0;
    while(n<maxOut&&S.cmdCount>0){
        out[n++]=S.cmdQueue[0];
        for(int i=1;i<S.cmdCount;i++) S.cmdQueue[i-1]=S.cmdQueue[i];
        S.cmdCount--;
    }
    return n;
}

bool netSessionBroadcastSnapshot(const void* blob, uint32_t size){
    NetSession& S=g_netSession;
    if(S.role!=NET_ROLE_HOST||S.sock==NET_INVALID||!blob||size==0) return false;
    bool any=false;
    for(auto& c : g_hostClients){
        if(!c.used) continue;
        if(netSendSnapshot(S.sock,c.ip,c.port,blob,size)) any=true;
    }
    return any;
}

void netSessionConsumeSnapshot(){
    g_netSession.snapReady=false;
    g_netSession.snapSize=0;
    g_netSession.snapRecv=0;
}

bool netSessionHasClients(){
    if(g_netSession.role!=NET_ROLE_HOST) return false;
    for(auto& c : g_hostClients) if(c.used) return true;
    return false;
}

int runLobbyTest(){
    printf("LOBBY: runtime host+cliente loopback (mismo proceso)\n");
    // NetSession es un global: host y cliente NO coexisten en el mismo proceso.
    // Test: (1) host arranca y un JOIN crudo lo registra (playerCount>=2);
    // (2) sin host, clientJoin debe quedar en "No answer" (no connected).
    if(!netSessionHostStart()){
        printf("LOBBY FAIL - hostStart (%s)\n",g_netSession.status);
        netSessionStop();
        return 1;
    }
    printf("LOBBY: host arrancado (%s)\n",g_netSession.status);
    unsigned short cliPort=0;
    NetSock cli=netOpen(0,&cliPort);
    if(cli==NET_INVALID){
        printf("LOBBY FAIL - socket cliente\n");
        netSessionStop();
        return 1;
    }
    NetMsg m={};
    m.type=NET_MSG_JOIN;
    m.seq=1;
    snprintf(m.payload,sizeof m.payload,"join");
    if(!netSendMsg(cli,"127.0.0.1",(unsigned short)NET_PORT,m)){
        printf("LOBBY FAIL - send JOIN\n");
        netClose(cli); netSessionStop();
        return 1;
    }
    bool gotJoin=false;
    for(int i=0;i<50&&!gotJoin;i++){
        netSessionPoll();
        // Tras drenar, el WELCOME ecoado al propio host puede sobreescribir
        // lastMsgType; playerCount>=2 es la señal robusta de registro.
        if(g_netSession.playerCount>=2){ gotJoin=true; }
        else { netSleepMs(20); }
    }
    if(!gotJoin){
        printf("LOBBY FAIL - host no vio JOIN (status=%s)\n",g_netSession.status);
        netClose(cli); netSessionStop();
        return 1;
    }
    int pc=g_netSession.playerCount;
    printf("LOBBY: host registro JOIN (players=%d status=%s)\n",pc,g_netSession.status);
    netClose(cli);
    netSessionStop();
    if(pc<2){
        printf("LOBBY FAIL - playerCount %d\n",pc);
        return 1;
    }
    // Segundo escenario: cliente arranca sin host -> no debe conectar
    if(!netSessionClientJoin("127.0.0.1")){
        printf("LOBBY FAIL - clientJoin (%s)\n",g_netSession.status);
        netSessionStop();
        return 1;
    }
    for(int i=0;i<40&&!g_netSession.connected;i++){
        netSessionPoll();
        netSleepMs(20);
    }
    bool wronglyConnected=g_netSession.connected;
    printf("LOBBY: cliente sin host -> connected=%d status=%s\n",
           (int)g_netSession.connected,g_netSession.status);
    netSessionStop();
    if(wronglyConnected){
        printf("LOBBY FAIL - connected sin host\n");
        return 1;
    }
    printf("LOBBY PASS\n");
    return 0;
}

// Fase H: runSyncTest vive en src/sync_test.cpp (incluye campaign.h/raylib.h,
// que no puede coexistir con windows.h de winsock en esta unidad).

