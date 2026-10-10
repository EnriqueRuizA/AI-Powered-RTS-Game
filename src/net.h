// src/net.h — Base de red UDP (Fase E) + protocolo de sesión (Fase F)
// Fundamento de red: sockets UDP no bloqueantes en loopback, verificables
// con el flag de linea de comandos -nettest (headless, sale con 0/1).
// Fase F anade el mensaje de sesión NetMsg (JOIN/WELCOME/PING/PONG/BYE)
// y el handshake minimo host<->cliente verificable en loopback.
// Fases posteriores (G/H) anadiran lobby y sincronización de campaña.
#pragma once
#include <stdint.h>

// Manejador opaco de socket (uintptr para casar con SOCKET de Winsock).
typedef uintptr_t NetSock;
#define NET_INVALID ((NetSock)-1)

// Puerto y version de protocolo (Fase F).
// Cambiar NET_PROTO_VERSION invalida handshakes con builds antiguos (REJECT).
#define NET_PORT          7777
#define NET_PROTO_VERSION 1

// Facciones (espejo del enum FactionId en config.h; net.h no puede incluir
// config.h porque trae raylib.h y choca con winsock2.h en la misma TU).
// Valores identicos a config.h:88 para que el payload WELCOME sea portable.
#define NET_FACTION_PLAYER      0
#define NET_FACTION_AGGRESSIVE  1
#define NET_FACTION_DEFENSIVE   2
#define NET_FACTION_COMMERCIAL  3
#define NET_FACTION_PIRATE      4
#define NET_FACTION_NEUTRAL     5
#define NET_FACTION_COUNT       6

// WSAStartup/WSAStartup-equivalente. Idempotente; true si OK.
bool netInit();
// Deshace netInit (WSACleanup). Idempotente.
void netShutdown();

// Crea un socket UDP no bloqueante ligado a 127.0.0.1:bindPort
// (0 = puerto efimero). outPort recibe el puerto real asignado (puede ser 0).
// NET_INVALID si falla. Fase E/F: solo loopback (evita avisos de firewall);
// G/H ampliaran a INADDR_ANY cuando haya protocolo de juego.
NetSock netOpen(unsigned short bindPort, unsigned short* outPort);
void netClose(NetSock s);

// Envia datagrama a ip:port. Devuelve bytes enviados o -1.
int netSendTo(NetSock s, const char* ip, unsigned short port,
              const void* data, int len);
// Recibe datagrama esperando hasta timeoutMs. >0 = bytes, 0 = timeout, -1 = error.
int netRecvFrom(NetSock s, void* buf, int maxLen, int timeoutMs);

// ── Fase F: protocolo de sesión ─────────────────────────────────────────────
// Magic 'MCNT' en little-endian (M=0x4D C=0x43 N=0x4E T=0x54).
#define NET_MAGIC 0x544E434Du

enum NetMsgType : unsigned char {
    NET_MSG_JOIN    = 1,  // cliente -> host: pide unirse
    NET_MSG_WELCOME = 2,  // host -> cliente: aceptado (payload: version aceptada)
    NET_MSG_REJECT  = 3,  // host -> cliente: rechazado (payload: razon corta)
    NET_MSG_PING    = 4,  // keepalive either way (payload: eco del seq)
    NET_MSG_PONG    = 5,  // respuesta a PING
    NET_MSG_BYE     = 6,  // cierre ordenado
    NET_MSG_SNAPSHOT= 7,  // Fase H: host -> clientes, trozo de snapshot de campana
    NET_MSG_CMD     = 8,  // Fase H: cliente -> host, comando de juego
    NET_MSG_ACK     = 9   // host -> cliente: ack de CMD (payload: eco del seq)
};

#pragma pack(push,1)
struct NetMsg {
    uint32_t      magic;    // NET_MAGIC
    unsigned char type;     // NetMsgType
    unsigned char version;  // NET_PROTO_VERSION del emisor
    uint16_t      seq;      // secuencia estrictamente creciente por emisor
    char          payload[64];   // payload corto (texto o datos tipados)
};
#pragma pack(pop)

// Envia NetMsg rellenando magic y version automaticamente. Devuelve true si OK.
bool netSendMsg(NetSock s, const char* ip, unsigned short port,
                const NetMsg& m);
// Recibe y valida NetMsg (magic + version). Devuelve 1 OK, 0 timeout, -1 invalido.
int netRecvMsg(NetSock s, NetMsg* out, int timeoutMs);
// Igual que netRecvMsg pero devuelve ademas la ip:port de origen (para que el
// host pueda responder WELCOME al puerto efimero correcto del cliente).
// ipOut puede ser null; portOut puede ser null.
int netRecvMsgFrom(NetSock s, NetMsg* out, int timeoutMs,
                   char* ipOut, int ipMax, unsigned short* portOut);

// ── Fase H: snapshots de campaña + comandos ────────────────────────────────
// El estado de campaña (hasta ~64-128 KB serializado) viaja en trozos
// NetSnapMsg (datagrama UDP independiente por trozo; en loopback no hay
// perdidas y el limite real de UDP es ~64 KB por datagrama).
#define NET_SNAP_CHUNK 1024
#define NET_SNAP_MAX   (256*1024)

#pragma pack(push,1)
struct NetSnapMsg {
    uint32_t      magic;    // NET_MAGIC
    unsigned char type;     // NET_MSG_SNAPSHOT
    unsigned char version;  // NET_PROTO_VERSION
    uint16_t      seq;      // secuencia del emisor
    uint32_t      offset;   // byte offset de este trozo en el blob completo
    uint32_t      total;    // tamano total del blob
    uint16_t      len;      // bytes validos en data (<= NET_SNAP_CHUNK)
    char          data[NET_SNAP_CHUNK];
};
#pragma pack(pop)

// Comandos de juego (cliente -> host) transportados en NetMsg.payload.
enum NetCmdType : unsigned char {
    NET_CMD_END_TURN  = 1,  // cliente pide pasar turno (host ejecuta processTurn)
    NET_CMD_MOVE_ARMY = 2,  // a=from, b=to, c=armyIdx (o -1 = playerProvince)
    NET_CMD_RECRUIT   = 3,  // a=provinceIdx, b=typeIdx
    NET_CMD_DISBAND   = 4,  // a=armyIdx
    NET_CMD_SYNC_REQ  = 5,  // cliente pide snapshot completo
    NET_CMD_ATTACK    = 6,  // H2: a=targetProvince, b=armyIdx (host entra en PRE_BATTLE)
    NET_CMD_FORM_ARMY = 7,  // PvP: a=province (host forma ejército de la facción del remitente)
    NET_CMD_JOIN_ARMY = 8   // PvP: a=province (host une reserva al ejército de la facción)
};

struct NetCmd {
    unsigned char cmd=0;
    int a=0, b=0, c=0;
    int faction=0;  // PvP: facción del remitente (host la rellena al recibir)
};

// Envia NetSnapMsg validado (magic/version). Devuelve true si OK.
bool netSendSnapMsg(NetSock s, const char* ip, unsigned short port,
                    const NetSnapMsg& m);
// Recibe y valida NetSnapMsg (magic + version + tamaño exacto).
// Devuelve 1 OK, 0 timeout, -1 invalido.
int  netRecvSnapMsg(NetSock s, NetSnapMsg* out, int timeoutMs);
// Envia un blob completo troceado a ip:port. Devuelve true si todos los
// trozos salieron.
bool netSendSnapshot(NetSock s, const char* ip, unsigned short port,
                     const void* blob, uint32_t size);

// Self-test capa cruda: 10 ping/pong UDP entre dos sockets en 127.0.0.1:7777.
// Imprime NETTEST PASS/FAIL por stdout. Devuelve 0 = PASS, 1 = FAIL.
int runNetTest();
// Self-test sesión (Fase F): JOIN->WELCOME, 3x PING/PONG con seq, BYE.
// Imprime SESSION PASS/FAIL. Devuelve 0 = PASS, 1 = FAIL.
int runSessionTest();
// Self-test runtime de lobby (Fase G): host+cliente en el MISMO proceso
// usando la API de sesión (netSession*): join automatico -> connected.
// Imprime LOBBY PASS/FAIL. Devuelve 0 = PASS, 1 = FAIL.
int runLobbyTest();
// Self-test de sincronización de campaña (Fase H): serializa una campaña
// construida a mano, la envía troceada por loopback, la reensambla,
// la deserializa y comprueba campos clave; tambien prueba el canal de
// comandos (NET_MSG_CMD). Imprime SYNC PASS/FAIL. Devuelve 0 = PASS, 1 = FAIL.
int runSyncTest();

// ── Fase G: sesión en runtime (lobby) ───────────────────────────────────────
// Estado global de la sesión de red; la pantalla STATE_MULTIPLAYER lo pinta
// y lo impulsa (HOST/JOIN/BACK). netSessionPoll() se llama cada frame.
enum NetRole { NET_ROLE_NONE=0, NET_ROLE_HOST, NET_ROLE_CLIENT };

#define NET_MAX_CLIENTS 8
#define NET_NAME_LEN    64

struct NetSession {
    NetRole role          = NET_ROLE_NONE;
    NetSock sock          = NET_INVALID;
    bool    connected     = false;   // cliente:收到了 WELCOME; host: siempre true si escuchando
    int     playerCount   = 0;       // host: 1 (tu) + clientes; cliente: 2 si conectado
    char    status[96]    = "Idle";  // frase corta para la UI
    char    peerIp[NET_NAME_LEN] = "127.0.0.1"; // cliente: IP del host; host: (no usado)
    uint16_t seq          = 0;       // secuencia propia (creciente)
    int     lastMsgType   = 0;       // ultimo NetMsgType recibido (0=nada)
    int     pollTick      = 0;       // ticks de poll (reintentos/keepalive)
    int     joinTries     = 0;       // reenvios de JOIN (max 5)
    int     lastPongTick  = 0;       // cliente: tick del ultimo PONG/mensaje del host
    // Fase H: cola de comandos recibidos (host) y snapshot recibido (cliente)
    NetCmd  cmdQueue[32];
    int     cmdCount      = 0;
    bool    snapReady     = false;   // cliente: hay snapshot completo pendiente de aplicar
    char    snapBuf[NET_SNAP_MAX];
    uint32_t snapSize     = 0;
    uint32_t snapRecv     = 0;
    bool    needSnapshot  = false;   // host: nuevo cliente registrado -> enviar snapshot
    // Fiabilidad de CMD (ack + retransmision): el cliente guarda el ultimo
    // comando enviado y lo reenvia si no llega ACK en ~1 s (60 ticks).
    NetCmd  pendingCmd;
    uint16_t pendingCmdSeq= 0;       // seq del comando pendiente (0 = ninguno)
    int     pendingCmdTick= 0;       // tick en que se envio por ultima vez
    int     pendingCmdTry = 0;       // intentos de envio (max 5)
    // PvP: host asigna faccion al cliente al hacer JOIN; el cliente la
    // aprende del WELCOME (o del snapshot). -1 = co-op (sin PvP).
    bool    pvpMode        = false;  // host: PvP activo (asignar faccion)
    int     assignedFaction= -1;     // cliente: faccion que controla (-1 = co-op)
};
extern NetSession g_netSession;

// Abre NET_PORT y empieza a aceptar JOIN (respuesta WELCOME automatica).
bool netSessionHostStart();
// Cierra el socket y vuelve a NET_ROLE_NONE.
void netSessionStop();
// Envia JOIN a ip:PORT (no bloquea; el WELCOME llega en netSessionPoll).
bool netSessionClientJoin(const char* ip);
// Procesa mensajes entrantes y keepalive. Llamar UNA vez por frame.
void netSessionPoll();

// Fase H: sincronización de campaña (host autoritativo)
// Host: encola un comando recibido; netSessionTakeCmd lo extrae.
bool netSessionSendCmd(const NetCmd& c);              // cliente -> host
int  netSessionTakeCmd(NetCmd* out, int maxOut);      // host drena su cola
// Host: envia el blob de campaña serializado a todos los clientes conectados.
bool netSessionBroadcastSnapshot(const void* blob, uint32_t size);
// Cliente: marca el snapshot como consumido (ya aplicado a g_campaign).
void netSessionConsumeSnapshot();
// Host: true si hay al menos 1 cliente conectado.
bool netSessionHasClients();
