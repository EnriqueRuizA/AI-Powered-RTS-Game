# Medieval Conquest — RTS

Un juego de estrategia en tiempo real en C++17 usando Raylib.

## Requisitos

- Windows
- Compilador C++17: **w64devkit (MinGW-w64, GCC 14.2)** en `C:\raylib\w64devkit` *(verificado)*
- **Raylib 5.5** (headers y `libraylib.a` vienen con el toolchain)

## Compilación (modular — `src/`)

```bat
rem Un solo comando desde la raíz del repositorio:
build.bat

rem Compila src\*.cpp y genera rts_game.exe en la raíz.
rem Log completo (errores/warnings): build\build.log
```

La línea manual equivalente (toolchain w64devkit en PATH):

```bat
g++ -std=c++17 -O2 -Wall -Wextra -Isrc src\*.cpp -o rts_game.exe -L"C:\raylib\raylib\src" -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32 2> build\build.log
```

## Ejecución

```bat
rem Desde la raíz del repositorio (assets/ y settings.ini se buscan en el CWD):
rts_game.exe

rem Self-test de red (headless, sin ventana; sale con código 0=PASS / 1=FAIL):
rts_game.exe -nettest
```

Si `assets/sprites/` no existe, el juego arranca igualmente con los sprites
procedurales originales (fallback automático).

## Configuración

- Copiar `settings.example.ini` a `settings.ini` (local, ignorado por Git).
- Resolución, volumen, dificultad e idioma se ajustan desde el menú o el `.ini`.
- Default único de resolución: **1280×768**; sin `.ini` el juego arranca con los defaults.

## Guardado de partidas

- Botón **SAVE** en la barra inferior del mapa de campaña, o tecla **F5**.
- **MENU** (volver al menú principal) guarda automáticamente.
- Auto-guardado al cerrar el juego si hay campaña activa (cualquier turno).
- Archivos: `campaign_0.dat` … `campaign_4.dat` + `campaign_last.dat`
  (campaña recordada para **CONTINUE**). Formato binario `SAVE_VERSION=12`
  (compatible hacia atrás: carga v8–v12).

## Estructura

- `src/` — 21 módulos `.cpp` + 19 headers `.h` (reestructuración FASE 1 + fases
  de contenido A–K; red desde la Fase E: `net.h`/`net.cpp`).
- `assets/sprites/` — hojas de unidades Kenney (CC0) con fallback procedural.
- `data/base/` — **unidades** (`units/*.unit`) y **edificios** (`buildings/*.bld`)
  *data-driven*: el juego los carga al arrancar y, si falta un archivo o una
  clave, conserva los valores *built-in* y avisa por consola/battleLog
  (parser propio `gamedata.cpp`, sin dependencias externas).
- `data/battle/formations.txt` — multiplicadores de formación de batalla (B1).
- `tools/preview_sprites.cpp` — genera `build/sprite_preview.png` para verificar
  la integración de sprites sin arrancar el juego.
- `ARCHITECTURE.md` — módulos, dependencias y decisiones de diseño.
- `PLAN.md` — plan de reestructuración, histórico de fases (§12) y estado.
- `settings.example.ini` — plantilla versionada de la configuración.

## Red (Fases E–H)

- `src/net.h` / `src/net.cpp` — base UDP no bloqueante (Winsock2), loopback
  `127.0.0.1:7777` (evita avisos de firewall; `INADDR_ANY` llegará con el
  protocolo de juego).
- `rts_game.exe -nettest` — self-test headless: ping/pong crudo (10 rondas),
  sesión loopback (JOIN→WELCOME, PING/PONG, BYE), runtime de lobby y
  **sincronización de campaña** (snapshot serializado + comandos + broadcast).
  Sale con 0=PASS / 1=FAIL (los 4 tests deben pasar).
- Botón **MULTIPLAYER** en el menú → lobby: HOST (escucha UDP 7777), JOIN por
  IP, estado de conexión.
- **Fase H — sincronización de campaña (host autoritativo co-op)**:
  - Host mantiene la única copia de `CampaignState`; los clientes envían
    comandos (`END_TURN`, `MOVE_ARMY`, `RECRUIT`, `DISBAND`, `ATTACK`) y
    aplican snapshots troceados (`NET_MSG_SNAPSHOT`, chunk 1024 B, tope
    256 KiB).
  - El snapshot usa el mismo formato binario `SAVE_VERSION=8` que `saveGame`.
  - `netSyncPump()` en el main loop drena comandos y retransmite si el
    estado cambió.
  - **H2 — ataques co-op**: el cliente envía `NET_CMD_ATTACK`; el host
    valida (adyacencia, no aliado, ejército sin mover), entra en
    PRE_BATTLE y resuelve la batalla; el resultado se retransmite vía
    snapshot.
  - **Limitaciones**: defensas IA las resuelve el host; acciones no
    sincronizadas se sobreescriben con el siguiente snapshot; 1
    `NetSession`/proceso. **`missPong`**: el cliente detecta el cierre del
    host sin `BYE` tras ~10 s sin PONG y muestra "Host lost". **`INADDR_ANY`**:
    el host se liga a todas las interfaces (juego en red real LAN/WAN, no
    solo loopback). **Victoria/derrota del cliente**: al aplicar un snapshot,
    el cliente entra en `STATE_VICTORY` (≥80%) o `STATE_DEFEAT` (0), mismo
    umbral que el host. **Fiabilidad CMD**: ACK del host + retransmisión del
    cliente (max 5 intentos, ~1 s de espera) + deduplicación por seq en el
    host.
  - **PvP — dos reinos separados** (`SAVE_VERSION=9`): el host controla
    `FACTION_PLAYER`; al hacer JOIN en modo PvP asigna al cliente la primera
    facción libre no-PLAYER/NEUTRAL y se la comunica en el WELCOME
    (`"v1 f<N>"`). El host valida cada comando contra la facción del
    remitente (`NetCmd.faction`) y usa `clientRes` para el reclutamiento del
    cliente. El cliente usa `g_localFaction`/`clientRes` para selección,
    recursos y UI; la IA salta la facción humana del cliente
    (`factionIsHuman`). **Activación**: botón "PVP: TWO KINGDOMS" en el
    lobby multiplayer (alterna `pvpMode` antes de HOST; desactivado por
    defecto, co-op es el modo por defecto). **Reserva por facción**
    (SAVE_VERSION=10): `clientReserve` separada; cada reclutamiento se
    enruta a la reserva de su facción (`RecruitEntry.faction`); formar/
    unir/disolver ejércitos y la UI usan la reserva de la facción local.
- Roadmap: F protocolo de sesión ✅ → G lobby ✅ → H sincronización ✅
  (incluye H2 ataques y PvP dos reinos). Detalle en `PLAN.md` §12.

## Tests

Hay dos familias: **self-tests headless** (sin ventana, se invocan directamente
sobre `rts_game.exe` y salen con código 0=PASS / 1=FAIL) y **suites UI** en
`build/` (ignoradas en Git; utilidades locales en ASCII puro para PowerShell
5.1, que arrancan el juego y verifican por píxeles). Las suites UI requieren
`uiScale 1.000` en `settings.ini` y 1600×900. Ejecutar desde la raíz, un test
por invocación:

```bat
rem --- Self-tests headless ---
rts_game.exe -nettest         # red: capa cruda + sesion + lobby + sync campana
rts_game.exe -battletest      # rutas de fin de batalla + formaciones (B1)
rts_game.exe -datatest        # unidades y edificios data-driven (golden/fallback)
rts_game.exe -disembarktest   # desembarque selectivo de tipos y cantidades
```

```powershell
powershell -ExecutionPolicy Bypass -File build\test_net.ps1         # red (-nettest)
powershell -ExecutionPolicy Bypass -File build\test_h_sync.ps1      # sync campana (-nettest)
powershell -ExecutionPolicy Bypass -File build\test_g_lobby.ps1     # lobby multiplayer (UI)
powershell -ExecutionPolicy Bypass -File build\test_faseK.ps1       # campana Great Continent
powershell -ExecutionPolicy Bypass -File build\test_faseI.ps1       # diplomacia
powershell -ExecutionPolicy Bypass -File build\test_faseJ.ps1       # ejercitos de campo
powershell -ExecutionPolicy Bypass -File build\test_faseD.ps1       # auto-resolve
powershell -ExecutionPolicy Bypass -File build\test_ranged.ps1      # unidades con alcance
powershell -ExecutionPolicy Bypass -File build\test_city.ps1        # matriz de construccion 4x4
powershell -ExecutionPolicy Bypass -File build\test_battleres.ps1   # fin de batalla (-battletest)
powershell -ExecutionPolicy Bypass -File build\test_disembark.ps1   # desembarque (-disembarktest)
powershell -ExecutionPolicy Bypass -File build\test_battle.ps1      # smoke de batalla por UI
powershell -ExecutionPolicy Bypass -File build\test_map.ps1         # mapa de campana
powershell -ExecutionPolicy Bypass -File build\test_vfx.ps1         # efectos visuales
```

> **Nota**: tras añadir la 5ª campaña (Europa), el botón QUICK BATTLE del
> menú bajó un paso: los tests de batalla usan las coordenadas actualizadas
> (`Click 800 572`) y pulsan **BEGIN BATTLE** (`Click 800 568`) para saltar
> la fase de preparación. `test_faseJ` tiene un flake conocido (~4 %) por la
> IA agresiva en el turno 1: si falla, reintentar una vez.

## Novedades v11/v12 (campaña y batalla)

- **5ª campaña: Europa** (36 provincias) en el menú principal.
- **Matriz de construcción 4×4** por ciudad + edificios repetibles con
  bonus acumulativos; murallas en el perímetro.
- **Atalaya**: la ciudad que la tiene revela a 2 regiones de distancia.
- **Barcos**: EMBARK (1 oro/unidad, solo en costa), DISEMBARK total o
  parcial (LAND HALF), rutas marítimas de 2 saltos entre costas.
- **MERGE**: ventana modal para combinar ejércitos de la provincia.
- **Composición del ejército** visible al seleccionar; **Escape/clic vacío**
  deselecciona (los clics en barras no deseleccionan).
- **Terrenos de batalla variados**: ríos con puente, lagos, bosques,
  montañas y ciudades con muralla según la provincia.
- **Fase de preparación pre-batalla** (defecto 15 s, configurable en
  Settings: `usePrepPhase`/`prepTimeSec`): elegir formación
  (LINE/COLUMN/SQUARE/LOOSE) con movimiento bloqueado.
- **Veteranía real (D2)**: Barracks acelera reclutamiento; Range/Stable dan
  veteranía inicial (guardada en `SAVE_VERSION=12`).

## Créditos de arte

- Unidades: **"Medieval RTS" por Kenney (kenney.nl)** — licencia
  [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) (dominio público;
  la atribución es opcional). Archivo de licencia incluido en
  `assets/sprites/LICENSE-Kenney.txt`. Fuente:
  <https://opengameart.org/content/medieval-rts-120>
