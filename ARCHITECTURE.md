# ARCHITECTURE.md — Medieval Conquest (RTS)

Documento vivo. Describe la arquitectura **real** del código tras la
reestructuración modular (FASE 1) y las fases de contenido A–K. Se generó desde
`PLAN.md` §5 (estructura de módulos y dependencias) y §11.A (decisiones de
diseño), actualizado contra el código el 2026-10-13 (`SAVE_VERSION=12`).

- **Stack**: C++17, Raylib 5.5 (render/input/audio), Winsock2 (red UDP),
  toolchain MinGW-w64 (w64devkit) en Windows. Un solo ejecutable: `rts_game.exe`.
- **Layout**: todo en `src/` (`-Isrc`); los headers viven junto a sus `.cpp`.
  No hay directorio `include/`. Raylib no se vendoriza: llega del toolchain.

## 1. Módulos (`src/`)

21 módulos `.cpp` + 19 headers `.h` (`main.cpp` y `sync_test.cpp` no tienen
header propio). El build compila por glob (`src\*.cpp`), así que añadir un
módulo nuevo no requiere tocar `build.bat`.

| Módulo | Responsabilidad |
|---|---|
| `config.h` / `config.cpp` | Constantes, `GameState`, enums, `GameSettings`, `loadSettings`/`saveSettings` (`settings.ini`). `SAVE_VERSION=12`. |
| `util.h` / `util.cpp` | RNG (`std::mt19937 g_rng`, `frandMT`/`randIntMT`), matemática y vectores (`vdist`, `vnorm`, `v2*`, `lerpAngle`, `turnAngle`). |
| `ui.h` / `ui.cpp` | Primitivas de UI: `DrawTextUI`/`MeasureTextUI` (macros `DrawText`/`MeasureText`), `drawButton`, `drawSmBtn`, sliders, `drawWrappedText`. |
| `camera.h` / `camera.cpp` | Zoom, seguimiento y lerp de la cámara (incluye `initBattleCamera`). |
| `sprite.h` / `sprite.cpp` | Sprites de unidades: fallback procedural + hojas bakeadas (`rebuildTexture`). |
| `terrain.h` / `terrain.cpp` | Generación de obstáculos AABB por tipo de terreno (incluye ríos/lagos/bosques/ciudad). |
| `elements.h` / `elements.cpp` | Tipos de unidad (`UnitTypeDef`, `g_unitTypes`), entidades de batalla (`Soldier`, `BattleUnit`, `Projectile`, `DeadMarker`, `ControlGroup`) y formación de slots (`calcFormationSlots`, `formationDims`, `g_formPerRow`/`g_formSpacing`/`g_formRowSpacing`). |
| `combat.h` / `combat.cpp` | `BattleState`/`g_battle`, cálculo de daño, proyectiles, IA de unidad, auto-resolve (FASE D) y **formaciones con multiplicadores** (`FormationTactic`, `applyBattleFormation`, `flankDmgMult`). |
| `campaign.h` / `campaign.cpp` | `Province`, `CampaignState`, turnos, generación de mapas, diplomacia/comercio, desembarque y el self-test headless `-battletest` (`runBattleResultTest`). |
| `city.h` / `city.cpp` | Edificios (tablas `bld*`), construcción, producción y reclutamiento; `defineBuiltinBuildings` (golden) y prerrequisitos (`bldHasPrereq`, `bldReqName`). |
| `save.h` / `save.cpp` | `saveGame`/`loadGame` binarios (`SAVE_VERSION=12`, compatible v8→v12) en `campaign_N.dat` + `campaign_last.dat`. |
| `net.h` / `net.cpp` | UDP no bloqueante (Winsock2), mensajes, sesión (handshake), lobby y snapshot de campaña; self-tests `-nettest` (`runNetTest`, `runSessionTest`, `runLobbyTest`, `runSyncTest`). |
| `gamedata.h` / `gamedata.cpp` | Loader *data-driven* con parser INI propio y fallback: unidades (`loadUnitData*`), edificios (`loadBuildingData*`) y formaciones (`loadFormationData*`). Self-test `-datatest`. |
| `ui_multiplayer.h` / `ui_multiplayer.cpp` | Pantalla MULTIPLAYER (lobby host/join, Fase G), extraída de `main.cpp` en el refactor R1. |
| `mapart.h` / `mapart.cpp` | Siluetas de continente (polígonos) para el mapa de campaña. |
| `atmos.h` / `atmos.cpp` | Atmósfera ambiental de la pantalla. |
| `audio.h` / `audio.cpp` | Carga y reproducción de música/SFX. |
| `fx.h` / `fx.cpp` | Efectos visuales de combate (chispas, sangre, polvo). |
| `prof.h` / `prof.cpp` | Profiling de frame por secciones (`profAdd`). |
| `main.cpp` | Punto de entrada, bucle principal, máquina de estados (`GameState`) y todas las pantallas; despacha los self-tests de argv (`-nettest`, `-battletest`, `-datatest`, `-disembarktest`). |
| `sync_test.cpp` | Serialización de snapshot para el test de sincronización (Fase H). |

## 2. Dependencias

Notación: `X ◄── Y` = "el módulo Y incluye a X". La relación es **acíclica**,
de arriba abajo: utilidades → tipos → lógica → pantallas → `main`.

```
config.h     ◄──  todos los headers (y, por tanto, todos los módulos)
util.h       ◄──  ui, sprite, elements, combat, campaign, city, save,
                  mapart, ui_multiplayer, main
elements.h   ◄──  combat, campaign, save, sprite, fx, main
camera.h     ◄──  combat, main
terrain.h    ◄──  combat, main
sprite.h     ◄──  combat, elements, main
ui.h         ◄──  campaign, combat, city, elements, save, ui_multiplayer, main
combat.h     ◄──  campaign, fx, gamedata, camera, main
campaign.h   ◄──  combat, city, save, mapart, ui_multiplayer, sync_test, main
city.h       ◄──  campaign, gamedata, main
save.h       ◄──  campaign, sync_test, main
net.h        ◄──  ui_multiplayer, sync_test, main
gamedata.h   ◄──  elements, main
```

`config.h` es el único header "central": lo incluyen todos los demás headers y
a su vez incluye `raylib.h`. `util.h` aporta RNG y matemática sin
dependencias de estado del juego. La notación es sobre el grafo de headers;
algunos `.cpp` incluyen además headers extra (p. ej. `elements.cpp` incluye
`ui.h` y `gamedata.h`), que no crean ciclos porque ningún header incluye a
`elements.h` desde el lado de UI/datos.

## 3. Datos *data-driven* (capa de mods)

Regla común a **unidades** (`data/base/units/*.unit`), **edificios**
(`data/base/buildings/*.bld`) y **formaciones** (`data/battle/formations.txt`),
implementada en `gamedata.cpp`:

- Parser INI propio (`clave = valor`, comentarios `;`/`#`), sin dependencias.
- Cada unidad/edificio/formación se sustituye por su archivo **solo si el
  archivo existe y todas sus claves obligatorias son válidas**. Si falta el
  archivo o una clave, se conserva el valor *built-in* (golden) y se emite un
  aviso con formato `archivo:linea: motivo` (consola + `battleLog`).
- Los *builtins* son la única fuente de verdad de los valores por defecto:
  `defineBuiltinTypes` (`elements.cpp`), `defineBuiltinBuildings` (`city.cpp`)
  y `defineBuiltinFormations` (`combat.cpp`).
- El orden de los slots de edificio (enum `BuildingType`) y de los tipos de
  unidad está fijado por los *builtins*; el loader sustituye por nombre, nunca
  reordena (los guardados persisten índices).

## 4. Flujo de build y tests

### Build

```bat
build.bat
```

- Compila `src\*.cpp` con `g++ -std=c++17 -O2 -Wall -Wextra -Isrc` y enlaza
  contra Raylib estático (`-lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32`).
- Salida: `rts_game.exe` en la raíz. Log completo en `build\build.log`.
- El script cuenta errores/warnings con `find` y termina con
  `BUILD OK [errors=N warnings=N]`. Objetivo de calidad: **0 errores / 0 warnings**.

### Tests

Dos familias:

1. **Self-tests headless** (sin ventana, despachados desde `main()` por argv,
   salen con código 0=PASS / 1=FAIL):
   - `-nettest` — red (`runNetTest` + `runSessionTest` + `runLobbyTest` + `runSyncTest`).
   - `-battletest` — rutas de fin de batalla (`runBattleResultTest`, secciones A–J) y
     formaciones con multiplicadores (sección K, B1).
   - `-datatest` — unidades y edificios *data-driven* (`runUnitDataTest` + `runBuildingDataTest`,
     golden + fallback).
   - `-disembarktest` — desembarque selectivo de tipos y cantidades (`runDisembarkTest`).
2. **Suites UI** en `build/*.ps1` (ignoradas en Git; arrancan el juego y
   verifican por píxeles; requieren `uiScale 1.000` y 1600×900). Scripts:
   `test_net`, `test_h_sync`, `test_g_lobby`, `test_faseK`, `test_faseI`,
   `test_faseJ`, `test_faseD`, `test_ranged`, `test_city`, `test_battleres`,
   `test_disembark`, `test_battle`, `test_map`, `test_vfx`.

`test_faseJ` tiene un flake conocido (~4 %) por la IA agresiva en el turno 1:
reintentar una vez antes de darlo por regresión.

## 5. Decisiones de diseño (resumen de `PLAN.md` §11.A)

- **Single-file → modular** por mantenibilidad y tiempo de compilación, no por
  obligación.
- **Layout único `src/` + `-Isrc`**: todos los `.h` junto a sus `.cpp`; no se
  usa `include/`.
- **Macros de texto conservadas** (`#define DrawText DrawTextUI`,
  `#define MeasureText MeasureTextUI`) para no renombrar ~1.000 call sites.
- **Nombres originales conservados** (`HUD_H`, `C_ENEMY_COL`,
  `BattleState::projs`…); renombrar es mejora posterior.
- **RNG conservado**: `std::mt19937 g_rng` + `frandMT`/`randIntMT`. Se descarta
  `frandU()` (semilla por `GetTime`), que cambiaría el muestreo sin beneficio.
- **Ningún binario en Git** (DLLs, `.exe` fuera del índice).
- **Formación por batalla, no por settings (D8)**: el selector pre-batalla muta
  solo `g_formPerRow`/`g_formSpacing`/`g_formRowSpacing` y los multiplicadores
  de `BattleState`; `g_settings` no se toca, así que no se persisten en
  `settings.ini` ni contaminan la siguiente batalla.
- **Contenido balanceable en datos** (unidades, edificios, formaciones) con
  *builtins* como fallback, para habilitar mods sin reescribir el motor.

## 6. Save / compatibilidad

- Formato **binario** con `SAVE_VERSION=12` (`config.h`), carga compatible con
  v8→v12. El mismo blob binario se reutiliza para el snapshot de red (Fase H).
