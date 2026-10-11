// src/mapart.cpp — Arte procedural del mapa de campana (Fase C)
#include "mapart.h"
#include "campaign.h"
#include "util.h"
#include <random>
#include <cmath>
#include <vector>
#include <algorithm>

int cityBuildLevel(const City& city){
    int n=1;
    for(int b=0;b<BLD_COUNT;b++) if(city.built[b]) n++;
    if(n>16) n=16;
    return n;
}

// ═══════════════════════════════════════════════════════════════════════════
//  MAR DE FONDO
// ═══════════════════════════════════════════════════════════════════════════
void drawCampaignSea(float time){
    DrawRectangleGradientV(0,0,SCREEN_W,SCREEN_H,{20,46,72,255},{10,24,42,255});
    // Olas: 3 lineas sinusoidales en deriva lenta
    for(int w=0;w<3;w++){
        float baseY=140.f+(float)w*(SCREEN_H-200)/2.5f;
        float phase=time*(0.6f+0.25f*(float)w)+(float)w*2.1f;
        unsigned char a=(unsigned char)(46+w*10);
        int prevX=0;
        float prevY=baseY+sinf(phase)*6.f;
        for(int x=12;x<=SCREEN_W;x+=12){
            float y=baseY+sinf(x*0.018f+phase)*6.f+sinf(x*0.007f-phase*1.3f)*3.f;
            DrawLine(prevX,(int)prevY,x,(int)y,{90,150,185,a});
            prevX=x; prevY=y;
        }
    }
    // Bruma arriba/abajo
    DrawRectangleGradientV(0,0,SCREEN_W,60,{120,160,190,40},{0,0,0,0});
    DrawRectangleGradientV(0,SCREEN_H-60,SCREEN_W,60,{0,0,0,0},{8,14,22,120});
}

// ═══════════════════════════════════════════════════════════════════════════
//  BLOBS DE PROVINCIA (deterministicos por indice)
// ═══════════════════════════════════════════════════════════════════════════
static const std::vector<Vector2>& blobUnitPts(int idx){
    static std::vector<std::vector<Vector2>> cache;
    if((int)cache.size()<=idx){
        int oldSize=(int)cache.size();
        cache.resize(idx+1);
        const int N=14;
        for(int k=oldSize;k<=idx;k++){
            std::mt19937 rng(0x9E3779B9u*(unsigned)(k+1));
            std::uniform_real_distribution<float> jr(0.76f,1.05f);
            std::uniform_real_distribution<float> ja(-0.09f,0.09f);
            cache[k].resize(N);
            for(int i=0;i<N;i++){
                float ang=(float)i*2.f*PI/(float)N+ja(rng);
                float r=jr(rng);
                cache[k][i]={cosf(ang)*r,sinf(ang)*r};
            }
        }
    }
    return cache[idx];
}

void drawProvinceBlob(int idx, Vector2 center, float rad,
                      Color fill, Color border, Color ownerRing){
    const std::vector<Vector2>& u=blobUnitPts(idx);
    int n=(int)u.size();
    for(int i=0;i<n;i++){
        const Vector2& a=u[i];
        const Vector2& b=u[(i+1)%n];
        DrawTriangle(center,
                     {center.x+b.x*rad,center.y+b.y*rad},
                     {center.x+a.x*rad,center.y+a.y*rad},
                     fill);
    }
    // Contorno del terreno
    for(int i=0;i<n;i++){
        const Vector2& a=u[i];
        const Vector2& b=u[(i+1)%n];
        DrawLineEx({center.x+a.x*rad,center.y+a.y*rad},
                   {center.x+b.x*rad,center.y+b.y*rad},2.f,border);
    }
    // Anillo interior del dueno (encima del relleno)
    for(int i=0;i<n;i++){
        const Vector2& a=u[i];
        const Vector2& b=u[(i+1)%n];
        DrawLineEx({center.x+a.x*rad*0.88f,center.y+a.y*rad*0.88f},
                   {center.x+b.x*rad*0.88f,center.y+b.y*rad*0.88f},1.5f,ownerRing);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  CONTINENTE TIPO RISK — celdas Voronoi por provincia
//  Continent = silueta fija con wobble determinista por campana.
//  Celda[i] = continent ∩ {p mas cerca de seed i que de cualquier otra}.
//  Solo visual: la logica de provincias no se toca.
// ═══════════════════════════════════════════════════════════════════════════
static std::vector<Vector2>                  g_cont;     // poligono del continente
static std::vector<std::vector<Vector2>>     g_cell;     // celda Voronoi por provincia
static int                                   g_cellCamp=-1;
static std::vector<Vector2>                  g_cellSeed; // centros cacheados (detecta resize)

// Silueta normalizada del continente (y-down, sentido horario visual)
static std::vector<Vector2> contOutline(int campaignId){
    // Europa (campaña 4): silueta realista — Ibérica, Francia, Escandinavia,
    // Italia (bota), Balcanes, Europa del Este. Reconocible a simple vista.
    if(campaignId==4){
        static const float europe[][2]={
            // Portugal/Andalucía (suroeste)
            {0.040f,0.760f},{0.050f,0.640f},{0.100f,0.580f},
            // Golfo de Vizcaya / Bretaña
            {0.170f,0.545f},{0.195f,0.485f},{0.235f,0.455f},
            // Canal de la Mancha / Países Bajos
            {0.290f,0.395f},{0.330f,0.355f},
            // Dinamarca / Noruega
            {0.375f,0.295f},{0.395f,0.215f},{0.405f,0.135f},
            // Noruega norte / Escandinavia
            {0.475f,0.075f},{0.560f,0.095f},{0.640f,0.115f},
            // Finlandia / Rusia norte
            {0.720f,0.130f},{0.840f,0.165f},{0.930f,0.245f},
            // Rusia este / Ucrania
            {0.910f,0.375f},{0.800f,0.430f},{0.730f,0.470f},
            // Mar Negro / Turquía
            {0.710f,0.560f},{0.680f,0.660f},
            // Grecia (sur)
            {0.615f,0.800f},{0.565f,0.775f},
            // Adriático este / Italia este
            {0.530f,0.640f},{0.470f,0.590f},
            // Bota italiana (sur)
            {0.445f,0.830f},{0.405f,0.795f},{0.395f,0.700f},
            // Italia oeste / Francia mediterránea
            {0.345f,0.615f},{0.305f,0.645f},
            // España este / sur
            {0.230f,0.710f},{0.160f,0.760f},{0.090f,0.790f}
        };
        const int NB=(int)(sizeof(europe)/sizeof(europe[0]));
        float cx=(float)SCREEN_W*0.45f, cy=(float)SCREEN_H*0.45f;
        std::mt19937 rng(0x4555524Fu); // "EURO"
        std::uniform_real_distribution<float> jn(-1.f,1.f);
        std::vector<Vector2> pts;
        pts.reserve(NB*2);
        for(int i=0;i<NB;i++){
            int j=(i+1)%NB;
            Vector2 a={europe[i][0]*(float)SCREEN_W, europe[i][1]*(float)SCREEN_H};
            Vector2 b={europe[j][0]*(float)SCREEN_W, europe[j][1]*(float)SCREEN_H};
            pts.push_back(a);
            Vector2 m={(a.x+b.x)*0.5f,(a.y+b.y)*0.5f};
            float nx=m.x-cx, ny=m.y-cy;
            float nl=sqrtf(nx*nx+ny*ny); if(nl<1.f) nl=1.f;
            float off=5.f+jn(rng)*16.f; // costa irregular pero contenida
            m.x+=nx/nl*off; m.y+=ny/nl*off;
            pts.push_back(m);
        }
        return pts;
    }
    static const float base[][2]={
        {0.030f,0.300f},{0.100f,0.140f},{0.240f,0.080f},{0.420f,0.120f},
        {0.560f,0.060f},{0.720f,0.140f},{0.860f,0.100f},{0.950f,0.260f},
        {0.970f,0.460f},{0.900f,0.620f},{0.930f,0.800f},{0.780f,0.900f},
        {0.600f,0.860f},{0.440f,0.930f},{0.280f,0.880f},{0.140f,0.920f},
        {0.050f,0.760f},{0.020f,0.550f}
    };
    const int NB=18;
    std::mt19937 rng(0x5EAC01u+(unsigned)campaignId*747796405u);
    std::uniform_real_distribution<float> jn(-1.f,1.f);
    float cx=(float)SCREEN_W*0.5f, cy=(float)SCREEN_H*0.5f;
    std::vector<Vector2> pts;
    pts.reserve(NB*2);
    for(int i=0;i<NB;i++){
        int j=(i+1)%NB;
        Vector2 a={base[i][0]*(float)SCREEN_W, base[i][1]*(float)SCREEN_H};
        Vector2 b={base[j][0]*(float)SCREEN_W, base[j][1]*(float)SCREEN_H};
        pts.push_back(a);
        // Punto medio subdividido: ruido hacia fuera (costa irregular)
        Vector2 m={(a.x+b.x)*0.5f,(a.y+b.y)*0.5f};
        float nx=m.x-cx, ny=m.y-cy;
        float nl=sqrtf(nx*nx+ny*ny); if(nl<1.f) nl=1.f;
        float off=8.f+jn(rng)*26.f; // siempre mayormente hacia fuera
        m.x+=nx/nl*off; m.y+=ny/nl*off;
        pts.push_back(m);
    }
    return pts;
}

// Sutherland-Hodgman: recorta poly al semiplano mas cerca de a que de b
static void clipHalfPlane(std::vector<Vector2>& poly, Vector2 a, Vector2 b){
    if(poly.size()<3) return;
    Vector2 d={b.x-a.x, b.y-a.y};
    float c0=(b.x*b.x+b.y*b.y)-(a.x*a.x+a.y*a.y); // 2p·(b-a) <= c0
    auto dist2=[&](Vector2 p){ return 2.f*(p.x*d.x+p.y*d.y)-c0; };
    std::vector<Vector2> out;
    out.reserve(poly.size()+2);
    int n=(int)poly.size();
    for(int i=0;i<n;i++){
        Vector2 P=poly[i], Q=poly[(i+1)%n];
        float sp=dist2(P), sq=dist2(Q);
        bool inP=sp<=0.f, inQ=sq<=0.f;
        if(inP) out.push_back(P);
        if(inP!=inQ){
            float t=sp/(sp-sq);
            out.push_back({P.x+(Q.x-P.x)*t, P.y+(Q.y-P.y)*t});
        }
    }
    poly.swap(out);
}

static float distSeg(Vector2 p, Vector2 a, Vector2 b){
    float abx=b.x-a.x, aby=b.y-a.y;
    float t=((p.x-a.x)*abx+(p.y-a.y)*aby)/(abx*abx+aby*aby+1e-6f);
    if(t<0.f) t=0.f;
    if(t>1.f) t=1.f;
    float px=a.x+abx*t-p.x, py=a.y+aby*t-p.y;
    return sqrtf(px*px+py*py);
}

static bool edgeOnCoast(Vector2 a, Vector2 b); // definicion mas abajo

// ── Fase D+: bordes irregulares (ruido posicional compartido) ───────────────
// Hash determinista [-1,1]
static float whash(int x,int y){
    unsigned h=(unsigned)x*374761393u+(unsigned)y*668265263u;
    h=(h^(h>>13))*1274126177u;
    return (float)((h&0xFFFFu)*(2.0f/65535.0f))-1.f;
}
// Value noise suave y posicional: misma posicion => mismo valor, asi que
// ambas celdas de una frontera calculan exactamente el mismo desplazamiento.
static float wnoise(float x,float y){
    const float SC=0.030f; // celda de ~33px => ondulacion de ~66px
    float fx=x*SC, fy=y*SC;
    int ix=(int)floorf(fx), iy=(int)floorf(fy);
    float tx=fx-(float)ix, ty=fy-(float)iy;
    tx=tx*tx*(3.f-2.f*tx); ty=ty*ty*(3.f-2.f*ty);
    float a=whash(ix,iy), b=whash(ix+1,iy), c=whash(ix,iy+1), d=whash(ix+1,iy+1);
    float ab=a+(b-a)*tx, cd=c+(d-c)*tx;
    return ab+(cd-ab)*ty;
}
// Subdivide cada arista y desplaza sus puntos interiores en perpendicular.
// Los extremos NO se mueven (esquinas compartidas => sin grietas) y la
// perpendicular usa direccion canonica lexicografica para que los dos lados
// de la frontera desplacen los puntos por igual.
static void wobblePoly(std::vector<Vector2>& poly, float ampMax, float step, bool skipCoast){
    int n=(int)poly.size();
    if(n<3) return;
    std::vector<Vector2> out;
    out.reserve(poly.size()*4);
    for(int i=0;i<n;i++){
        Vector2 a=poly[i], b=poly[(i+1)%n];
        out.push_back(a);
        float dx=b.x-a.x, dy=b.y-a.y;
        float len=sqrtf(dx*dx+dy*dy);
        if(len<2.f) continue;
        if(skipCoast && edgeOnCoast(a,b)) continue; // costa ya ondulada en g_cont
        bool flip=(a.x>b.x)||(a.x==b.x&&a.y>b.y);
        float ux=flip?-dx/len:dx/len, uy=flip?-dy/len:dy/len;
        float nx=-uy, ny=ux;
        int seg=(int)(len/step)+1;
        if(seg<2) seg=2;
        if(seg>24) seg=24;
        float amp=ampMax;
        if(len*0.16f<amp) amp=len*0.16f; // aristas cortas casi rectas (evita auto-interseccion)
        for(int k=1;k<seg;k++){
            float t=(float)k/(float)seg;
            float mx=a.x+dx*t, my=a.y+dy*t;
            float d=wnoise(mx,my)*amp;
            out.push_back({mx+nx*d, my+ny*d});
        }
    }
    poly.swap(out);
}

static bool needRebuild(int campaignId){
    if(g_cellCamp!=campaignId) return true;
    const auto& prov=g_campaign.provinces;
    if(g_cell.size()!=prov.size() || g_cellSeed.size()!=prov.size()) return true;
    for(size_t i=0;i<prov.size();i++){
        if(fabsf(g_cellSeed[i].x-prov[i].center.x)>0.5f ||
           fabsf(g_cellSeed[i].y-prov[i].center.y)>0.5f) return true;
    }
    return false;
}

static void buildCells(int campaignId){
    const auto& prov=g_campaign.provinces;
    g_cont=contOutline(campaignId);
    wobblePoly(g_cont,9.f,18.f,false); // Fase D+: costa irregular
    g_cell.assign(prov.size(), {});
    g_cellSeed.clear();
    for(size_t i=0;i<prov.size();i++) g_cellSeed.push_back(prov[i].center);
    for(size_t i=0;i<prov.size();i++){
        std::vector<Vector2> poly=g_cont;
        for(size_t j=0;j<prov.size() && poly.size()>=3;j++){
            if(j==i) continue;
            clipHalfPlane(poly, prov[i].center, prov[j].center);
        }
        wobblePoly(poly,7.f,18.f,true); // Fase D+: fronteras irregulares (costa intacta)
        g_cell[i]=poly;
    }
    g_cellCamp=campaignId;
}

// Relleno en abanico desde f, con shrink hacia f (deja gaps de frontera)
static void fillFan(Vector2 f, const std::vector<Vector2>& poly, Color col, float shrink){
    int n=(int)poly.size();
    if(n<3) return;
    for(int i=0;i<n;i++){
        Vector2 A={f.x+(poly[i].x-f.x)*shrink,   f.y+(poly[i].y-f.y)*shrink};
        Vector2 B={f.x+(poly[(i+1)%n].x-f.x)*shrink, f.y+(poly[(i+1)%n].y-f.y)*shrink};
        DrawTriangle(f, B, A, col); // CCW: raylib culea caras traseras (GL_CULL_FACE)
    }
}

// ¿La mitad del borde cae sobre la costa del continente?
static bool edgeOnCoast(Vector2 a, Vector2 b){
    Vector2 m={(a.x+b.x)*0.5f,(a.y+b.y)*0.5f};
    int n=(int)g_cont.size();
    for(int i=0;i<n;i++){
        if(distSeg(m, g_cont[i], g_cont[(i+1)%n])<2.5f) return true;
    }
    return false;
}

void drawCampaignContinent(int campaignId){
    if(needRebuild(campaignId)) buildCells(campaignId);
    if(g_cont.size()<3) return;

    const auto& prov=g_campaign.provinces;

    // Relleno base del continente (playa/pergamino) — los gaps lo muestran
    Vector2 cc={0.f,0.f};
    for(auto&p:g_cont){ cc.x+=p.x; cc.y+=p.y; }
    cc.x/=(float)g_cont.size(); cc.y/=(float)g_cont.size();
    int cn=(int)g_cont.size();
    for(int i=0;i<cn;i++)
        DrawTriangle(cc, g_cont[(i+1)%cn], g_cont[i], {224,208,160,255}); // CCW (anti-culling)

    // Colores de relleno por terreno (mismo estilo que el mapa viejo)
    static const Color terrainFill[4]={
        {140,125,70,255},   // PLAIN
        {50,95,45,255},     // FOREST
        {90,75,65,255},     // MOUNTAIN
        {196,176,120,255}   // COAST
    };
    static const Color fogFill={35,38,42,255};

    // 1) Rellenos de celda
    for(size_t i=0;i<prov.size() && i<g_cell.size();i++){
        bool explored=(i<g_campaign.explored.size() && g_campaign.explored[i]);
        Color fill;
        if(!explored) fill=fogFill;
        else{
            // Fase D+: el color de dueno domina (0.85) => territorio propio y
            // aliado se lee como un solo bloque; cada enemigo, su color.
            // Fase I: en modo alianzas solo cambian los colores (aliado =
            // color del jugador, pacto = verde, sin relacion = gris).
            Color oc=diplomacyMapColor(prov[i].owner);
            Color tc=terrainFill[(int)prov[i].terrain];
            const float OW=0.85f;
            fill.r=(unsigned char)((float)tc.r+((float)oc.r-(float)tc.r)*OW);
            fill.g=(unsigned char)((float)tc.g+((float)oc.g-(float)tc.g)*OW);
            fill.b=(unsigned char)((float)tc.b+((float)oc.b-(float)tc.b)*OW);
        }
        fillFan(prov[i].center, g_cell[i], fill, 0.965f);
    }

    // 2) Bordes: costa (sand) y fronteras entre provincias (oscuro)
    for(size_t i=0;i<prov.size() && i<g_cell.size();i++){
        bool explored=(i<g_campaign.explored.size() && g_campaign.explored[i]);
        const auto& poly=g_cell[i];
        int n=(int)poly.size();
        for(int k=0;k<n;k++){
            Vector2 a=poly[k], b=poly[(k+1)%n];
            if(edgeOnCoast(a,b))
                DrawLineEx(a,b,3.f,{214,196,140,255});
            else
                DrawLineEx(a,b,2.f, explored?Color{58,48,30,230}:Color{64,68,72,220});
        }
    }

    // 3) Contorno exterior del continente (sobre el mar)
    for(int i=0;i<cn;i++)
        DrawLineEx(g_cont[i], g_cont[(i+1)%cn], 2.5f,{120,102,60,255});
}

bool pointInProvinceCell(int idx, Vector2 pt){
    if(idx<0 || idx>=(int)g_cell.size()) return false;
    const auto& poly=g_cell[idx];
    int n=(int)poly.size();
    if(n<3){
        // Fallback circular si la celda quedo vacia
        if(idx<(int)g_campaign.provinces.size())
            return vdist(pt, g_campaign.provinces[idx].center)<44.f;
        return false;
    }
    bool inside=false;
    for(int i=0,j=n-1;i<n;j=i++){
        if((poly[i].y>pt.y)!=(poly[j].y>pt.y) &&
           pt.x < (poly[j].x-poly[i].x)*(pt.y-poly[i].y)/(poly[j].y-poly[i].y)+poly[i].x)
            inside=!inside;
    }
    return inside;
}

void drawProvinceCellOutline(int idx, Color col, float thick){
    if(idx<0 || idx>=(int)g_cell.size()) return;
    const auto& poly=g_cell[idx];
    int n=(int)poly.size();
    for(int k=0;k<n;k++) DrawLineEx(poly[k], poly[(k+1)%n], thick, col);
}

// ═══════════════════════════════════════════════════════════════════════════
//  DECORACION DE TERRENO
// ═══════════════════════════════════════════════════════════════════════════
void drawTerrainDecor(int idx, TerrainType t, Vector2 c, float rad){
    std::mt19937 rng(0x51ED270Bu^(unsigned)idx*2654435761u);
    std::uniform_real_distribution<float> u01(0.f,1.f);
    auto spot=[&](float&x,float&y,float rMin,float rMax){
        float ang=u01(rng)*2.f*PI;
        float rr=rad*(rMin+u01(rng)*(rMax-rMin));
        x=c.x+cosf(ang)*rr; y=c.y+sinf(ang)*rr*0.78f; // squash Y: perspectiva
    };
    float x,y;
    if(t==TERRAIN_MOUNTAIN){
        for(int i=0;i<4;i++){
            spot(x,y,0.12f,0.5f);
            float w=7.f+u01(rng)*7.f, h=6.f+u01(rng)*7.f;
            DrawTriangle({x-w,y},{x+w,y},{x,y-h},{118,112,104,255});
            DrawTriangle({x-w,y},{x,y},{x,y-h},{92,88,82,255}); // lado sombra (CCW)
            DrawTriangle({x-w*0.34f,y-h*0.58f},{x+w*0.34f,y-h*0.58f},{x,y-h},{225,228,232,255});
        }
    } else if(t==TERRAIN_FOREST){
        for(int i=0;i<7;i++){
            spot(x,y,0.12f,0.58f);
            float s=0.8f+u01(rng)*0.5f;
            DrawRectangle((int)x-1,(int)y-2,2,3,{74,52,30,255});
            DrawTriangle({x-4.f*s,y-1.f},{x+4.f*s,y-1.f},{x,y-10.f*s},{40,92,42,255});
            DrawTriangle({x-3.f*s,y-5.f},{x+3.f*s,y-5.f},{x,y-13.f*s},{48,108,48,255});
        }
    } else if(t==TERRAIN_PLAIN){
        for(int i=0;i<6;i++){
            spot(x,y,0.12f,0.55f);
            if(u01(rng)<0.4f){
                DrawLine((int)x,(int)y,(int)(x-2),(int)(y-4),{112,132,58,220});
                DrawLine((int)x,(int)y,(int)(x+1),(int)(y-5),{124,144,64,220});
                DrawLine((int)x,(int)y,(int)(x+3),(int)(y-3),{104,124,54,220});
            } else {
                DrawLine((int)x,(int)y,(int)x,(int)(y-5),{170,150,80,230});
                DrawCircle((int)x,(int)(y-6),1,{196,176,96,230});
            }
        }
    } else { // COAST: ola + arena
        for(int i=0;i<3;i++){
            spot(x,y,0.5f,0.68f);
            DrawLine((int)(x-4),(int)y,(int)(x+4),(int)y,{170,210,225,150});
            DrawLine((int)(x-2),(int)(y+2),(int)(x+6),(int)(y+2),{160,200,215,110});
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  MINI-CIUDAD ACUMULATIVA
// ═══════════════════════════════════════════════════════════════════════════
static const Color MC_WOOD  ={96,66,40,255};
static const Color MC_STRAW ={188,160,98,255};
static const Color MC_STONE ={142,138,128,255};
static const Color MC_DSTONE={104,100,94,255};
static const Color MC_ROOF  ={118,58,44,255};

static void mcHut(float x,float gy,float s){
    // cabaña de paja: cuerpo madera + techo triangular
    DrawRectangle((int)(x-3.f*s),(int)(gy-4.f*s),(int)(6.f*s),(int)(4.f*s),MC_WOOD);
    DrawTriangle({x-4.5f*s,gy-3.5f*s},{x+4.5f*s,gy-3.5f*s},{x,gy-9.f*s},MC_STRAW);
    DrawRectangle((int)(x-1.f*s),(int)(gy-2.5f*s),(int)(2.f*s),(int)(2.5f*s),{50,34,20,255});
}

static void mcHouse(float x,float gy,float s){
    DrawRectangle((int)(x-3.5f*s),(int)(gy-5.f*s),(int)(7.f*s),(int)(5.f*s),{150,120,80,255});
    DrawTriangle({x-4.5f*s,gy-4.5f*s},{x+4.5f*s,gy-4.5f*s},{x,gy-10.f*s},MC_ROOF);
    DrawRectangle((int)(x-1.f*s),(int)(gy-3.f*s),(int)(2.f*s),(int)(3.f*s),MC_WOOD);
}

static void mcKeep(float x,float gy,float s){
    // torre del homenaje
    DrawRectangle((int)(x-4.5f*s),(int)(gy-13.f*s),(int)(9.f*s),(int)(13.f*s),MC_STONE);
    for(int b=0;b<3;b++)
        DrawRectangle((int)(x-4.5f*s+b*3.5f*s),(int)(gy-15.5f*s),(int)(2.2f*s),(int)(3.f*s),MC_DSTONE);
    DrawRectangle((int)(x-1.f*s),(int)(gy-4.f*s),(int)(2.f*s),(int)(4.f*s),{40,32,24,255});
    DrawRectangle((int)(x-3.f*s),(int)(gy-11.f*s),(int)(2.f*s),(int)(2.f*s),{70,90,120,255});
    DrawRectangle((int)(x+1.2f*s),(int)(gy-11.f*s),(int)(2.f*s),(int)(2.f*s),{70,90,120,255});
}

static void mcTower(float x,float gy,float s){
    DrawRectangle((int)(x-2.f*s),(int)(gy-8.f*s),(int)(4.f*s),(int)(8.f*s),MC_DSTONE);
    DrawRectangle((int)(x-2.6f*s),(int)(gy-10.f*s),(int)(5.2f*s),(int)(2.4f*s),MC_STONE);
    DrawRectangle((int)(x-0.7f*s),(int)(gy-5.f*s),(int)(1.4f*s),(int)(1.8f*s),{60,75,100,255});
}

void drawMiniCity(Vector2 pos,int level,FactionId owner,float time){
    float gy=pos.y+4.f;
    float s=0.95f;
    Color fc=diplomacyMapColor(owner); // Fase I: modo alianzas cambia solo el color
    // plataforma de tierra
    DrawEllipse((int)pos.x,(int)gy,14,4,{78,60,36,170});
    // L1+: aldea de cabañas
    mcHut(pos.x-6.f,gy+1.f,s*0.9f);
    mcHut(pos.x+5.5f,gy+1.5f,s*0.8f);
    // L4+: casas + puesto de mercado con toldo de faccion
    if(level>=4){
        mcHouse(pos.x-1.f,gy+1.f,s*0.85f);
        mcHouse(pos.x+8.f,gy+2.f,s*0.7f);
        // mercado: toldo
        DrawRectangle((int)(pos.x-12.f),(int)(gy-4.f),(int)(5.f),(int)(1.5f),fc);
        DrawLine((int)(pos.x-12.f),(int)(gy-4.f),(int)(pos.x-12.f),(int)gy,MC_WOOD);
        DrawLine((int)(pos.x-7.f),(int)(gy-4.f),(int)(pos.x-7.f),(int)gy,MC_WOOD);
    }
    // L8+: keep + torres
    if(level>=8){
        mcKeep(pos.x,gy+1.f,s*0.9f);
        mcTower(pos.x-9.f,gy+1.f,s*0.8f);
        mcTower(pos.x+9.f,gy+1.f,s*0.8f);
    }
    // L12+: muralla con almenas + puerta + esquineras
    if(level>=12){
        int wl=(int)(pos.x-13.f), wr=(int)(pos.x+13.f);
        int wtop=(int)(gy-7.f), wbot=(int)(gy+3.f);
        // muros laterales (hueco abajo = puerta)
        DrawRectangle(wl,wtop,3,wbot-wtop,MC_DSTONE);
        DrawRectangle(wr-3,wtop,3,wbot-wtop,MC_DSTONE);
        // tramo superior
        DrawRectangle(wl,wtop-2,wr-wl,3,MC_DSTONE);
        for(int cx=wl;cx<wr-2;cx+=4) DrawRectangle(cx,wtop-4,2,2,MC_STONE);
        // esquineras
        mcTower((float)wl+1.f,gy+3.f,s*0.7f);
        mcTower((float)wr-1.f,gy+3.f,s*0.7f);
    }
    // Bandera de faccion en el punto mas alto (keep L8+, sino cabaña)
    float fxPos=(level>=8)?pos.x:pos.x-6.f;
    float yTop=(level>=8)?gy-14.f*s:gy-9.f*s*0.9f;
    DrawLine((int)fxPos,(int)yTop,(int)fxPos,(int)(yTop-7.f),{40,36,30,255});
    float flutter=sinf(time*4.f+pos.x*0.05f)*1.2f;
    DrawTriangle({fxPos,yTop-7.f},
                 {fxPos,yTop-3.5f},
                 {fxPos+6.f+flutter,yTop-5.5f},fc); // CCW (anti-culling)
}
