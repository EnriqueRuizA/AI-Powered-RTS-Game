// src/ui_multiplayer.cpp — Pantalla MULTIPLAYER (Fase G: lobby host/join)
// Refactor R1 paso 1: extraida de main.cpp. Refactor mecanico, sin cambio
// de comportamiento (mismas coordenadas, textos, colores y flujo). El estado
// de red vive en net.cpp (g_netSession) y la campana en campaign.cpp
// (g_campaign); aqui solo se pinta y se impulsan HOST/JOIN/PvP/BACK.
#include "ui_multiplayer.h"
#include "config.h"
#include "util.h"
#include "ui.h"
#include "campaign.h"
#include "net.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>

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
