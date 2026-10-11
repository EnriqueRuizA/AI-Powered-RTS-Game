#include "config.h"
#include "config.h"
#include "util.h"
#include "ui.h"
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

GameState g_state = STATE_MAIN_MENU;
bool      g_quitRequested = false;
bool      g_hasSave = false;
float     g_menuTime = 0.f;
GameSettings g_settings;

void saveSettings(){
    FILE* f=fopen(SETTINGS_FILE,"w");
    if(!f) return;
    fprintf(f,"screenW %d\n",g_settings.screenW);
    fprintf(f,"screenH %d\n",g_settings.screenH);
    fprintf(f,"fullscreen %d\n",(int)g_settings.fullscreen);
    fprintf(f,"uiScale %.3f\n",g_settings.uiScale);
    fprintf(f,"masterVolume %.3f\n",g_settings.masterVolume);
    fprintf(f,"musicVolume %.3f\n",g_settings.musicVolume);
    fprintf(f,"difficulty %d\n",g_settings.difficulty);
    fprintf(f,"showFPS %d\n",(int)g_settings.showFPS);
    fprintf(f,"language %d\n",g_settings.language);
    fprintf(f,"formationSpacing %.3f\n",g_settings.formationSpacing);
    fprintf(f,"formationPerRow %d\n",g_settings.formationPerRow);
    fprintf(f,"prepTimeSec %d\n",g_settings.prepTimeSec);
    fprintf(f,"usePrepPhase %d\n",(int)g_settings.usePrepPhase);
    fclose(f);
}
void loadSettings(){
    FILE* f=fopen(SETTINGS_FILE,"r");
    if(!f) return;
    char key[64];
    while(fscanf(f,"%63s",key)==1){
        if(strcmp(key,"screenW")==0) fscanf(f,"%d",&g_settings.screenW);
        else if(strcmp(key,"screenH")==0) fscanf(f,"%d",&g_settings.screenH);
        else if(strcmp(key,"fullscreen")==0){ int v=0; fscanf(f,"%d",&v); g_settings.fullscreen=(bool)v; }
        else if(strcmp(key,"uiScale")==0) fscanf(f,"%f",&g_settings.uiScale);
        else if(strcmp(key,"masterVolume")==0) fscanf(f,"%f",&g_settings.masterVolume);
        else if(strcmp(key,"musicVolume")==0) fscanf(f,"%f",&g_settings.musicVolume);
        else if(strcmp(key,"difficulty")==0) fscanf(f,"%d",&g_settings.difficulty);
        else if(strcmp(key,"showFPS")==0){ int v=0; fscanf(f,"%d",&v); g_settings.showFPS=(bool)v; }
        else if(strcmp(key,"language")==0) fscanf(f,"%d",&g_settings.language);
        else if(strcmp(key,"formationSpacing")==0) fscanf(f,"%f",&g_settings.formationSpacing);
        else if(strcmp(key,"formationPerRow")==0) fscanf(f,"%d",&g_settings.formationPerRow);
        else if(strcmp(key,"prepTimeSec")==0) fscanf(f,"%d",&g_settings.prepTimeSec);
        else if(strcmp(key,"usePrepPhase")==0){ int v=1; fscanf(f,"%d",&v); g_settings.usePrepPhase=(bool)v; }
    }
    fclose(f);
    // Clamp UI scale to supported range
    if(g_settings.uiScale<1.f) g_settings.uiScale=1.f;
    if(g_settings.uiScale>2.25f) g_settings.uiScale=2.25f;
    if(g_settings.formationSpacing<16.f) g_settings.formationSpacing=16.f;
    if(g_settings.formationSpacing>40.f) g_settings.formationSpacing=40.f;
    if(g_settings.formationPerRow<3) g_settings.formationPerRow=3;
    if(g_settings.formationPerRow>10) g_settings.formationPerRow=10;
    if(g_settings.prepTimeSec<5) g_settings.prepTimeSec=5;
    if(g_settings.prepTimeSec>60) g_settings.prepTimeSec=60;
}
