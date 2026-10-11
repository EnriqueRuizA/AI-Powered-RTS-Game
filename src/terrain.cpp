// src/terrain.cpp — Generación de obstáculos AABB por tipo de terreno
// v11: ampliado con ríos (+ puentes), lagos, ciudades y terreno enriquecido.
#include "terrain.h"
#include <cstdlib>
#include <ctime>
#include <cmath>

// Obstáculo auxiliar con "tipo" implícito por color en drawBattlefield:
// se codifica en la posición/semilla. generateTerrainObstacles solo genera AABBs.

void generateTerrainObstacles(TerrainType terrain,int provinceIdx,std::vector<Rectangle>& out,
                              bool hasCity){
    out.clear();
    if(terrain==TERRAIN_FOREST){
        // Clusters of trees as obstacle rects
        srand(provinceIdx*13337);
        for(int i=0;i<25;i++){
            float ox=(float)(rand()%BATTLE_W);
            float oy=(float)(rand()%BATTLE_H);
            float ow=40.f+(float)(rand()%60);
            float oh=40.f+(float)(rand()%60);
            out.push_back({ox,oy,ow,oh});
        }
    } else if(terrain==TERRAIN_MOUNTAIN){
        srand(provinceIdx*7777);
        for(int i=0;i<12;i++){
            float ox=(float)(rand()%BATTLE_W);
            float oy=(float)(rand()%BATTLE_H);
            out.push_back({ox,oy,80.f+rand()%120,80.f+rand()%120});
        }
        // Roca extra dispersa
        for(int i=0;i<6;i++){
            float ox=(float)(rand()%BATTLE_W);
            float oy=(float)(rand()%BATTLE_H);
            out.push_back({ox,oy,40.f+rand()%50,40.f+rand()%50});
        }
    } else if(terrain==TERRAIN_PLAIN){
        // Llanura: un río con puente (obstáculo largo con hueco)
        srand(provinceIdx*5551);
        bool river=rand()%2==0;
        if(river){
            bool horiz=rand()%2==0;
            int bridgePos=800+rand()%960; // posición del puente en el eje
            if(horiz){
                // Río horizontal: 3 tramos con hueco en el puente
                float ry=(float)(rand()%BATTLE_H);
                out.push_back({0.f,ry,(float)bridgePos-40.f,36.f});
                out.push_back({(float)(bridgePos+40.f),ry,(float)BATTLE_W-(float)(bridgePos+40.f),36.f});
                // El puente NO es obstáculo (hueco pasable)
            } else {
                float rx=(float)(rand()%BATTLE_W);
                out.push_back({rx,0.f,36.f,(float)bridgePos-40.f});
                out.push_back({rx,(float)(bridgePos+40.f),36.f,(float)BATTLE_H-(float)(bridgePos+40.f)});
            }
        } else {
            // Colinas suaves
            for(int i=0;i<5;i++){
                float ox=(float)(rand()%BATTLE_W);
                float oy=(float)(rand()%BATTLE_H);
                out.push_back({ox,oy,50.f+rand()%70,50.f+rand()%70});
            }
        }
    } else if(terrain==TERRAIN_COAST){
        // Costa: lagos/marismas + rocas costeras
        srand(provinceIdx*3331);
        for(int i=0;i<4;i++){
            float ox=(float)(rand()%BATTLE_W);
            float oy=(float)(rand()%BATTLE_H);
            out.push_back({ox,oy,70.f+rand()%90,70.f+rand()%90});
        }
        for(int i=0;i<6;i++){
            float ox=(float)(rand()%BATTLE_W);
            float oy=(float)(rand()%BATTLE_H);
            out.push_back({ox,oy,30.f+rand()%40,30.f+rand()%40});
        }
    }

    // Ciudades: si la provincia tiene ciudad, añadir edificios en el centro
    if(hasCity){
        srand(provinceIdx*9973);
        int cx=BATTLE_W/2, cy=BATTLE_H/2;
        for(int i=0;i<6;i++){
            float ox=(float)(cx-160+rand()%320);
            float oy=(float)(cy-100+rand()%200);
            out.push_back({ox,oy,40.f+rand()%50,40.f+rand()%50});
        }
        // Muralla circular aproximada con 4 rectángulos
        out.push_back({(float)(cx-200),(float)(cy-140),400.f,24.f});
        out.push_back({(float)(cx-200),(float)(cy+116),400.f,24.f});
        out.push_back({(float)(cx-200),(float)(cy-140),24.f,280.f});
        out.push_back({(float)(cx+176),(float)(cy-140),24.f,280.f});
    }

    srand((unsigned)time(nullptr));
}
