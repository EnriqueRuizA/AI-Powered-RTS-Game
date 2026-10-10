# PLAN DE SOLUCIÓN — Proyecto Medieval Conquest (RTS)

> **Fecha original:** 2026-10-07
> **Fecha de revisión y corrección:** 2026-10-08
> **Estado:** Documento verificado contra el código (`rts_game.cpp`) y contra el índice de Git.
> **Alcance de esta revisión:** solo se ha corregido este documento. Las acciones sobre
> Git, `.gitignore` y código que se describen aquí **aún no están ejecutadas**.

---

## 0. VERIFICACIÓN DE DATOS

Datos comprobados directamente en el repositorio antes de corregir el plan:

| Dato | Valor verificado | Comando / origen |
|---|---|---|
| Líneas de `rts_game.cpp` | **4.816** (4.430 no vacías) | conteo directo |
| Tamaño de `rts_game.cpp` | **247.784 bytes (~242 KB)** | `Get-Item` |
| Archivos trackeados en Git | `.gitignore`, `LICENSE`, `README.md`, `rts_game.cpp`, `rts_game.exe`, `settings.ini`, 5 DLLs | `git ls-files` |
| Binarios trackeados | `rts_game.exe` (~2,7 MB) + `libraylib.dll`, `libstdc++-6.dll`, `glfw3.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll` (~5,1 MB) ≈ **~8 MB** | `git ls-files` |
| Sin trackear (pendiente de commit) | `PLAN.md`, `code-reviewer.agent.md` | `git status` |
| Guardados ignorados | `campaign_0.dat`, `campaign_1.dat`, `campaign_2.dat`, `campaign_last.dat`, `campaign_save.dat`, `savegame.dat` | `.gitignore` |
| Sistema de build | **No existe** (sin `CMakeLists.txt`, sin `Makefile`, sin `.github/`) | `Test-Path` |
| Comando de build real | `g++ -std=c++17 rts_game.cpp -lraylib -lm -o game` | `rts_game.cpp:4` |
| Ramas | `main`, `Estabilización` | `git branch -a` |
| Último commit | `8418583` — "Update de mapa de campaña" (2026-02-27) | `git log` |
| Contenido histórico de `NOTAS.txt` | 5 líneas de un prompt de mejora, **sin credenciales** | `git show bde8338:NOTAS.txt` |
| `.cursor\` | **No existe** | `Test-Path` |

---

## Resumen ejecutivo

Este documento describe la reestructuración del proyecto `Medieval Conquest` (RTS en C++ con Raylib)
desde una estructura *single-file* hacia una arquitectura modular mantenible, además de
sanear la gestión de Git y la configuración.

`NOTAS.txt` fue eliminado del árbol de trabajo (y está en `.gitignore`), pero **su contenido
sigue accesible en el historial de Git** (commit `bde8338`). Su contenido real era una plantilla
de prompt de 5 líneas, sin credenciales ni datos sensibles. Todo lo ajeno a este repositorio
(otras iniciativas) queda fuera del alcance de este plan.

---

## 1. ESTADO ACTUAL

> **Nota de ejecución:** esta sección describe el estado **previo** a las FASE 0–2
> (ya ejecutadas). El estado vigente está en **«Estado de ejecución»** al final.

### 1.1 Resumen del proyecto

| Atributo | Valor |
|----------|-------|
| Nombre | Medieval Conquest — RTS |
| Lenguaje | C++17 |
| Motor / librería principal | Raylib (versión exacta a confirmar: la DLL no expone número de versión) |
| Compilador / toolchain | g++ (MinGW, por las DLLs de runtime) / Visual Studio (`.vs\`) |
| Archivo fuente | `rts_game.cpp` — single-file, **4.816 líneas / ~242 KB** |
| Ejecutable | `rts_game.exe` (~2,7 MB) — **sí está versionado en Git** |
| Runtime | 5 DLLs (`libraylib.dll`, `glfw3.dll`, `libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`) — **también versionadas** |
| Configuración | `settings.ini` — tiene regla en `.gitignore` **pero está trackeado** (regla ineficaz) |
| Sistema de build | **Inexistente** (sin Makefile/CMake/CI) |
| Estado del código | Funcional, pero no escalable (4.816 líneas en un solo archivo) |
| Documentación | `README.md` (2 líneas) + `LICENSE` (MIT) |

### 1.2 Arquitectura actual

```
P:\REPOSITORIO GIT\Proyecto-Memphis\
├── rts_game.cpp            → Único fuente (4.816 líneas, ~242 KB)
├── rts_game.exe            → Binario compilado (SÍ trackeado, ~2,7 MB)
├── libraylib.dll           → Runtime de Raylib (SÍ trackeado, ~2,0 MB)
├── libstdc++-6.dll         → Runtime C++ MinGW (SÍ trackeado, ~2,4 MB)
├── glfw3.dll               → Runtime de ventana (SÍ trackeado, ~0,24 MB)
├── libgcc_s_seh-1.dll      → Runtime GCC (SÍ trackeado, ~0,15 MB)
├── libwinpthread-1.dll     → Runtime pthreads (SÍ trackeado, ~0,11 MB)
├── settings.ini            → Configuración (trackeado pese a estar en .gitignore)
├── campaign_*.dat          → Guardados de campaña (ignorados, no trackeados)
├── README.md               → Mínimo (2 líneas)
├── LICENSE                 → MIT
├── .gitignore              → Incompleto (falta *.exe, *.dll, build/)
├── PLAN.md                 → Este documento (sin trackear)
├── code-reviewer.agent.md  → Agente de revisión (sin trackear)
├── .vs\                    → Proyecto de Visual Studio (ignorado)
└── .git\
```

### 1.3 Problemas identificados

| # | Problema | Impacto | Prioridad |
|---|----------|---------|-----------|
| A | Tres valores por defecto de resolución distintos (1280×768 / 1280×720 / 1600×900) | Comportamiento distinto si falta `settings.ini`; mantenimiento confuso | Media |
| B | Single-file de 4.816 líneas → difícil mantenimiento | Mantenimiento y velocidad de iteración (cualquier cambio recompila todo) | Alta |
| C | **Binarios trackeados** (~8 MB) y `.gitignore` sin `*.exe/*.dll` | Repo pesado; reglas de ignore que no surten efecto | Media |
| D | `README.md` desactualizado (2 líneas); sin `ARCHITECTURE.md`, sin build system, sin CI | Experiencia de desarrollo y de usuario | Media |
| E | `NOTAS.txt` eliminado pero **presente en el historial de Git** | Higiene del repo (sin datos sensibles: su contenido era un prompt de 5 líneas) | Baja |

### 1.4 Análisis de problemas técnicos

#### Problema A: Configuración con tres valores por defecto

- `settings.ini` declara `screenW=1600; screenH=900`.
- `rts_game.cpp:38-39` declara `static int SCREEN_W = 1280; SCREEN_H = 768;` (son **`static int`, no `static const`**: se reasignan en ejecución).
- `rts_game.cpp:700-701` declara los *defaults* del struct con `screenW=1280; screenH=720`.

**Cómo funciona realmente (importante para no "corregir" algo que no es un bug de render):**

1. `main()` llama a `loadSettings()` y sobrescribe `SCREEN_W/H = g_settings.screenW/H`
   **antes** de `InitWindow` (`rts_game.cpp:4692-4697`).
2. Cada frame, `SCREEN_W/H` se actualizan con `GetScreenWidth/Height` (`rts_game.cpp:4730-4731`).
3. Al aplicar ajustes se llama a `SetWindowSize(g_settings.screenW, g_settings.screenH)` (`rts_game.cpp:3811`).

**Conclusión:** en ejecución **no hay doble fuente de verdad**; el juego usa siempre `settings.ini`.
El problema real es de **mantenimiento**: tres *defaults* distintos hacen que el comportamiento
cambie según exista o no el `.ini`, y que las constantes del código mientan sobre el tamaño real.

#### Problema B: Single-file

- Todo el código en un único archivo (4.816 líneas) dificulta la navegación.
- No hay separación entre declaraciones (`.h`) y definiciones (`.cpp`).
- No hay módulos lógicos: unidades, combate, campaña, ciudad, UI, cámara, guardado.
- No hay separación de responsabilidades; cualquier cambio recompila las 4.816 líneas.

#### Problema C: Binarios versionados e ignore incompleto

- **`rts_game.exe` y las 5 DLLs SÍ están trackeados** en Git (~8 MB); el plan anterior afirmaba lo contrario.
- `.gitignore` **no contiene** `*.exe` ni `*.dll` (solo `NOTAS.txt`, `settings.ini`, `*.dat`, `.vs/*`, `.cursor/*`, `.git/*`, `PROMPT_*.md`).
- Añadir `*.exe/*.dll` al `.gitignore` **no basta**: Git ignora solo lo no trackeado. Hace falta además `git rm --cached`.
- `settings.ini` está en `.gitignore` **y** trackeado a la vez → la regla no surte efecto y sus cambios se siguen commitando.

#### Problema D: Documentación y build

- `README.md` solo contiene:
  ```markdown
  # Proyecto-Memphis
  Videojuego RTS creado en C++
  ```
- No existe `ARCHITECTURE.md`, `CMakeLists.txt`, `Makefile`, ni integración continua (`.github/`).
- El comando de build existe solo como comentario en `rts_game.cpp:4` y produce `game`, no `rts_game.exe`.

#### Problema E: `NOTAS.txt` en el historial

- El archivo fue eliminado del árbol de trabajo y añadido a `.gitignore`.
- Su contenido sigue en el historial (`bde8338`): 5 líneas de un prompt de mejora de juego,
  **sin credenciales ni información sensible** (verificado).
- Acción propuesta (baja prioridad): purgar el historial solo si se considera higiene; no es
  una incidencia de seguridad.

---

## 2. PLAN DE SOLUCIÓN — Hoja de ruta general

> **Decisión de arquitectura:** todos los `.h` y `.cpp` de juego viven en `src/`
> (se compila con `-Isrc`). **No se usa `include/`** en este plan: esa carpeta queda
> reservada en el futuro para librerías externas si se decide vendorizarlas
> (hoy Raylib llega del toolchain, no del repo).

```
FASE 0: Preparación                    → Sanitización y saneamiento
        ├─ .gitignore unificado + git rm --cached de binarios
        ├─ settings.example.ini (defaults únicos)
        └─ README.md inicial

FASE 1: Reestructuración modular
        ├─ src/config.h / config.cpp   → Constantes, enums, GameSettings, load/saveSettings
        ├─ src/util.h / util.cpp       → Funciones de utilidad + RNG actual
        ├─ src/ui.h / ui.cpp           → UI helpers (botones, sliders, texto)
        ├─ src/camera.h / camera.cpp   → Cámara
        ├─ src/sprite.h / sprite.cpp   → Generación procedural de sprites
        ├─ src/terrain.h / terrain.cpp → Obstáculos y generación de terreno
        ├─ src/elements.h / elements.cpp → Unidades, proyectiles, formaciones, stats
        ├─ src/combat.h / combat.cpp   → Lógica de combate
        ├─ src/campaign.h / campaign.cpp → Mapa, provincias, turnos
        ├─ src/city.h / city.cpp       → Construcciones, recursos
        ├─ src/save.h / save.cpp       → Serialización binaria
        ├─ src/main.cpp                → Punto de entrada
        └─ build/                      → Objetos (ignorado en Git)

FASE 2: Unificación de configuración
        ├─ Defaults de resolución únicos (1280×768) en código y example
        ├─ Validación de rangos (ya existe: uiScale 1.0–2.25)
        ├─ git rm --cached settings.ini + versionar settings.example.ini
        └─ Verificar fallback si falta settings.ini

FASE 3: Mejoras técnicas (v8.x)         → ver tabla con Estado en §9
        ├─ v8.2 (pendiente): separación suave entre unidades
        ├─ v8.3 (pendiente): cover-seeking  [flanqueo: ya hecho]
        └─ v8.4 (pendiente): sistema de misiones

FASE 4: Documentación y CI
        ├─ README.md completo
        ├─ CMakeLists.txt o Makefile
        ├─ ARCHITECTURE.md
        └─ .gitignore consolidado
```

---

## 3. CRONOGRAMA

| Fase | Tarea | Duración |
|------|-------|----------|
| FASE 0 | Preparación (gitignore, des-tracking, README) | 15–30 min |
| FASE 1 | Reestructuración modular (12 .cpp) | 3–4 horas |
| FASE 2 | Unificación de configuración | 20–30 min |
| FASE 3 | Mejoras técnicas (v8.2, v8.3, v8.4) | 2–3 horas |
| FASE 4 | Documentación y CI | 30 min |

**Total estimado:** **~6–8,5 horas** (suma de las duraciones anteriores: 6,1–8,5 h).

---

## 4. DETALLES TÉCNICOS

### FASE 0: Preparación

#### 4.1 `.gitignore` unificado (versión única; reemplaza a las dos que había)

```gitignore
# ============================================================
# .gitignore — Medieval Conquest (RTS)
# ============================================================

# --- Artefactos de compilación (no trackeados; si ya lo están,
# --- ejecutar antes: git rm --cached <archivo>) ---
*.exe
*.dll
*.lib
*.dll.a
*.obj
*.o
*.pdb
*.ilk
*.exp

# --- Carpetas de build ---
build/
_build/
cmake-build/
dist/
release/

# --- Editores / SO ---
.vs/
.vscode/
.idea/
.cursor/
.DS_Store

# --- Archivos temporales ---
*.swp
*.tmp
.cache/
*.i

# --- Guardados del juego (datos personales del usuario) ---
savegame.dat
campaign_save.dat
campaign_*.dat
*.sav

# --- Configuración local (se versiona settings.example.ini) ---
settings.ini

# --- Documentación interna / prompts ---
NOTAS.txt
PROMPT_MEJORAS_MEDIEVAL_CONQUEST.md
PROMPT_MEJORAS_PREMIUM.md
```

> **Nota:** se elimina la entrada `.git/*` de la versión actual (es redundante: Git nunca
> versiona su propio directorio). Añadir estas reglas **no des-trackea** nada por sí solo.

#### 4.2 `settings.example.ini` (nuevo archivo, versionado)

El archivo real `settings.ini` pasa a ser local de cada máquina. Este *example* documenta
los defaults y se versiona:

```ini
# settings.example.ini — Medieval Conquest
# Copiar a settings.ini (local, ignorado por Git).

# Resolución de la ventana (default único: 1280x768)
screenW 1280
screenH 768

# Pantalla completa (0/1)
fullscreen 0

# Escala de UI: rango permitido 1.000 - 2.250 (el código lo limita)
uiScale 1.000

# Volúmenes (0.000 - 1.000)
masterVolume 1.000
musicVolume 0.700

# Dificultad: 0=EASY, 1=NORMAL, 2=HARD
difficulty 1

# Mostrar FPS (0/1)
showFPS 0

# Idioma: 0=EN, 1=ES
language 0
```

> **Sobre el 1600×900 actual:** el `settings.ini` local del desarrollador usa 1600×900,
> pero los *defaults* del código son 1280×768 (`:38-39`) y 1280×720 (`:700-701`).
> Este plan unifica los tres en **1280×768**; el usuario puede volver a 1600×900 en su
> `settings.ini` local sin que eso se comitee.
>
> **Rangos verificados en el código:** `uiScale` se limita a **1.0–2.25**
> (`rts_game.cpp:743-744` y `:3759-3760`), no a 0.75–2.25.

#### 4.3 README.md inicial (propuesta)

````markdown
# Medieval Conquest — RTS

Un juego de estrategia en tiempo real en C++17 usando Raylib.

## Requisitos

- Compilador C++17 (g++ 11+ / MinGW o MSVC)
- Raylib 4.x o superior (versión exacta a confirmar)
- Windows (las DLLs de runtime deben estar junto al ejecutable)

## Compilación (FASE 1 completada)

```bash
# Unix / MinGW
make            # o: cmake -B cmake-build && cmake --build cmake-build
```

## Ejecución

```bash
./rts_game.exe    # las DLLs deben estar junto al ejecutable
```

## Configuración

- Copiar `settings.example.ini` a `settings.ini` (local, ignorado por Git).
- Resolución, volumen, dificultad e idioma se ajustan desde el menú o el `.ini`.
````

> Se retira `-DGRAPHICS_HEADLESS=0`: **no es una opción de Raylib** (inventada en la versión
> anterior del plan). El artefacto se unifica como `rts_game.exe`.

---

### FASE 1: Reestructuración modular

#### 4.4 `src/config.h` (corregido respecto a la versión anterior)

```cpp
// src/config.h — Configuración global del juego
#pragma once

#include <string>
#include <vector>
#include <cmath>
#include "raylib.h"

// === PANTALLA ===
// OJO: son variables, NO constantes. El código actual las reasigna cada frame
// (rts_game.cpp:4694, :4730-4731) y al redimensionar (:3811).
// Usar `constexpr`/`const` rompería la compilación.
inline int SCREEN_W = 1280;
inline int SCREEN_H = 768;

// === PALETA (nombres originales del código, para no renombrar call sites) ===
inline const Color C_BG        = {14, 12, 10, 255};
inline const Color C_GOLD      = {200, 165, 80, 255};
inline const Color C_COPPER    = {160, 80, 40, 255};
inline const Color C_PARCHMENT = {230, 220, 200, 255};
inline const Color C_SECONDARY = {140, 130, 110, 255};
inline const Color C_BLOOD     = {160, 30, 20, 255};
inline const Color C_ALLY      = {80, 160, 220, 255};
inline const Color C_ENEMY_COL = {220, 60, 50, 255};
inline const Color C_TERRAIN   = {42, 54, 32, 255};

// === ESCALA DE UI (:46-52) — usada por ui.h (uiFS/uiPx) ===
inline float g_uiScaleDraw = 1.f;
inline int   uiFS(int fs){ return (int)roundf((float)fs*g_uiScaleDraw); }
inline float uiPx(float px){ return px*g_uiScaleDraw; }

// === ENUMS (todos existen en rts_game.cpp) ===
enum GameState {
    STATE_MAIN_MENU=0, STATE_CAMPAIGN_MAP, STATE_CITY_MANAGEMENT,
    STATE_RECRUITMENT, STATE_PRE_BATTLE, STATE_BATTLE, STATE_BATTLE_RESULT,
    STATE_UNIT_CODEX, STATE_SETTINGS, STATE_UNIT_EDITOR,
    STATE_QUICK_BATTLE_SETUP, STATE_VICTORY, STATE_DEFEAT, STATE_MARKETPLACE
    // STATE_COUNT  // Mejora opcional: añadir solo al final, no cambia valores
};

enum SpriteBase   { SPR_INFANTRY=0, SPR_CAVALRY, SPR_RANGED, SPR_BASE_COUNT };
enum BuildingType {
    BLD_MARKET=0, BLD_FARM, BLD_SAWMILL, BLD_QUARRY, BLD_SMITHY,
    BLD_BARRACKS, BLD_STABLE, BLD_RANGE, BLD_WORKSHOP,
    BLD_WALLS1, BLD_WALLS2, BLD_MAGETOWER, BLD_TEMPLE, BLD_PORT, BLD_COUNT
};
enum FactionId     { FACTION_PLAYER=0, FACTION_AGGRESSIVE, FACTION_DEFENSIVE,
                     FACTION_COMMERCIAL, FACTION_NEUTRAL, FACTION_COUNT };
enum TerrainType   { TERRAIN_PLAIN=0, TERRAIN_FOREST, TERRAIN_MOUNTAIN, TERRAIN_COAST };
enum SoldierState  { SS_IDLE=0, SS_MOVING_SLOT, SS_MOVING_TARGET,
                     SS_ATTACKING_MELEE, SS_ATTACKING_RANGED, SS_FLEEING };
enum UnitGroupState{ UGS_IDLE=0, UGS_ADVANCING, UGS_ENGAGED, UGS_ROUTING };
enum TradeResource { TRADE_GOLD=0, TRADE_FOOD, TRADE_WOOD, TRADE_STONE, TRADE_IRON };

// === AJUSTES ===
struct GameSettings {
    int   screenW       = 1280;   // unificado: era 1280 en el struct (:700)
    int   screenH       = 768;    // unificado: era 720 en el struct (:701)
    bool  fullscreen    = false;
    float uiScale       = 1.0f;
    float masterVolume  = 1.0f;
    float musicVolume   = 0.7f;
    int   difficulty    = 1;      // 0=EASY,1=NORMAL,2=HARD
    bool  showFPS       = false;
    int   language      = 0;      // 0=EN,1=ES
};

// === RECURSOS (:423-425) — lo usa CampaignState ===
struct Resources {
    float gold=500.f, food=200.f, wood=100.f, stone=50.f, iron=50.f;
};

// === CONSTANTES (nombres y valores reales del código) ===
inline const int HUD_H     = 96;    // HUD inferior en batalla   (:40)
inline const int TOPBAR_H  = 32;    // Barra superior en batalla (:41)
inline const int BATTLE_W  = 2560;  // Tamaño del campo de batalla (:42)
inline const int BATTLE_H  = 1536;  //                             (:43)

inline const char* SETTINGS_FILE = "settings.ini";  // (:698)
inline const int   SAVE_VERSION  = 5;               // (:24)
```

**Corregido respecto a la versión anterior del plan:**
- `SCREEN_W/H` eran `constexpr` → **no compilaban** con el código actual (se reasignan).
- Se eliminan constantes inventadas: `HUD_ROW_HEIGHT=45`, `UPGRADE_COST_*`,
  `HUD_TOP_BAR_HEIGHT` y `SETTINGS_SAVE_FILE` (la real es `SETTINGS_FILE`).
- Se conservan los nombres originales (`HUD_H`, `TOPBAR_H`, `C_ENEMY_COL`…) para no
  renombrar cientos de puntos de uso.

#### 4.5 `src/util.h` — se conserva el RNG actual

```cpp
// src/util.h — Funciones utilitarias (nombres reales del código)
#pragma once

#include "config.h"
#include <cstdlib>
#include <cmath>
#include <random>

// --- RNG ACTUAL (rts_game.cpp:750-752). NO sustituir por frandU()/GetTime():
// --- cambiaría el muestreo de combates y generaciones sin beneficio. ---
extern std::mt19937 g_rng;                                   // definido en util.cpp
inline float frandMT(){ return std::uniform_real_distribution<float>(0.f,1.f)(g_rng); }
inline int   randIntMT(int lo,int hi){ return std::uniform_int_distribution<int>(lo,hi)(g_rng); }

inline unsigned char clampU8(int v){
    return static_cast<unsigned char>(v<0?0:(v>255?255:v));
}
inline float frand(){ return static_cast<float>(rand())/RAND_MAX; }  // legacy (:84)

inline float vdist(Vector2 a, Vector2 b){ float dx=a.x-b.x, dy=a.y-b.y; return sqrtf(dx*dx+dy*dy); }
inline Vector2 vnorm(Vector2 v){
    float l=sqrtf(v.x*v.x+v.y*v.y);
    if(l<0.0001f) return {0.f,0.f};
    return {v.x/l, v.y/l};
}
inline float dirToAngle(Vector2 d){ return atan2f(-d.y,d.x)*RAD2DEG; }
inline float lerpAngle(float c,float t,float s){
    float diff=t-c;
    while(diff>180.f) diff-=360.f;
    while(diff<-180.f) diff+=360.f;
    return c+diff*s;
}
inline bool ptInRect(Vector2 p, Rectangle r){
    return p.x>=r.x && p.x<=r.x+r.width && p.y>=r.y && p.y<=r.y+r.height;
}

// --- Helpers vectoriales usados por decenas de puntos (:88-92) ---
inline Vector2 v2add(Vector2 a, Vector2 b){ return {a.x+b.x, a.y+b.y}; }
inline Vector2 v2sub(Vector2 a, Vector2 b){ return {a.x-b.x, a.y-b.y}; }
inline Vector2 v2scale(Vector2 a, float s){ return {a.x*s, a.y*s}; }
inline float   v2dot(Vector2 a, Vector2 b){ return a.x*b.x + a.y*b.y; }
inline float   v2len(Vector2 a){ return sqrtf(a.x*a.x + a.y*a.y); }
```

#### 4.6 `src/ui.h` — incluir la estrategia de macros

```cpp
// src/ui.h — Helpers de interfaz
#pragma once

#include "config.h"
#include "util.h"
#include <string>
#include <cstdio>

// Nombres reales del código (:54-61)
inline void DrawTextRaw(const char* t,int x,int y,int fs,Color c){ ::DrawText(t,x,y,fs,c); }
inline int  MeasureTextRaw(const char* t,int fs){ return ::MeasureText(t,fs); }
inline void DrawTextUI (const char* t,int x,int y,int fs,Color c){ DrawTextRaw(t,x,y,uiFS(fs),c); }
inline int  MeasureTextUI(const char* t,int fs){ return MeasureTextRaw(t,uiFS(fs)); }

// CRÍTICO: el código llama a DrawText/MeasureText en ~1.000 sitios (:63-64).
// Mantener estas macros evita renombrarlos todos:
#ifndef DrawText
#define DrawText    DrawTextUI
#endif
#ifndef MeasureText
#define MeasureText MeasureTextUI
#endif

// Firmas reales (declaraciones; definiciones en ui.cpp)
bool drawButton(Rectangle r, const char* lbl, Vector2 m,
                Color cn={40,55,40,255}, Color ch={70,110,60,255});            // (:872)
bool drawSmBtn(Rectangle r, const char* lbl, Vector2 m,
               Color cn={30,40,30,255}, Color ch={55,90,45,255});              // (:888)
int drawIntSlider(Rectangle r, int val, int mn, int mx,
                  const char* label, Vector2 mouse,
                  Color fill={80,160,80,200});                                 // (:904)
float drawFloatSlider(Rectangle r, float val, float mn, float mx,
                      const char* label, const char* fmt, Vector2 mouse,
                      Color fill={80,160,80,200});                             // (:924)
void drawWrappedText(const char* text,int x,int y,int maxWidth,int fs,Color col); // (:963)
```

> **Correcciones vs la versión anterior:** no existen `drawSlider`, `wrapText` ni
> `toFormat` en el código (los reales son `drawIntSlider`/`drawFloatSlider`,
> `drawWrappedText`). Se documenta la macro `#define DrawText DrawTextUI`, que era
> el detalle que faltaba para que la migración no rompiera las pantallas.

#### 4.7 `src/sprite.h`

```cpp
// src/sprite.h — Generación procedural de sprites
#pragma once
#include "config.h"

// Firmas reales (:216, :262, :305, :349, :356)
Image     makeInfantrySprite(unsigned char r, unsigned char g, unsigned char b, int weaponHint);
Image     makeCavalrySprite (unsigned char r, unsigned char g, unsigned char b);
Image     makeRangedSprite  (unsigned char r, unsigned char g, unsigned char b, bool crossbow);
Texture2D imageToTex(Image img);
void      rebuildTexture(int idx);   // envuelve la lambda makeImg (:356-374)
```

> `makeUnitTexture` (nombre de la versión anterior del plan) **no existe**: la función
> real es `rebuildTexture(int idx)`, que usa las lambdas `makeImg`/`setOrPush`.

#### 4.8 `src/combat.h` — `BattleState` con los campos REALES

```cpp
// src/combat.h — Lógica de combate
#pragma once
#include "config.h"
#include "elements.h"
#include <vector>

struct BattleState {                       // réplica exacta de rts_game.cpp:610-643
    std::vector<BattleUnit>  playerUnits;
    std::vector<BattleUnit>  enemyUnits;
    std::vector<Projectile>  projs;        // NO "projectiles"
    std::vector<DeadMarker>  dead;         // NO "deadMarkers"
    Camera2D  cam;                         // NO "camera"
    float     camZoom;                     // NO "cameraZoom"
    std::vector<Rectangle>   obstacles;    // AABB contra obstáculos del terreno (:618)
    TerrainType terrain;
    float     timeScale;
    bool      paused;
    bool      dragging;                    // selección por arrastre
    Vector2   selStart;
    Rectangle selRect;
    int       battleProvince;
    bool      isDefense;
    bool      battleOver;
    bool      playerWon;
    float     resultTimer;
    float     lootGold;
    char      scenarioName[64];
    ControlGroup controlGroups[9];
    int       activeControlGroup;
    // NO existe campo "phase" (el comentario "VISTA_0..3" del plan anterior era inventado)
};

// ControlGroup (definido en elements.h, réplica de :601-605):
//   struct ControlGroup { std::vector<int> unitIndices;
//                         std::vector<Vector2> relOffsets; bool active=false; };
//
// NOTA: no existe ninguna función tipo getUnitRadius/getSoldierRadius en el código
// (el nombre de la versión anterior era inventado). El espaciado de soldados vive en
// calcFormationSlots (:1404).
```

#### 4.9 `src/campaign.h` — nombres y campos reales

```cpp
// src/campaign.h — Mapa de campaña, provincias, turnos
#pragma once
#include "config.h"
#include <string>
#include <vector>
#include <utility>

struct City {                       // rts_game.cpp:455-461
    char     name[32];              // char[32], NO std::string[32]
    bool     built[BLD_COUNT];
    int      constructing;          // -1 = nada
    int      constructTurns;
    float    defBonus;
};

struct Province {                   // rts_game.cpp:475-486
    char       name[32];
    Vector2    center;
    float      nx, ny;              // posición normalizada [0-1]
    TerrainType terrain;
    FactionId  owner;               // NO "faction"
    bool       hasCity;
    City       city;                // ciudad anidada, NO "cityBuilding"
    std::vector<int> adjacent;
    std::vector<std::pair<int,int>> army;   // {typeIdx, soldierCount}
};

// Entrada de cola de reclutamiento (:558-562) — debe ir antes de CampaignState
struct RecruitEntry {
    int typeIdx;
    int turnsLeft;
    int originalTurns;
};

struct CampaignState {              // rts_game.cpp:567-594 — NO se llama "Campaign"
    int   campaignId = 0;
    int   turn       = 1;
    Resources res;
    int   playerProvince = 0;
    std::vector<Province> provinces;
    std::vector<std::pair<int,int>> playerArmy;
    int   pendingBattleProvince = -1;
    bool  pendingBattleIsDefense = false;
    int   viewedCity = -1;
    std::vector<RecruitEntry> recruitQueue;
    std::vector<std::pair<int,int>> readyUnits;
    bool  explored[32] = {};
    int   battlesWon = 0, battlesLost = 0, peakProvinces = 0;
    int   armyMoveFrom = -1, armyMoveTo = -1;
    float armyMoveT = 0.f;
};
```

#### 4.10 `src/city.h`

```cpp
// src/city.h — Construcciones y recursos de ciudad
#pragma once
#include "config.h"

// Las operaciones sobre City viven en city.cpp; la struct definida en campaign.h (:455).
// Tablas de edificios reales (:441-453):
extern const int   bldGoldCost[BLD_COUNT];
extern const int   bldWoodCost[BLD_COUNT];
extern const int   bldStoneCost[BLD_COUNT];
extern const int   bldIronCost[BLD_COUNT];
extern const int   bldTurns[BLD_COUNT];
extern const int   bldPrereq[BLD_COUNT];
```

> Se retira el `struct City { std::string name[32]; bool hasBarracks; ... }` de la versión
> anterior: era **erróneo** (`name[32]` como `std::string` serían 32 cadenas; los flags
> `hasBarracks/hasStable/hasMarket` no existen; lo real es `built[BLD_COUNT]`).

#### 4.11 `src/save.h` — firmas reales

```cpp
// src/save.h — Serialización binaria (formato actual: binario con SAVE_VERSION=5)
#pragma once

void saveGame();          // rts_game.cpp:4463 — SIN parámetros
bool loadGame();          // rts_game.cpp:4530 — SIN parámetros
// Usa campaign_%d.dat / campaign_last.dat (getCampaignSavePath, :28-33)
// fwrite/fread con SAVE_VERSION (:4467, :4541). Compatibilidad: carga saves v5.
```

#### 4.12 `src/main.cpp` y ensamblado

```cpp
// src/main.cpp — Punto de entrada (lógica actual de main(), :4691-4816)
#include "raylib.h"
#include "config.h"
#include "util.h"
#include "ui.h"
#include "sprite.h"
#include "terrain.h"
#include "elements.h"
#include "combat.h"
#include "campaign.h"
#include "city.h"
#include "camera.h"
#include "save.h"
```

---

## 5. ARQUITECTURA DETALLADA

### 5.1 Estructura de módulos

```
medieval-conquest/
├── src/
│   ├── config.h / config.cpp   constantes, enums, GameSettings, loadSettings/saveSettings
│   ├── util.h / util.cpp      matemática, RNG (frandMT/randIntMT), helpers vectoriales
│   ├── ui.h / ui.cpp           DrawTextUI, drawButton, drawSmBtn, sliders, drawWrappedText
│   ├── camera.h / camera.cpp   zoom, seguimiento, lerp
│   ├── sprite.h / sprite.cpp   makeInfantry/Cavalry/RangedSprite, imageToTex, rebuildTexture
│   ├── terrain.h / terrain.cpp generación de obstáculos AABB por tipo de terreno
│   ├── elements.h / elements.cpp  UnitTypeDef, Soldier, BattleUnit, Projectile,
│   │                               DeadMarker, ControlGroup, formaciones
│   ├── combat.h / combat.cpp   BattleState, cálculo de daño, proyectiles, IA de unidad
│   ├── campaign.h / campaign.cpp   Province, CampaignState, turnos, generación de mapa
│   ├── city.h / city.cpp       edificios, producción, reclutamiento
│   ├── save.h / save.cpp       saveGame/loadGame (binario v5)
│   └── main.cpp                bucle principal, transiciones de estado, pantallas
├── build/                      *.obj (ignorado)
├── settings.example.ini        versionado
├── settings.ini                local (ignorado)
├── README.md
├── PLAN.md
└── rts_game.cpp                (✓ eliminado en la FASE 1; ver Estado de ejecución)
```

> **Unificado:** el plan anterior mezclaba `units.h` (§5.1) con `elements.h` (§2/§6/§9)
> y `src/` con `include/`. Ahora es **`elements.h` y todo en `src/`**.

### 5.2 Relación de dependencias (propuesta)

> Notación: `X ◄── Y` = "el módulo Y incluye a X". La relación es **acíclica**
> (de arriba abajo: utilidades → tipos → lógica → pantallas → main).

```
config.h    ◄──  todos los módulos
util.h      ◄──  ui, sprite, terrain, elements, combat, campaign, city, main
elements.h  ◄──  combat, campaign, main
ui.h        ◄──  main, campaign, city, combat (todas las pantallas)
camera.h    ◄──  combat, main
sprite.h    ◄──  terrain, campaign, main
terrain.h   ◄──  combat, campaign
combat.h    ◄──  campaign, main
campaign.h  ◄──  city, save, main
city.h      ◄──  main
save.h      ◄──  main
```

> **Corrección:** la versión anterior del plan se contradecía a sí misma: en §5.2 ponía
> `config.h ──► todos` (config es incluido por todos) y en §5.3 decía "config.h incluye
> todos los demás". Con la notación `◄──` queda explícito y sin ambigüedad.

### 5.3 Diagrama de dependencias y artefactos

```
medieval-conquest/
│
├── src/            12 módulos (ver §5.1)
│                   config.h es el único header "central" (lo incluye todo)
│
├── build/          objetos: config.o, util.o, ui.o, camera.o, sprite.o,
│                   terrain.o, elements.o, combat.o, campaign.o, city.o,
│                   save.o, main.o
│
├── Raylib          provisto por el toolchain (raylib.h / raymath.h)
│                   NO se copia al repositorio
│
└── rts_game.exe    artefacto final (ignorado en Git tras la FASE 0)
```

> **Corrección:** `raylib/math.h` no existe en Raylib (los headers son `raylib.h` y
> `raymath.h`). Y `include/` ya no participa en este esquema (decisión §2).

---

## 6. CAMBIOS DETALLADOS POR ARCHIVO

### 6.1 Mapeo de funciones a módulos (nombres verificados contra el código)

| Función original (`rts_game.cpp`) | Archivo destino | Línea |
|---|---|---|
| `DrawTextUI`, `DrawTextRaw`, `MeasureTextUI`, `MeasureTextRaw` + macros `DrawText`/`MeasureText` | `src/ui.h` | :54-64 |
| `drawButton`, `drawSmBtn` | `src/ui.cpp` | :872, :888 |
| `drawIntSlider`, `drawFloatSlider` | `src/ui.cpp` | :904, :924 |
| `drawWrappedText` | `src/ui.cpp` | :963 |
| `frandMT`, `randIntMT`, `g_rng` | `src/util.h` + `src/util.cpp` | :750-752 |
| `clampU8`, `vdist`, `vnorm`, `lerpAngle`, `v2*` | `src/util.h` | :69-92 |
| `makeInfantrySprite`, `makeCavalrySprite`, `makeRangedSprite`, `imageToTex`, `rebuildTexture` | `src/sprite.cpp` | :216, :262, :305, :349, :356 |
| `calcFormationSlots` | `src/elements.cpp` | :1404 |
| `loadSettings`, `saveSettings`, `SETTINGS_FILE` | `src/config.cpp` | :698-745 |
| `saveGame`, `loadGame` (sin parámetros) | `src/save.cpp` | :4463, :4530 |
| `BattleState` y lógica de combate | `src/combat.cpp` | :610 |
| `CampaignState`, `Province`, `City`, `newCampaign` | `src/campaign.cpp` | :455, :567 |
| `initBuiltinTypes`, `UnitTypeDef` | `src/elements.cpp` | :157 |

**Eliminados de la tabla anterior por no existir en el código:** `drawSlider`,
`wrapText` (real: `drawWrappedText`), `makeTexture` (real: lambda `makeImg`, :358),
`randInt` (real: `randIntMT`).

### 6.2 Recuento de archivos

- **12 archivos `.cpp`**: `config`, `util`, `ui`, `camera`, `sprite`, `terrain`,
  `elements`, `combat`, `campaign`, `city`, `save`, `main`.
- **11 headers `.h`**: los mismos módulos excepto `main.cpp` (que no tiene header).

---

## 7. COMPILACIÓN

### 7.1 Build manual (comandos **separados**, uno por fichero)

```bash
# Unix: mkdir -p build   |   Windows (cmd/PowerShell): mkdir build

g++ -std=c++17 -Isrc -O2 -c src/config.cpp   -o build/config.o
g++ -std=c++17 -Isrc -O2 -c src/util.cpp     -o build/util.o
g++ -std=c++17 -Isrc -O2 -c src/ui.cpp       -o build/ui.o
g++ -std=c++17 -Isrc -O2 -c src/camera.cpp   -o build/camera.o
g++ -std=c++17 -Isrc -O2 -c src/sprite.cpp   -o build/sprite.o
g++ -std=c++17 -Isrc -O2 -c src/terrain.cpp  -o build/terrain.o
g++ -std=c++17 -Isrc -O2 -c src/elements.cpp -o build/elements.o
g++ -std=c++17 -Isrc -O2 -c src/combat.cpp   -o build/combat.o
g++ -std=c++17 -Isrc -O2 -c src/campaign.cpp -o build/campaign.o
g++ -std=c++17 -Isrc -O2 -c src/city.cpp     -o build/city.o
g++ -std=c++17 -Isrc -O2 -c src/save.cpp     -o build/save.o
g++ -std=c++17 -Isrc -O2 -c src/main.cpp     -o build/main.o

g++ -o rts_game.exe build/*.o -lraylib -lopengl32 -lgdi32 -lwinmm
# En cmd/PowerShell no hay globbing: listar build\*.o explícitamente.
```

**Correcciones respecto a la versión anterior:**
- Los guiones partidos `-c src/terrain.cpp-    o build/terrain.o` y
  `-c src/elements.cpp - o ...` eran **errores de sintaxis**.
- Un único `g++` con varios `-c ... -o` es **inválido en GCC**
  (*cannot specify '-o' with '-c' when compiling multiple input files*): por eso se separan.
- `-Iinclude/` → **`-Isrc`** (decisión de layout).
- Se añaden las libs de sistema que Raylib necesita en Windows MinGW
  (`-lopengl32 -lgdi32 -lwinmm`); enlazar con `-lraylib` requiere la **librería de
  importación** (`libraylib.dll.a` o `raylib.lib`), que **no está en el repo** (y `*.lib`
  está hoy en el `.gitignore`).
  → **Toolchain verificado durante la FASE 1:** w64devkit GCC 14.2
  (`C:\raylib\w64devkit\bin` debe estar en `PATH`) + raylib 5.5 del sysroot, enlace
  contra `libraylib.a` estático. El bloque anterior compila y enlaza en Windows
  (build de los 12 módulos + smoke test realizados).

### 7.2 Build con CMake (opcional)

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(MedievalConquest CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/config.cpp src/util.cpp src/ui.cpp src/camera.cpp
    src/sprite.cpp src/terrain.cpp src/elements.cpp src/combat.cpp
    src/campaign.cpp src/city.cpp src/save.cpp src/main.cpp
)

add_executable(rts_game ${SOURCES})

target_include_directories(rts_game PRIVATE "${PROJECT_SOURCE_DIR}/src")

# Raylib: preferir find_package si el toolchain lo instala;
# si no, link_directories() a la carpeta que contiene libraylib.dll.a / raylib.lib
find_package(raylib QUIET)
if(raylib_FOUND)
    target_link_libraries(rts_game PRIVATE raylib)
else()
    message(FATAL_ERROR "Raylib no encontrado: indicar su ruta de instalación")
endif()
```

```bash
cmake -B cmake-build
cmake --build cmake-build
```

**Correcciones:** el target se llamaba `game.exe` pero se referenciaba como `game`
(error de CMake); se usa `rts_game` (consistente con `rts_game.exe`); falta
`find_package`; y la carpeta de build es **`cmake-build/`** para no mezclar los `.o`
del §7.1 dentro de `build/`.

---

## 8. GESTIÓN DE VERSIONES (git)

### 8.1 `.gitignore`

Ver **§4.1**: existe una única versión canónica allí (la anterior tenía dos propuestas
distintas e incompatibles).

### 8.2 Pasos de saneamiento (✅ EJECUTADOS el 2026-10-08 — staged, pendientes de commit)

```bash
# 1. Quitar los binarios ya trackeados (el .gitignore no los limpiará solo)
git rm --cached rts_game.exe libraylib.dll glfw3.dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll

# 2. Quitar la configuración local y pasar a usar el example
git rm --cached settings.ini
git add settings.example.ini

# 3. Aplicar el .gitignore unificado y trackear los archivos de trabajo
git add .gitignore PLAN.md code-reviewer.agent.md

# 4. Commit separado y con mensaje claro
git commit -m "Saneamiento: fuera binarios y settings local, .gitignore unificado"
```

> **Decisión tomada:** `settings.ini` deja de trackearse (la regla `.gitignore` pasa a
> surtir efecto) y se versiona `settings.example.ini`. Si en el futuro se quisiera
> compartir configuración de equipo, sería **uno u otro**, no ambos.

### 8.3 `.gitattributes` (opcional, solo si se usa Git LFS)

```gitattributes
*.exe filter=lfs diff=lfs merge=lfs
*.dll filter=lfs diff=lfs merge=lfs
```

> **Advertencia:** si se declara `filter=lfs` sin tener LFS instalado, cualquier persona
> que clone el repo no podrá leer esos archivos. Además **contradice la política
> "ningún binario en Git"** (§11.A): si los binarios se descartan con `git rm --cached`,
> este archivo no hace falta. Elegir una de las dos vías.

---

## 9. CHECKLIST DE IMPLEMENTACIÓN

### FASE 0 — Preparación ✅ COMPLETADA

- [x] `.gitignore` unificado (§4.1) y entrada `.git/*` eliminada.
- [x] `git rm --cached` de `rts_game.exe` + 5 DLLs (§8.2).
- [x] `settings.example.ini` creado (§4.2).
- [x] `README.md` inicial (§4.3, ampliado con el build modular verificado).

### FASE 1 — Reestructuración modular ✅ COMPLETADA (build + smoke test verificados)

- [x] `src/config.h` + `config.cpp` — `SCREEN_W/H` **mutables**, paleta, enums, `GameSettings`, `loadSettings`/`saveSettings`.
- [x] `src/util.h`/`util.cpp` — `frandMT`, `randIntMT` (RNG actual), `clampU8`, `vdist`, `vnorm`, `lerpAngle`, `v2*`.
- [x] `src/ui.h`/`ui.cpp` — `DrawTextUI` + **macros `DrawText`/`MeasureText`**, `drawButton`, `drawSmBtn`, `drawIntSlider`, `drawFloatSlider`, `drawWrappedText`.
- [x] `src/camera.h`/`camera.cpp` — zoom, seguimiento, interpolación (extraído de `updateDrawBattle`/`initBattle`).
- [x] `src/sprite.h`/`sprite.cpp` — `makeInfantrySprite`, `makeCavalrySprite`, `makeRangedSprite`, `imageToTex`, `rebuildTexture`.
- [x] `src/terrain.h`/`terrain.cpp` — obstáculos AABB por tipo de terreno (extraído de `initBattle`).
- [x] `src/elements.h`/`elements.cpp` — `UnitTypeDef`, `Soldier`, `BattleUnit`, `Projectile`, `DeadMarker`, `ControlGroup`, `calcFormationSlots`.
- [x] `src/combat.h`/`combat.cpp` — `BattleState` con campos reales (§4.8).
- [x] `src/campaign.h`/`campaign.cpp` — `Province`, `CampaignState`, `City` (§4.9).
- [x] `src/city.h`/`city.cpp` — tablas `bld*` y lógica de construcción.
- [x] `src/save.h`/`save.cpp` — `saveGame()`/`loadGame()` sin parámetros, binario v5.
- [x] `src/main.cpp` — bucle principal (`:4691-4816`).
- [x] `rts_game.cpp` eliminado del árbol y del índice (estaba en §5.1).

### FASE 2 — Unificación de configuración ✅ COMPLETADA

- [x] Defaults de resolución unificados en **1280×768** (`GameSettings.screenH` 720→768 en `config.h`; `SCREEN_W/H` ya era 1280×768).
- [x] `git rm --cached settings.ini`.
- [x] Verificado el fallback: sin `settings.ini` el juego arranca con los defaults (probado, corre).
- [x] Rangos validados: `uiScale 1.0–2.25` (clamp en `loadSettings`), volúmenes 0–1, `difficulty 0–2`.
- [x] El juego arranca con el `.ini` local si existe (probado) y con defaults si no.

### FASE 3 — Mejoras técnicas (con Estado verificado contra el código)

| Versión | Mejora descrita en el plan | Estado real en el código | Evidencia |
|---|---|---|---|
| (v7.0/v7.2) | Sistema de formaciones en línea y facing | **Hecho** | Banner `BATTLE FORMATION HELPERS (v7.0...)` `:1402-1404`; `lastFormationFacing` (v7.2) `:532`, `:1505` |
| v8.1 | (el checklist anterior lo asociaba a formaciones) | **Hecho — pero es otra cosa** | El v8.1 real es "anchor follows centroid" (movimiento), `:1733` y `:2008` |
| v8.2 | Separación suave entre unidades en formación | **Pendiente** | El v8.2 real es movimiento agresivo/lineal (`:1773`, `:1888`, `:1982`); solo hay AABB contra obstáculos del terreno (`:1597`) |
| v8.3 | IA con flanqueo | **Hecho (parcial)** | Flanqueo de caballería en `:2099-2109` (`orderType=3`, `:530`); el v8.3 real es avance ultra-agresivo (`:2026`, `:2033`) |
| v8.3 | IA con cover-seeking | **Pendiente** | No existe ninguna referencia a cobertura en el código |
| v8.4 | Sistema de misiones | **Pendiente** | No hay rastro de misiones/objetivos/recompensas (no existe ninguna etiqueta v8.4) |

- [ ] v8.2 — Separación suave entre unidades (Pendiente).
- [ ] v8.3 — Cover-seeking (Pendiente); el flanqueo ya está hecho.
- [ ] v8.4 — Sistema de misiones (Pendiente).

### FASE 4 — Documentación y CI

- [ ] `README.md` completo: requisitos, compilar, ejecutar, jugar, configuración.
- [ ] `ARCHITECTURE.md` — diagramas, módulos, decisiones de diseño (§5).
- [ ] `CMakeLists.txt` **o** `Makefile` funcional (§7).
- [x] `.gitignore` consolidado (§4.1) y binarios fuera del índice (§8.2) — *hecho en FASE 0*.
- [ ] Compilación y ejecución verificadas en Windows con las DLLs en PATH.
- [ ] Commit de la reestructuración con mensaje claro.

---

## 10. RIESGOS Y MITIGACIONES

| Riesgo | Probabilidad | Impacto | Mitigación |
|---|---|---|---|
| Error de compilación por orden/alcance de includes | Media | Alto | Compilación incremental por módulo; orden dado en §7.1 |
| Funcionalidad rota tras dividir el archivo (sin tests de regresión) | **Alta** | Alto | No hay ningún framework de tests en el repo: validar manualmente cada pantalla (14 estados de `GameState`) tras cada módulo extraído; hacer commits pequeños y reversibles |
| **Enlace de Raylib en Windows** ✅ **RESUELTO**: toolchain local = **w64devkit GCC 14.2** (`C:\raylib\w64devkit`) + **raylib 5.5** (`libraylib.a` estático en el sysroot); el repo no contiene import libs | ~~Alta~~ Resuelta | Alto | Build verificado en la FASE 1: `PATH=C:\raylib\w64devkit\bin` + `g++ -std=c++17 -Isrc … -lraylib -lopengl32 -lgdi32 -lwinmm` (comando exacto en el README) |
| Prolongación de la estimación (6–8,5 h) | Media | Medio | Segmentar por módulo con compilación tras cada uno |
| Cambios no deseados al tocar `settings.ini`/defaults | Baja | Medio | Des-tracking + `settings.example.ini` (§8.2) |
| Añadir `filter=lfs` sin Git LFS instalado | Baja | Medio | Solo aplicar §8.3 si se confirma LFS; si no, descartar binarios |
| Compatibilidad de partidas guardadas | Baja | Medio | Mantener `SAVE_VERSION=5` y el formato binario actual (§11.B) |
| Dependencia de una versión exacta de Raylib desconocida | Media | Medio | Confirmar la versión de `libraylib.dll` y fijarla en el README antes de migrar |

---

## 11. ANEJO

### A. Decisiones de diseño

- **Single-file → modular:** por mantenibilidad y tiempo de compilación, no por obligación.
- **Layout único `src/` + `-Isrc`:** todos los `.h` junto a sus `.cpp`. **No se usa
  `include/`** (era la contradicción §2/§7/§11 del documento anterior). Raylib no se
  vendoriza: llega del toolchain.
- **Macros de texto conservadas:** `#define DrawText DrawTextUI` y
  `#define MeasureText MeasureTextUI` se mantienen para no renombrar ~1.000 call sites.
- **Nombres originales conservados** (`HUD_H`, `C_ENEMY_COL`, `BattleState::projs`…):
  renombrar es una mejora posterior, no parte de la reestructuración.
- **RNG conservado:** `std::mt19937 g_rng` + `frandMT`/`randIntMT` (`:750-752`). Se descarta
  el `frandU()` propuesto (semilla por `GetTime`), que cambiaría el muestreo sin beneficio.
- **Ningún binario en Git (aún no es cierto):** hoy `rts_game.exe` y 5 DLLs están
  trackeados. La política se vuelve real con `git rm --cached` (§8.2) + las reglas de §4.1.

### B. Compatibilidad y guardado

- **Serialización: binario, ya implementado.** `SAVE_VERSION = 5` (`:24`) con
  `fwrite`/`fread` (`:4467`, `:4541`) sobre `campaign_%d.dat` y `campaign_last.dat`.
  La versión anterior del plan decía "JSON o binario, se decide según `SAVE_FORMAT`":
  la decisión ya está tomada. Mantener v5 al migrar a módulos.

### C. Referencias

- Raylib: https://www.raylib.com/ · https://github.com/raysan5/raylib
- `raymath.h` (matemáticas de Raylib): https://github.com/raysan5/raylib/blob/master/src/raymath.h
- GNU Make: https://www.gnu.org/software/make/
- CMake: https://cmake.org/cmake/help/latest/

---

## Estado de ejecución (2026-10-08)

| Fase | Estado | Evidencia |
|---|---|---|
| FASE 0 Preparación | ✅ Completada | `.gitignore` unificado; `rts_game.exe` + 5 DLLs + `settings.ini` fuera del índice (staged); `settings.example.ini` y README creados |
| FASE 1 Modular | ✅ Completada | 12 `.cpp` + 11 `.h` en `src/`; **los 12 compilan** (GCC 14.2); enlace OK (2,82 MB); **smoke test 6 s sin crash**; `rts_game.cpp` eliminado del árbol e índice |
| FASE 2 Config | ✅ Completada | Default único 1280×768; `git rm --cached settings.ini`; fallback sin `.ini` probado en ejecución |
| FASE 3 Mejoras (v8.2/v8.3/v8.4) | ⬜ Pendiente | Tabla de Estado más abajo |
| FASE 4 Docs/CI | ⬜ Pendiente | README ✅ parcial; `ARCHITECTURE.md`, CMake/Makefile y CI pendientes |

- **Toolchain resuelto** (riesgo §10 cerrado): `C:\raylib\w64devkit\bin` en `PATH` + raylib 5.5.
- **Git:** nada commitado aún; los cambios (`rm --cached`, adds, borrado de `rts_game.cpp`) están **staged** a la espera de confirmación.
- Correcciones menores hechas al ejecutar: argumentos por defecto duplicados en `ui.cpp` (solo en `ui.h`); `combat.cpp` necesita `campaign.h` (`processTurn` lee `g_campaign`); definiciones de `g_battle`/`g_lastResult`/`g_campaign`/`g_preBattle` añadidas a sus módulos; `main.cpp` sin header propio.

---

## Estado del plan

| Sección | Estado de la revisión |
|---|---|
| §0 Verificación de datos | Nueva — datos comprobados en repo |
| §1 Estado actual | Corregido (cifras, árbol, problemas A–E) |
| §2 Hoja de ruta | Corregida (layout `src/`, FASE 2 reformulada) |
| §3 Cronograma | Corregido (total **6–8,5 h**) |
| §4 Detalles | Corregido (config, RNG, structs, firmas, scripts) |
| §5 Arquitectura | Corregido (nombres de módulos y dependencias) |
| §6 Mapeo | Corregido (solo funciones que existen) |
| §7 Compilación | Corregido (comandos válidos, CMake válido) |
| §8 Git | Corregido (pasos `git rm --cached`) |
| §9 Checklist | Completado + tabla Estado para v8.x |
| §10 Riesgos | Ampliado (enlace Raylib, ausencia de tests) |
| §11 Anexo | Corregido (layout, RNG, guardado binario) |

**Siguiente hito:** FASE 3 (v8.2 separación suave, cover-seeking, sistema de misiones)
y, tras ella, FASE 4 (`ARCHITECTURE.md`, build system, CI). El saneamiento de Git
(FASE 0) y la reestructuración (FASE 1–2) están **staged sin commit**, a la espera
de confirmación.

> **Actualización (2026-10-10):** FASES 0–2 commitadas y cerradas; tras ellas se
> ejecutaron las fases de contenido y red descritas en **§12** (ya ejecutadas).
> La FASE 3/4 original (v8.2–v8.4, ARCHITECTURE.md, CI) sigue pendiente.

---

## 12. FASES POSTERIORES (contenido, campañas y red) — 2026-10-08 → 2026-10-10

Tras la reestructuración modular se ejecutó un plan de fases de contenido y
tecnología, **una fase = un commit**, en este orden:

| Fase | Commit | Contenido |
|---|---|---|
| Realismo de combate | `5b28375` | Separación sólida sin peonzas, formación configurable |
| Fase 0–5 (finales) | `17d002f`…`17d002f` | Build unificado (B), rendimiento 9.2→60 FPS + profiler (A), unidades isométricas animadas (2), audio CC0 (3), VFX de combate (4), atmósfera (5) |
| Fase A | `67023e3` | Rendimiento de combate 9.2 → 60 FPS + profiler (`src/prof`) |
| Fase B | `2eb2186` | Build unificado a `rts_game.exe` en la raíz (`build.bat`) |
| Fase C | `7da77d7` | Mapa de campaña tipo Risk (continente con fronteras Voronoi, `src/mapart`) |
| Fase D | `7f4ac27` | Auto-resolve con predicción de victoria (estilo RISK) |
| Fase D+ | `4723442` | Bordes irregulares y colores de bando en el mapa de campaña |
| Fase J | `231212e` | Ejércitos de campo (badges, selección, formación/unión, IA móvil) + fix ranged hold |
| Fase I | `ab1ba8e` | Diplomacia: alianzas militares, pactos comerciales, trueque, modos de mapa |
| Fase K | `6afe190` | Campaña «Great Continent» (32 provincias), 4ª facción Sable Fleet, diplomacia de 4 filas, `SAVE_VERSION` 8 |
| **Fase E** | `6ab20c8` | **Base de red UDP** (ver §12.1) |
| **Fase F** | `f1ccb3e` | **Protocolo de sesión** NetMsg + handshake (ver §12.2) |
| **Fase G** | `d705913` | **Lobby MULTIPLAYER** (ver §12.3) |
| **Fase H** | `cafed73` | **Sincronización de campaña** host autoritativo (ver §12.4) |

### 12.1 Fase E — base de red UDP (ejecutada, `6ab20c8`)

- `src/net.h` / `src/net.cpp` (nuevos): sockets UDP **no bloqueantes** con
  Winsock2 (incluido el PRIMERO en `net.cpp`, antes de raylib/windows.h, para
  evitar el conflicto clásico de cabeceras; `net.h` no expone tipos Winsock,
  usa `NetSock = uintptr_t`).
- `netInit` / `netShutdown` (WSAStartup/WSACleanup idempotentes),
  `netOpen(bindPort,&outPort)` (bind a **127.0.0.1** — evita avisos de firewall;
  F/G/H ampliarán a `INADDR_ANY` cuando haya protocolo de juego),
  `netSendTo` / `netRecvFrom` (con `select()` y timeout).
- `runNetTest()`: self-test headless **10 rondas ping/pong** entre dos sockets
  en `127.0.0.1:7777` (servidor + cliente efímero); imprime `NETTEST PASS/FAIL`
  y sale con código 0/1.
- `main(int argc, char** argv)`: el flag **`-nettest`** ejecuta el self-test
  ANTES de `InitWindow` (sin ventana) y sale.
- `build.bat`: añadido **`-lws2_32`**.
- Verificado: `rts_game.exe -nettest` → PASS (10/10), exit 0. `build/test_net.ps1`.

### 12.2 Fase F — protocolo de sesión (handshake)

Sobre la base UDP de la Fase E, define el **mensaje de sesión** y el handshake
mínimo entre host y cliente:

- `NetMsg` (packed): `magic` ('MCNT'), `type` (JOIN/WELCOME/REJECT/PING/PONG/BYE),
  `version` (`NET_PROTO_VERSION=1`), `seq`, payload de 64 bytes.
- `netSendMsg` / `netRecvMsg` (valida magic+version; devuelve 1 OK, 0 timeout,
  -1 inválido).
- Handshake: cliente JOIN → host WELCOME (o REJECT si versión incompatible);
  keepalive PING/PONG con `seq` estricto; cierre BYE.
- `-nettest` ampliado: además del ping/pong crudo, **session test** loopback
  (JOIN→WELCOME, 3×PING/PONG, BYE) → `SESSION PASS/FAIL`; exit 0 solo si ambas
  fases pasan. `build/test_net.ps1` verifica ambas.

### 12.3 Fase G — lobby en el juego (ejecutada)

- Nuevo estado **`STATE_MULTIPLAYER`** + botón **MULTIPLAYER** en el menú
  (step*7; SETTINGS/EXIT desplazados a step*8/9 — sin tests afectados).
- Pantalla `updateDrawMultiplayer` (main.cpp): status box (frase de
  `NetSession::status`), contador de jugadores (vista host), botones
  HOST/STOP HOSTING, fila JOIN (campo de IP con input de teclado + botón),
  hint, BACK (abajo-derecha como Diplomacia). BACK y salida del juego
  cierran la sesión (`netSessionStop` en cleanup de main).
- API runtime en `net.h`/`net.cpp`: `NetSession` global (role, sock, connected,
  playerCount, status, peerIp, seq, reintentos), `netSessionHostStart`,
  `netSessionClientJoin`, `netSessionPoll` (drain no bloqueante + re-JOIN cada
  60 ticks hasta 5 intentos + keepalive PING cada 120 ticks + BYE al parar).
- `netRecvMsgFrom` (nueva): expone ip:port de origen; el host responde
  WELCOME/PONG a la dirección real del cliente (no a sí mismo).
- `-nettest` ampliado con **`runLobbyTest`**: (1) host arranca, JOIN crudo lo
  registra (playerCount≥2); (2) cliente sin host nunca conecta → LOBBY PASS.
- Tests: `build/test_net.ps1` (10 checks) y nuevo `build/test_g_lobby.ps1`
  (8 checks UI: menú→lobby, HOST status+players, STOP, JOIN status, BACK).
- Límites conocidos (documentados, sin impacto en el alcance de G): un solo
  `NetSession` global por proceso (dos instancias = dos procesos); sin UI de
  lista de jugadores más allá del contador; sin sync de juego (Fase H).

### 12.4 Fase H — sincronización de campaña (ejecutada)

- Modelo **host autoritativo co-op**: reino compartido (mismas provincias,
  recursos y ejércitos para todos). El host es la única fuente de verdad;
  los clientes envían comandos y aplican snapshots.
- **Snapshot**: binario `SAVE_VERSION=8` (mismo formato que `saveGame`),
  troceado en paquetes `NET_MSG_SNAPSHOT` (chunk 1024 B, tope 256 KiB).
  `serializeCampaignState`/`deserializeCampaignState` refactorizados en
  `save.cpp`; `saveGame`/`loadGame` son wrappers byte-idénticos.
- **Comandos** (`NET_MSG_CMD`, cliente → host): `END_TURN`, `MOVE_ARMY`
  (propias), `RECRUIT`, `DISBAND`, `SYNC_REQ`, **`ATTACK`** (H2). El host
  valida y aplica; si el estado cambió, retransmite snapshot a todos los
  clientes.
- **H2 — ataques co-op**: el cliente que desea atacar envía
  `NET_CMD_ATTACK` (a=provincia objetivo, b=índice de ejército). El host
  valida (adyacencia, no aliado/propio, ejército sin mover), selecciona el
  ejército, arma `g_preBattle` con la guarnición + ejércitos de campo
  enemigos y entra en `STATE_PRE_BATTLE`. **El host resuelve la batalla**
  (auto-resolve o batalla real); el resultado se retransmite vía snapshot.
  El cliente permanece en el mapa y aplica el resultado cuando llega.
- **Integración** (`main.cpp`): helpers `netIsClient`/`netIsHost`/
  `netMarkDirty`/`netCanEdit`/`netHostApplyCmd`/`netSyncPump` (1×/frame en
  el main loop). El cliente no muta recursos/localmente en acciones de
  edición; las envía como comandos.
- **Limitaciones documentadas**: defensas IA las resuelve el host; acciones
  no sincronizadas se sobreescriben con el siguiente snapshot; 1
  `NetSession`/proceso. **`missPong` implementado**: el cliente detecta el
  cierre del host sin `BYE` tras ~10 s sin PONG (600 ticks a ~60 fps) y
  pone `connected=false` con mensaje "Host lost". **`INADDR_ANY`
  implementado**: el socket del host se liga a todas las interfaces (no solo
  loopback), lo que permite juego en red real; el cliente puede unirse por
  IP LAN/WAN. **Checks victoria/derrota del cliente implementados**: al
  aplicar un snapshot, el cliente cuenta sus provincias y entra en
  `STATE_VICTORY` (≥80%) o `STATE_DEFEAT` (0), mismo umbral que el host.
  **Fiabilidad CMD implementada**: el host envía `NET_MSG_ACK` (eco del seq)
  al recibir un comando; el cliente guarda el comando pendiente y lo
  retransmite cada ~1 s (max 5 intentos) si no llega ACK; el host
  deduplica por seq (ip:port + lastSeq) para no aplicar reenvíos dos veces.
- **PvP — dos reinos separados (última limitación grande, RESUELTA)**:
  `SAVE_VERSION=9` añade `pvpMode`/`clientFaction`/`clientRes` al final de
  `CampaignState` (deserialize acepta v8 con defaults). El host es
  `FACTION_PLAYER`; al hacer JOIN en modo PvP asigna al cliente la primera
  facción libre no-PLAYER/NEUTRAL (`NetHostClient.faction`) y la comunica en
  el payload de WELCOME (`"v1 f<N>"`; el cliente la guarda en
  `NetSession.assignedFaction`). El host rellena `NetCmd.faction` con la
  facción del remitente al encolar comandos; `netHostApplyCmd` valida
  propiedad/costes contra esa facción (no contra `FACTION_PLAYER`
  hardcodeado) y usa `clientRes` para el reclutamiento del cliente. El
  cliente fija `g_localFaction` (= `clientFaction` en PvP, `PLAYER` en
  co-op/solo) cada frame; `selectArmy`/`defenseSoldiers`, el handler de
  clicks (mover/atacar), la barra de recursos, el ejército seleccionado y
  las pantallas de ciudad/reclutamiento/mercado/diplomacia usan
  `g_localFaction`/`clientRes`. `factionIsHuman` hace que la IA salte la
  facción humana del cliente (coalición, refuerzos, ejércitos de campo) y
  `processTurn` da income a `clientRes` para sus ciudades. Check de
  victoria/derrota local usa la facción propia. **Activación**: botón
  "PVP: TWO KINGDOMS" en el lobby multiplayer (alterna `pvpMode` antes de
  hacer HOST; desactivado por defecto, co-op sigue siendo el modo por
  defecto). **Limitación conocida**: la reserva (`g_campaign.reserve`) y la
  cola de reclutamiento se comparten (las tropas reclutadas por el cliente
  acaban en la reserva común); un fix futuro requeriría reservas por
  facción (SAVE_VERSION=10).
- `runSyncTest` vive en `src/sync_test.cpp` (incluye `campaign.h`/`raylib.h`,
  sin winsock; usa `std::this_thread::sleep_for`) para evitar el choque
  raylib.h vs windows.h. `-nettest` ampliado: `rc4=runSyncTest()`, exit 0
  solo si los 4 tests PASS. Verificado: SYNC PASS (blob 489 B, 1 trozo,
  deserialización y broadcast correctos).
- Tests: `build/test_h_sync.ps1` (11 checks, ALL PASS). Regresión completa
  verde: `test_net`, `test_g_lobby`, `test_h_sync`, `test_faseK/I/J/D`,
  `test_ranged`.

### 12.5 Infraestructura de tests (build/, ignorada en Git)

Tests PowerShell **ASCII puro** (bytes > 127 = error de sintaxis en PS 5.1),
coordenadas a pantalla (1600×900, uiScale 1.000), con helpers endurecidos tras
incidencias reales:

- `FocusWin`: `ShowWindow(9)` (SW_RESTORE) + `SetForegroundWindow` + fallback
  `keybd_event(ALT)` verificando `GetForegroundWindow`; llamada al inicio de
  `Click`/`GetBmp` y al arrancar. Motivo: el navegador del usuario robaba
  foco (clicks caían en otra app) y tras `Kill()` forzados la ventana quedaba
  **blanca** (superficie DWM colgada) aunque el proceso renderizaba.
- `settings.ini` debe mantener **uiScale 1.000**: los tests muestrean píxeles
  a escala 1.0 (1.212 rompía todos los umbrales).
- Suite actual (todas ALL PASS tras la Fase H): `test_net` (10, -nettest),
  `test_h_sync` (11, -nettest SYNC), `test_g_lobby` (8, lobby UI),
  `test_faseK` (12), `test_faseI` (25), `test_faseJ` (24), `test_faseD` (19),
  `test_ranged` (7), `test_vfx` (visual). `test_battle.ps1` es legacy (usa
  `rts_game_mod.exe` inexistente): fuera de la suite.
- La suite dura ~2,5 min; si se usa el equipo durante la ejecución pueden
  perderse checks sensibles al foco (interferencia, no regresión).
