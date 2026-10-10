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

## Estructura

- `src/` — 18 módulos `.cpp` + 17 headers `.h` (reestructuración FASE 1 + fases
  de contenido A–K; red desde la Fase E: `net.h`/`net.cpp`).
- `assets/sprites/` — hojas de unidades Kenney (CC0) con fallback procedural.
- `tools/preview_sprites.cpp` — genera `build/sprite_preview.png` para verificar
  la integración de sprites sin arrancar el juego.
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

Los tests viven en `build/` (ignorado en Git; son utilidades locales, ASCII
puro para PowerShell 5.1). Requieren `uiScale 1.000` en `settings.ini` y
1600×900. Ejecutar desde la raíz, un test por invocación:

```powershell
powershell -ExecutionPolicy Bypass -File build\test_net.ps1     # red (-nettest)
powershell -ExecutionPolicy Bypass -File build\test_h_sync.ps1  # sync campana (-nettest)
powershell -ExecutionPolicy Bypass -File build\test_g_lobby.ps1 # lobby multiplayer (UI)
powershell -ExecutionPolicy Bypass -File build\test_faseK.ps1   # campaña Great Continent
powershell -ExecutionPolicy Bypass -File build\test_faseI.ps1   # diplomacia
powershell -ExecutionPolicy Bypass -File build\test_faseJ.ps1   # ejércitos de campo
powershell -ExecutionPolicy Bypass -File build\test_faseD.ps1   # auto-resolve
powershell -ExecutionPolicy Bypass -File build\test_ranged.ps1  # unidades con alcance
```

## Créditos de arte

- Unidades: **"Medieval RTS" por Kenney (kenney.nl)** — licencia
  [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) (dominio público;
  la atribución es opcional). Archivo de licencia incluido en
  `assets/sprites/LICENSE-Kenney.txt`. Fuente:
  <https://opengameart.org/content/medieval-rts-120>
