# HOJA DE RUTA A TRIPLE A — Medieval Conquest (RTS)

> **Objetivo declarado**: convertir el juego en un título con estándares de producción
> AAA, con una **campaña tipo *Shogun Total War* "descafeinada"** como núcleo de
> jugabilidad y una **arquitectura altamente moddable** que permita, sin reescribir
> el motor, reconversión temática total (caso de uso previsto: **guerra fría**).
>
> Documento vivo. Verificado contra el código el 2026-10-11 (`SAVE_VERSION=12`,
> build OK 0 errores / 10 warnings, tests `test_faseJ`, `test_ranged`,
> `test_h_sync`, `-nettest` en verde; fase v11/v12 commiteada).

---

## Índice

1. [Punto de partida verificado](#1-punto-de-partida-verificado)
2. [Qué es la campaña de Shogun Total War y qué nos quedamos (versión descafeinada)](#2-shogun-total-war-descafeinado)
3. [JUGABILIDAD — núcleo de la campaña (énfasis)](#3-jugabilidad--núcleo-de-la-campaña)
4. [Madurez AAA por áreas](#4-madurez-aaa-por-áreas)
5. [MODDABILITY — arquitectura de datos (énfasis)](#5-moddability--arquitectura-de-datos)
6. [Reconversión a guerra fría como prueba de fuego del sistema de mods](#6-reconversión-a-guerra-fría)
7. [Roadmap por fases](#7-roadmap-por-fases)
8. [Deuda técnica inmediata (bloqueantes)](#8-deuda-técnica-inmediata)
9. [Riesgos](#9-riesgos)
10. [Anexo A — Inventario de contenido hardcodeado](#10-anexo-a--inventario-de-contenido-hardcodeado)
11. [Anexo B — Requisitos de aceptación por fase](#11-anexo-b--requisitos-de-aceptación)

---

## 1. Punto de partida verificado

### 1.1 Qué hay hoy

| Área | Estado | Evidencia |
|---|---|---|
| Motor | Raylib 5.5, C++17, 2D con sprites + fallback procedural | `build.bat:16`, `sprite.cpp:255-311` |
| Módulos | 19 `.cpp` / 17 `.h`, compilación por glob | `src/`, `build.bat` |
| Campaña | 5 mapas (14/10/12/32/36 provincias), turno a turno, niebla, diplomacia, comercio | `campaign.cpp:369-704`, Fase I |
| Ciudades | Matriz 4×4 de construcción, edificios repetibles con `count[]`, 15 edificios | `main.cpp:1720-1915`, `city.cpp:19-37` |
| Ejércitos | Múltiples stacks de campo, héroes (General/Rey), veteranía, barcos (embarque 1 oro/unidad) | `campaign.h:52-63`, v11 |
| Batalla | Tiempo real, formaciones, moral/ruta, flanqueo, auto-resolve, fase de preparación configurable (15 s) | `combat.cpp`, `terrain.cpp` |
| Red | UDP propia, host autoritativo co-op + PvP parcial de facciones, lobby, snapshots | `net.cpp/h`, Fase H |
| Guardado | Binario v8→v12, 4 slots + CONTINUE | `save.cpp` |
| Tests | `nettest` headless + 10 suites UI por screenshot | `build/test_*.ps1` |

### 1.2 Qué NO hay (brecha real)

- **Todo el contenido está hardcodeado en C++**: unidades (`elements.cpp:25-71`),
  edificios (`city.cpp:19-37`), mapas (`campaign.cpp:369-704`), facciones
  (`config.h:90`), balance (`combat.cpp` — constantes mágicas).
- **No hay capa de modding**: ni loader de datos, ni IDs estables, ni i18n
  (la clave `language` se guarda pero no se usa: `config.cpp:36,56`).
- **No existen** eventos, misiones, estaciones/temporada, asedios por turno,
  árbol tecnológico, agentes, sucesión dinástica, batallas navales.
- **`main.cpp` monolítico**: 182 KB / 3522 líneas con la UI de 6+ pantallas.
- IA de campaña: una única fórmula de agresión (`campaign.cpp:895-898`) con
  personalidad de facción **solo composicional** (unidades iniciales distintas).

### 1.3 Deuda de la fase v11 (bloqueante antes de escalar)

| # | Bug / gap | Referencia | Estado 2026-10-11 |
|---|---|---|---|
| D1 | `bool explored[32]` → en Europa (36 prov.) las provincias **32-35 nunca se exploran** | `campaign.h:91`, `campaign.cpp:92,99,113` | ✅ CERRADO: `explored` es `vector<bool>` dinámico |
| D2 | Bonus de **Barracks** (`-1t recruit`) y **Range/Stable** (`+veterancy`) solo pintados en UI | `main.cpp:1885-1886` | ✅ CERRADO: `recruitTurnsFor`/`cityVeterancy` + `SAVE_VERSION=12` |
| D3 | Desembarque no admite **combinación arbitraria** de unidades | `main.cpp:1507-1545` | ✅ CERRADO: modal con selector [-] n [+] por tipo + LAND SELECTED (`disembarkTake`, commit `283e23d`) |
| D4 | `build/test_battle.ps1` roto (apunta a `build\rts_game_mod.exe` inexistente) | `build/test_battle.ps1:20` | ✅ CERRADO: apunta a `rts_game.exe` |
| D5 | Trabajo v11 **sin commitear** y `PLAN.md` sin documentar la fase | `git status` | ✅ CERRADO: commiteado + `PLAN.md` §12.6 + README |
| D6 | `bldPrereq` duplicado literal en la UI | `city.cpp:30` vs `main.cpp:1828` | ✅ CERRADO: `bldHasPrereq`/`bldReqName` en `city.h/cpp` |
| D7 | `PreBattleState::formationChoice` sin usar (campo muerto) | `campaign.h:129` | ✅ CERRADO: campo eliminado |
| D8 | La formación pre-batalla muta `g_settings.formationPerRow/Spacing` **globales** | `combat.cpp:1383-1388` | ✅ CERRADO: `g_formPerRow/g_formSpacing` por batalla |

---

## 2. Shogun Total War descafeinado

### 2.1 Qué hace la campaña de STW (referencia)

Campaña por turnos sobre mapa de provincias: economía por provincia, cadena de
edificios, reclutamiento condicionado, ejércitos con daimyo/estandarte, agentes
(ninja, monje, geisha, diplomático), diplomacia con honor, estaciones/invierno,
eventos aleatorios, batalla en tiempo real con formaciones y moral, auto-resolve,
condiciones de victoria por conquista/domination, árbol familiar y sucesión.

### 2.2 Qué nos quedamos (core), qué simplificamos, qué descartamos

| Sistema STW | Decisión | Versión descafeinada |
|---|---|---|
| Mapa de provincias con adyacencia y niebla | **CORE** | Ya existe. Ampliar a >32 prov. (D1) y FOW por facción |
| Economía por provincia + cadena de edificios | **CORE** | Ya existe. Multiplicadores por copia (D2) y prereqs data-driven |
| Reclutamiento condicionado + cola | **CORE** | Ya existe. Aplicar bonus de barracas de verdad |
| Ejércitos de campo con general + estandarte | **CORE** | Ya existe (héroes, veteranía). Falta "aura" del general y capacidad de mando (stack limit) |
| Movimiento por nodos + transporte marítimo | **CORE** | Ya existe (v11). Completar desembarque arbitrario (D3) |
| Diplomacia (alianzas, comercio, guerra) | **CORE** | Parcial. Añadir: declarar guerra, vasallaje simple, casus belli minimal |
| Batalla RT con formaciones, moral, flanqueo | **CORE** | Ya existe. Falta aplicar formaciones reales (COLUMN/SQUARE/LOOSE hoy son etiquetas) |
| Auto-resolve | **CORE** | Ya existe |
| Asedios | **SIMPLIFICAR** | Guarnición + asalto por turnos (2 turnos de sitio → bonus defensivo creciente). Sin mapa de asedio separado |
| Estaciones / año | **SIMPLIFICAR** | 4 turnos = 1 año; invierno = −1 movimiento y −10% ingreso en provincias de montaña. Data-driven |
| Agentes (ninja, monje, geisha, diplomático) | **SIMPLIFICAR** | **Un solo agente**: "emisario" (revelar niebla + mejorar relaciones). Ampliable por mod |
| Honor / reputación | **SIMPLIFICAR** | Un entero `reputación` 0-100: romper pactos = −, ayudar aliados = +; modifica precios de diplomacia |
| Eventos aleatorios | **CORE** | Sistema de eventos data-driven (ver §3.9) — gran gancho, bajo coste |
| Condiciones de victoria | **SIMPLIFICAR** | Tabla data-driven: % provincias, control de capitales, puntos, turnos máximos |
| Árbol familiar / sucesión del daimyo | **DESCARTAR** | Sustituir por "consejo de guerra" (3 consejeros con pasivo) si se quiere sabor |
| Batallas navales | **DESCARTAR** | Solo transporte (ya implementado) y bloqueo de ruta comercial |
| Tecnología / renovación de unidades | **DESCARTAR** (fase tardía) | Opcional: "doctrinas" (3 ramas × 4 nodos) cuando la capa de datos esté lista |
| Clanes / facciones | **CORE** | 6→N facciones data-driven, con tabla de personalidad de IA |

**Principio**: *nada nuevo se implementa como código de caso especial; todo sistema
nuevo nace como tabla de datos + motor genérico*. Es lo que separa "juego con
mods" de "juego moddeable".

---

## 3. JUGABILIDAD — núcleo de la campaña

Énfasis del informe. Cada sistema indica: **hoy → objetivo → requisitos de datos → prioridad**.

### 3.1 Bucle de juego (turn loop)

```
[Turno] → Producción económica → Colas (construcción/reclutamiento) →
IA de facciones → Diplomacia/eventos → Movimiento de ejércitos →
Batallas (RT o auto-resolve) → Victoria/derrota
```

**Hoy**: `processTurn()` (`campaign.cpp:791-1039`) hace todo en un bloque.
**Objetivo**: separar en **sistemas suscritos a un `TurnPipeline`** (orden fijo,
declarado en datos) para que un mod pueda inyectar pasos (p. ej. "cálculo de
suministro" o "contaminación nuclear") sin tocar el motor.

- F1: `TurnPipeline` con etapas nombradas: `economy, queues, ai, diplomacy, events, victory`.
- F2: fase de **orden de ejecución configurable** (`data/campaign/turn_pipeline.txt`).

### 3.2 Mapa y geografía

**Hoy**: 5 mapas embebidos como `ProvinceTemplate[]` + `adjData[][2]` manual
(`campaign.cpp:369-704`); siluetas dibujadas por código (`mapart.cpp:104-154`).

**Objetivo**:
1. **Editor de mapa externo** (aun sea un CSV/JSON + un exportador) que genere
   `data/maps/<id>/provinces.txt`, `adjacency.txt`, `outline.txt`, `factions.txt`.
2. FOW dinámico (`std::vector<bool>` con tamaño = nº de provincias) — elimina D1.
3. Terreno por provincia con **recursos geográficos** (trigo, hierro, madera, sal,
   caballos) que alimenten la economía y el comercio (ya existe `TRADE_RATES`).
4. Capas de visualización: política, recursos, niebla, relieve, rutas comerciales
   (hoy solo 2 modos: normal/alianzas, `main.cpp:1345`).

**Datos**: `provinces.txt` (id, nombre, nx, ny, terreno, recurso, capital, ciudad),
`adjacency.txt` (pares por id), `outline.txt` (polígonos de silueta).

### 3.3 Economía y ciudad

**Hoy**: matriz 4×4 + `count[]` por edificio, 1 producción por turno con
multiplicador de copias (`campaign.cpp` yield×n), murallas en perímetro.

**Objetivo**:
1. **Cerrar D2**: bonus acumulativos reales (reclutamiento −1t/copia con tope,
   veteranía inicial +1 cada 2 copias de Range/Stable, descuento de mantenimiento).
2. **Costes de mantenimiento por edificio** (hoy solo unidades) → tensión económica.
3. **Rutas de edificios data-driven**: prereqs por id, no por bitmask de 5 bits
   (hoy `buildingReqs` limita a 5 edificios, `elements.h:44`).
4. **Edificios con niveles** en lugar de duplicados where aplique (murallas ya lo
   son): `level 1..3` con curva de coste/beneficio.
5. **Obras/edificios únicos de provincia** (maravillas) → 1 por provincia, bonus global.

**Datos**: `data/buildings/<id>.txt` (nombre i18n, coste, turnos, prereqs[], yields[],
efectos[], límite de copias, nivel máximo).

### 3.4 Reclutamiento, unidades y veteranía

**Hoy**: 10 tipos hardcodeados (`elements.cpp:25-71`), editor de unidades **solo en
RAM** (`main.cpp:2527`, "changes persist until restart"), cola `recruitQueue`.

**Objetivo**:
1. **Persistir el editor**: botón "Export" → `data/units/*.txt`; "Import" al arrancar.
   Convierte el editor existente en el *toolchain* del modder.
2. **IDs estables de unidad** (string) en el save, no índices. Hoy `typeIdx` en el
   binario + `UNIT_GENERAL=8/UNIT KING=9` fijos → cualquier mod que reordene rompe saves.
3. **Categorías de unidad** (infantería/caballería/arsed/arquero/artillería) →
   habilita formaciones, contadores de composición estilo NATO y contadores de AI.
4. **Veteranía con fuente de edificio** (D2) + **historial por unidad de campo**
   (bajas causadas, batallas ganadas) persistido en el save.
5. **Mantenimiento y capas de suministro**: cada stack tiene `capacidad de mando`
   (general = +N); exceder penaliza moral. Limita el " doom stack ".

**Datos**: `data/units/<id>.txt`, `data/unit_categories.txt`.

### 3.5 Ejércitos, movimiento y barcos

**Hoy**: stacks de campo, `moved` por turno, `shipCanReach` (ruta marítima de 2
saltos), embarque 1 oro/unidad.

**Objetivo**:
1. ~~Cerrar D3 (desembarque por tipo con cantidades seleccionables).~~ ✅
   Hecho: selector [-] n [+] por tipo + LAND SELECTED (`283e23d`).
2. **Puntos de movimiento (MP)** por ejército en vez de binario `moved`:
   terreno + general + estación consumen MP. Data-driven por terreno.
3. **Ejércitos combinables en campo** (ya existe el modal de merge) → añadir
   "formar aquí" desde reserva sin abrir la ciudad.
4. **Interdicción naval**: barco en provincia costera bloquea el +X% de ingreso
   de rutas comerciales enemigas (usa el sistema de barcos ya creado).
5. **Retirada con coste** y **perseguir** (reglas claras de retirada: `safeRetreatProvince`
   ya existe).

**Datos**: `data/terrain/movement.txt` (MP por terreno, coste de embarque),
`data/naval/blockade.txt`.

### 3.6 Batalla en tiempo real

**Hoy**: 2560×1536, despliegue 20/80%, moral/ruta, flanqueo de caballería,
auto-resolve con `armyPower`, overlay de preparación con 4 formaciones.

**Problemas de diseño detectados**:
- Las 4 formaciones **solo cambian `formationPerRow`/`spacing`**; no hay diferencia
  real entre LINE/COLUMN/SQUARE/LOOSE (`combat.cpp:1383-1388`, D8).
- El auto-resolve usa otra fórmula distinta a la simulación real → **sesgo**
  (el jugador percibe injusticia al auto-resolver).
- Sin asedios: las murallas son obstáculos visuales + `defBonus` global.

**Objetivo**:
1. **Formaciones reales**: cada formación define multiplicadores (frente,
   profundidad, velocidad, vulnerabilidad a flanqueo/arco) en datos; aplicados en
   `calcFormationSlots` y en el cálculo de daño.
2. **Unificar auto-resolve con la simulación**: mismo motor, corrida headless con
   ticks reducidos → predicción creíble y coherente con el resultado.
3. **Sistema de moral y mando**: aura del general (radio), pérdida de aura al morir,
   pánico en cadena ya existente → parametrizar por datos.
4. **Asedio por turnos** (simplificado): al atacar provincia con murallas, 2 fases
   (asalto de muralla → batalla en la plaza) con bonus defensivo acumulativo.
5. **Terreno táctico legible**: hoy los obstáculos son AABB coloreados
   (`combat.cpp:1130-1148`). Objetivo: tiles/piezas gráficas (ríos con puentes
   marcados, bosques con cobertura real, lagos, ciudad con muralla).
6. **Cámara y espectador**: replays de batalla (grabación de inputs/estados) —
   contenido barato y muy "AAA".

**Datos**: `data/battle/formations.txt`, `data/battle/morale.txt`,
`data/battle/autoresolve.txt`, `data/battle/terrain_effects.txt`.

### 3.7 Diplomacia

**Hoy**: alianzas militares (límite 50% de potencia), pactos comerciales, trueque
con factor por facción (`campaign.cpp:1178-1195`), persistida en save.

**Objetivo (descafeinado pero sólido)**:
1. **Declaración de guerra / paz** con reglas (no atacar aliados sin romper alianza).
2. **Vasallaje simple**: protegido paga tributo y sigue órdenes básicas.
3. **Reputación** (0-100) que modifica disposición → sustituye al honor de STW.
4. **Propuestas con contrapartida** (oro por alianza, mapa a cambio de paz).
5. **IA de diplomacia con intereses**: agresión, miedo, avaricia, venganza
   (tabla por facción data-driven; hoy la "personalidad" es solo composicional).

**Datos**: `data/factions/<id>.txt` (nombre, color, disposición base, pesos de IA:
`aggression, greed, fear, vengefulness, expandBias`), `data/diplomacy/rules.txt`.

### 3.8 IA de campaña

**Hoy**: una fórmula (`aggression = 0.05 + 0.02*turn/10`, clamp 0.4, × dificultad)
+ BFS por territorio propio hacia el jugador (`campaign.cpp:895-1018`).
La dificultad **solo** modifica esa agresión.

**Objetivo — IA por objetivos (utility AI)**:
1. Cada facción evalúa candidatas: *expandir, reforzar, defender, comerciar,
   aliarse, explorar, asediar, castigar traidor* con utilidad = f(peso de facción,
   estado del mundo).
2. Presupuesto de acciones por turno (1-3) → comportamientos legibles y distintos.
3. **Dificultad en 3 ejes** (economía IA, bonus de batalla, información) en vez de
   un único multiplicador.
4. **Anti-snowball**: coaliciones anti-líder (ya hay germen en `campaign.cpp:900-921`),
   rebeliones en provincias lejanas con mucha ocupación.
5. Ejércitos IA con **doctrina de composición** derivada de sus edificios.

**Datos**: `data/ai/personality.txt`, `data/ai/utility_weights.txt`, `data/ai/difficulty.txt`.

### 3.9 Eventos, misiones y ritmo

**Hoy**: no existe nada (verificado: sin `event/season/mission` en `src/`).

**Objetivo (máximo gancho por esfuerzo)**:
1. **Motor de eventos data-driven**: condición (script mínimo o predicado declarativo)
   + efecto (modificar recursos, relación, reclutar, dañar) + texto i18n + opciones.
   Ej.: "Inundación en X: pierdes 20% de comida o pagas 300 de oro".
2. **Misiones secundarias** de facción (matar ejército, tomar provincia, alianza)
   con recompensa → guía al jugador y da narrativa.
3. **Estación**: 4 turnos = año; invierno modifica movimiento/ingresos (§3.2).
4. **Consejeros** (sustituto descafeinado del árbol familiar): 3 passivos elegibles
   cada N turnos.

**Datos**: `data/events/*.txt`, `data/missions/*.txt`.

### 3.10 Victoria, dificultad y onboarding

- **Condiciones de victoria data-driven**: conquista (% o capitales), supervivencia
  N turnos, puntuación económica, dominio naval. Hoy: fijo 80%/0% (`campaign.cpp:1030-1035`).
- **Tutoriales contextuales** (tooltips que aparecen la primera vez) — sin onboarding
  hoy el juego es inabrible para jugador nuevo.
- **Retos/achievements** y **resumen de campaña** final (pantalla de estadísticas:
  batallas ganadas, ejércitos destruidos, economía) → sensación AAA.

---

## 4. Madurez AAA por áreas

| Área | Hoy | Brecha AAA | Esfuerzo |
|---|---|---|---|
| **Motor/rendimiento** | Raylib, CPU rendering, sin shaders propios | Profiling, batching, LOD de sprites, partículas GPU, modo sin límite de FPS | M |
| **Gráficos** | Sprites Kenney CC0 + procedural; 1280×768 | Arte propio, animación por frames con skeletons, iluminación 2D (normal maps), post-proceso, UI con animaciones | **XL** |
| **Jugabilidad de campaña** | Bucle básico funcional | Sistemas de §3 (eventos, asedios, formaciones reales, IA por utilidad) | **XL** |
| **Batalla** | Competente pero con formaciones simuladas | Formaciones reales, auto-resolve coherente, replays, terreno legible | L |
| **IA** | 1 fórmula | Utility AI multi-objetivo, dificultad por ejes, anti-snowball | L |
| **Audio** | 13 SFX + música streaming (`audio.h`) | Banda sonora, VO de unidad, mezcla dinámica, música por capas según batalla | **XL** |
| **Narrativa/onboarding** | Ninguna | Tutorial, advisor, lore en loading screens, resumen final | M |
| **Multipartida** | Co-op host autoritativo + PvP parcial | Matchmaking/ladder, servidores, replays compartibles, anti-cheat básico, Observador | **XL** |
| **Moddability** | Cero | Capa de datos completa (§5) | **L (prioridad 1)** |
| **i18n** | `language` sin usar | Tablas de cadenas EN/ES + RTL-ready | M |
| **Producción/Calidad** | Tests UI por screenshot, sin CI | CI (build + tests headless), sanitizers, crash reporting, L10N, certificación de plataformas | M |
| **Código** | `main.cpp` 3522 líneas | Refactor por pantallas/UI modules, sin warnings | M |

> **L = 2-4 semanas, M = 4-8, XL = 2+ meses** de un solo dev con IA de apoyo.

---

## 5. MODDABILITY — arquitectura de datos

Este es el segundo énfasis y **el que desbloquea la conversión a guerra fría**.

### 5.1 Principios

1. **Datos, no código**: un mod de tema completo (guerra fría) no debe requerir
   recompilar. Solo recompilar si se añaden *sistemas* nuevos.
2. **IDs estables por string** (`unit.spear_levy`, `bld.farm`, `fac.louisiana_state`).
   Los índices numéricos solo viven dentro del motor y se resuelven en carga.
3. **Validación con errores claros** (archivo:línea) — un modder no debe leer C++.
4. **Overrides por capas**: `base/` → `mods/<orden>/` → `user/`. Gana el último.
5. **Versionado de esquema** (`schema = 2`) con migraciones automáticas.
6. **Nada de magia en el binario del save**: el save guarda IDs, no posiciones de enum.

### 5.2 Estructura propuesta de carpetas

```
data/
  base/                     # contenido oficial (versionado)
    units/*.unit
    buildings/*.bld
    maps/<mapid>/provinces.map  adjacency.txt  outline.map  factions.txt
    factions/*.fac
    battle/formations.txt  morale.txt  autoresolve.txt  terrain.txt
    events/*.evt  missions/*.mis
    strings/en.str  es.str
    lang_manifest.txt
  mods/
    coldwar/
      mod.ini               # name, version, author, schema, depends
      assets/               # sprites, música, iconos (misma estructura que assets/)
      data/                 # overrides + contenido nuevo (mismas rutas que base/)
  user/                     # unidades creadas con el editor de la UI
```

### 5.3 Formato de datos

Sin dependencias externas hoy (solo Raylib). Opciones:

| Opción | Pros | Contras | Recomendación |
|---|---|---|---|
| **INI/clave-valor por secciones** (extensión propia `.unit/.bld/.fac`) | Parser de 100 líneas, ya hay precedente (`config.cpp`), legible | Malo para listas anidadas | **Fase D1 (inmediato)** |
| JSON embebido (parser propio ~300 líneas) | Estándar de facto, herramientas de comunidad | Ruido de llaves | **Fase D2 (objetivo)** |
| Lua (vendor MIT) | Lógica arbitraria, eventos dinámicos | Curva, sandboxing | **Fase D4 (solo si hace falta)** |

**Regla**: empezar con declarativo (INI/JSON) y solo subir a scripting cuando un
sistema lo exija (eventos complejos, IA condicional).

Ejemplo de esquema de unidad:

```ini
; data/base/units/yari_levy.unit
schema = 1
id      = unit.yari_levy
name    = @unit.yari_levy.name          ; clave i18n
category = infantry
soldiers = 40
hp = 100
armor = 5
speed = 62
melee.attack = 14
melee.defense = 12
melee.dmg = 6
melee.interval = 1.1
morale = 55
recruit.gold = 90
recruit.turns = 2
requires = bld.barracks
sprite.base = Infantry
weapon = spear
lore = @unit.yari_levy.lore
```

### 5.4 Capas de implementación

| Fase | Qué | Aceptación |
|---|---|---|
| **D1 — Extract** | Mover unidades, edificios y facciones de C++ a `data/base/` con parser INI propio. El juego carga de datos; si falta archivo → fallback a builtins. | Juego idéntico con `data/` borrado (fallback) y con `data/` presente |
| **D2 — Mapas + JSON** | Loader de mapas (`provinces/adjacency/outline`) y migración del parser a JSON. Eliminar los 5 `ProvinceTemplate` embebidos. | Crear un mapa de 8 provincias solo con archivos → jugable |
| **D3 — Save por IDs** | `SAVE_VERSION=13`: guardar `typeId`/`buildingId` como strings o tabla de equivalencias. FOW dinámico. | Partida guardada con mod cargada sin mod → no corrompe; reordenar unidades no rompe |
| **D4 — Mod manager** | Carga de `mods/` con orden, overrides, `mod.ini`, pantalla de mods en el menú, validación de esquema. | Instalar un mod = copiar carpeta |
| **D5 — i18n** | Tablas de cadenas; reemplazar todos los literales de `DrawText`. La clave `language` por fin hace algo. | EN/ES completos; añadir idioma = 1 archivo |
| **D6 — Efectos declarativos** | Motor de efectos (`+gold 100`, `+relation facX 10`, `recruit.turns -1`) reutilizado por edificios, eventos, misiones, diplomacia. | Añadir un edificio nuevo sin tocar C++ |
| **D7 — Tools** | `tools/data_check.exe` (valida esquemas, referencias rotas, balance out-of-range) + export/import del editor de unidades. | CI ejecuta `data_check` en cada commit |
| **D8 — Scripting (opcional)** | Lua vendorizado para eventos/IA si lo declarativo se queda corto. | Mod de evento complejo sin tocar motor |

### 5.5 Refactor de código necesario como prerequisite

- **Romper `main.cpp`** en `ui/` (campaña, ciudad, batalla, menús, editor) — hoy
  3522 líneas hacen inviable cualquier extensión (`main.cpp`).
- **`GameData` como servicio central** (`gamedata.h`): registro de unidades,
  edificios, facciones, terrenos, efectos; consultados por id. Sustituye los
  arrays `extern const` de `city.h`.
- **IDs estables en todo el flujo**: `FieldArmy.units` pasa de `pair<int,int>` a
  `pair<UnitId,int>` (o tabla resuelta), `buildingReqs` bitmask → `vector<BuildId>`.
- **Eliminar duplicados** (`bldPrereq` en UI: D6) y **campos muertos** (D7).
- **`build.bat` no necesita cambios** (glob `src/*.cpp`) — añadir `src/data/*.cpp`
  basta con soltarlo en `src/`.

---

## 6. Reconversión a guerra fría

**Prueba de fuego del sistema de mods**: si la capa de datos funciona, la
conversión debe ser *un pack de datos + assets*, no un fork.

| Capa | Qué cambia | Dónde |
|---|---|---|
| Unidades | `Spear Levy` → `Rifle Squad`, `Knights` → `Main Battle Tank`, `Bow Levy` → `Artillery Battery`, `General` → `Field Marshal`, `King` → `Head of State` | `data/*/units/*.unit` + i18n |
| Edificios | `Farm` → `Food Complex`, `Sawmill` → `Lumber`, `Barracks` → `Barracks`, `Mage Tower` → `R&D Lab`, `Temple` → `Propaganda Office`, `Port` → `Naval Base`, `Watchtower` → `Radar Station` | `data/*/buildings/*.bld` |
| Facciones | 6 clanes → OTAN / Pacto de Varsovia / no alineados / etc. con colores y personalidad de IA | `data/*/factions/*.fac` |
| **Mapa** | Europa ya existe (36 provincias). Reajustar nombres a países/regiones modernas | `data/*/maps/europa/` |
| Terrenos | Mismo motor; añadir `URBAN`, `TUNDRA`, `DESERT` como tipos | `data/base/terrain/` |
| Batalla | Formaciones → `line/column/wedge`; efectos → cobertura urbana, trincheras | `data/*/battle/` |
| Arte | Sprites → **contadores NATO** (encaja perfecto con el sistema actual de celdas de color: `main.cpp:1368-1389` ya dibuja cuadrados con color por tipo). Assets por nombre en `mods/coldwar/assets/` | `mods/coldwar/assets/` |
| Audio | Música orquestal → synth/cold war score; SFX de espada → fusil/artillería | `mods/coldwar/assets/audio/` |
| Reglas | Bomba nuclear como evento/efecto; MAD como condición de derrota; inteligencia/espía (reutiliza el agente "emisario") | `data/*/events/`, `missions/` |
| Ventana/título | `Medieval Conquest` → nombre del mod (`main.cpp:3383`, `mod.ini`) | `mod.ini` |

**Criterio de éxito**: `rts_game.exe` con `mods/coldwar/` activo arranca en modo
guerra fría **sin recompilar**.

---

## 7. Roadmap por fases

Prioridad: **P0** = bloquea todo lo demás, **P1** = núcleo de jugabilidad,
**P2** = producción AAA, **P3** = contenido y expansión.

| Fase | Nombre | Prio | Contenido | Esfuerzo | Dependencias |
|---|---|---|---|---|---|
| **0** | Deuda v11 | **P0** | D1..D8 (§8), commit de v11, documentar en PLAN.md | 2-4 días | — |
| **D1** | Extracción de datos | **P0** | Unidades/edificios/facciones a `data/base/`, parser propio, fallback builtins | 1-2 sem | Fase 0 |
| **R1** | Refactor UI | **P0** | Partir `main.cpp` en `ui/*.cpp`; servicio `GameData` | 1 sem | Fase 0 |
| **G1** | Jugabilidad campaña I | **P1** | MP por ejército, asedios por turnos, bonus D2 reales, desembarque D3, victoria data-driven | 2 sem | D1 |
| **D2** | Mapas + JSON | **P1** | Loader de mapas, editor/exportador, FOW dinámico | 1-2 sem | D1 |
| **B1** | Batalla II | **P1** | Formaciones reales, auto-resolve coherente, aura de general, terreno legible | 2-3 sem | D1 |
| **I1** | IA de campaña II | **P1** | Utility AI, personalidad por facción, anti-snowball, dificultad por ejes | 2-3 sem | D1 |
| **E1** | Eventos + misiones + estaciones | **P1** | Motor de eventos data-driven, 15 eventos base, 8 misiones, año/estación | 2 sem | D1, D2 |
| **D3/D4** | Save por IDs + Mod manager | **P1** | SAVE v13, pantalla de mods, overrides, `data_check` | 2 sem | D1, D2 |
| **D5** | i18n | **P1** | EN/ES completos, literales fuera del código | 1-2 sem | R1 |
| **D6** | Efectos declarativos | **P1** | Motor de efectos compartido | 1 sem | D1 |
| **A1** | Arte y animación | **P2** | Sprites propios, animación, partículas, iluminación 2D, UI con motion | 2+ mes | — |
| **S1** | Audio | **P2** | Banda sonora, VO, mezcla dinámica | 1-2 mes | — |
| **N1** | Multipartida II | **P2** | Ladder/matchmaking, observer, replays, anti-cheat básico | 2+ mes | D3 |
| **Q1** | Producción | **P2** | CI (build+tests+`data_check`), sanitizers, crash report, rendimiento | 1-2 sem | D1 |
| **O1** | Onboarding | **P2** | Tutorial contextual, advisor, resumen de campaña, logros | 1-2 sem | D5 |
| **X1** | Expansión de contenido | **P3** | +100 unidades, +30 edificios, +3 mapas, doctrinas/tecnología | 2+ mes | D1..D6 |
| **CW** | Mod guerra fría | **P3** | Pack completo (§6) | 3-4 sem | D1..D6, A1 parcial |

**Orden crítico**: `0 → R1 → D1 → (D2, G1, B1, I1, E1) → D3/D4 → D5/D6 → P2/P3`.

### 7.1 Progreso de R1 y D1 (2026-10-12)

- [x] **R1 paso 1** — pantalla MULTIPLAYER extraída de `main.cpp` a
  `src/ui_multiplayer.{h,cpp}` (refactor mecánico: el bloque movido es
  byte-idéntico, mismas coordenadas/textos/colores/flujo; `main.cpp`
  3583 → 3469 líneas). Commit `02a84d7`. **Siguiente**: extraer el resto
  de pantallas (campaña, ciudad, batalla, menús, editor) a `ui/*.cpp`.
- [x] **D1 paso 1** — unidades data-driven: `data/base/units/*.unit`
  (10 unidades con los valores golden de `initBuiltinTypes()`), parser INI
  propio `src/gamedata.{h,cpp}` y fallback a builtins con aviso
  `archivo:línea` (consola + battleLog). Self-test headless `-datatest`
  (11 checks: golden / fallback / archivo inválido). Commit `476069f`.
- [x] **D1 paso 2** — edificios data-driven: `data/base/buildings/*.bld`
  (15 edificios con los valores golden de `city.cpp`, orden = enum
  BuildingType), `BuildingDef` + `defineBuiltinBuildings`/`applyBuildingDefs`,
  `bldCoastal` (Port) y sección BLDDATATEST del `-datatest` (12 checks).
  Commit `1d0b9be`. **Siguiente**: facciones `*.fac`.

---

## 8. Deuda técnica inmediata

Antes de empezar cualquier fase, cerrar (§1.3):

- [x] **D1** `explored[32]` → `std::vector<bool>` con tamaño real (bug funcional en Europa)
- [x] **D2** aplicar bonus de Barracks (reclutamiento) y Range/Stable (veteranía)
- [x] **D3** desembarque con selección de tipos/cantidades (selector [-] n [+] por tipo + LAND SELECTED; `disembarkTake` headless + fix P3, commit `283e23d`)
- [x] **D4** reparar/eliminar `build/test_battle.ps1`
- [x] **D5** commitear v11 + documentar fase en `PLAN.md` y `README.md`
- [x] **D6** eliminar `bldPrereq` duplicado de la UI
- [x] **D7** borrar `PreBattleState::formationChoice` muerto
- [x] **D8** formación pre-batalla sin mutar `g_settings` global
- [x] Poner `settings.example.ini` al día con las 4 claves de formación/prep

---

## 9. Riesgos

| Riesgo | Impacto | Mitigación |
|---|---|---|
| Alcance AAA realista para 1 dev | Alto | Priorizar D1 (modding) + jugabilidad; tratar arte/audio como contratados o packs |
| Romper el save al cambiar a IDs | Alto | Migrador con `SAVE_VERSION=13` y pruebas de compat v8→v13 en CI |
| Refactor de `main.cpp` rompe UI | Alto | Tests screenshot existentes antes y después (`test_faseJ`, `test_map`); refactor incremental por pantalla |
| Red (host autoritativo) no escala a PvP serio | Medio | Mantener co-op; MVP PvP solo LAN/ladder local |
| Parser de datos mal diseñado → reescritura | Medio | `schema` versionado + `data_check` desde el día 1 |
| Perder "feel" del juego al data-driven | Medio | Presets oficiales idénticos a los valores actuales (golden tests de balance) |

---

## 10. Anexo A — Inventario de contenido hardcodeado

| Contenido | Ubicación | Volumen |
|---|---|---|
| Unidades (stats, lore, requisitos) | `elements.cpp:25-71` | 10 tipos, ~25 campos c/u |
| Edificios (nombres, costes, turnos, prereqs, yields) | `city.cpp:19-37` | 15 × 10 arrays |
| Prerrequisitos duplicados en UI | `main.cpp:1828` | duplicado |
| Mapas de campaña (plantillas + vecindad) | `campaign.cpp:369-704` | 5 mapas, 36 máx. |
| Siluetas de continente | `mapart.cpp:104-154` | 5 polígonos |
| Facciones (enum, nombres, colores) | `config.h:90`, `campaign.h:11-14` | 6 |
| IA de campaña (fórmula de agresión) | `campaign.cpp:895-1018` | 1 fórmula |
| Balance de combate | `combat.cpp:40-139,275-289,602-642` | ~30 constantes mágicas |
| Efectos de terreno en batalla | `combat.cpp:52-56` | +25/+15/+10 % |
| Textos de UI | `main.cpp` passim | cientos de literales EN |
| Música/SFX (rutas e ids) | `audio.cpp:16-46` | 13 SFX, 2 pistas |
| Nombres de campaña en menú | `main.cpp:3136-3140` | 5 botones fijos |
| Victoria/derrota (80 %) | `campaign.cpp:1030-1035` | 1 regla |
| Ajustes de usuario | `config.cpp:25-72` | 13 claves (único data-driven real) |
| Build | `build.bat:16` | glob `src/*.cpp` (autoincluye módulos) |

---

## 11. Anexo B — Requisitos de aceptación

**Fase 0**: `-nettest`, `test_faseJ`, `test_ranged`, `test_h_sync` en verde; 0 errores
de compilación; v11/v12 commiteado; D1, D2, D4–D8 cerrados, D3 mitigado
(LAND HALF; selector arbitrario → G1).

**Fase D1**: con `data/` vacío el juego funciona (fallback); con `data/` presente,
las unidades/edificios/facciones son idénticos a los actuales (golden values);
`data_check` detecta referencias rotas.

**Fase D2**: mapa nuevo de 8 provincias creado solo con archivos jugable de
principio a fin (nueva partida, batalla, victoria).

**Fase D3**: guardar partida con mod A, arrancar sin mod → no corrompe, avisa;
reordenar tipos de unidad en datos no desplaza nada en la partida.

**Fase D4**: instalar un mod = copiar carpeta a `mods/` y activarlo en el menú.

**Fase D5**: cambiar `settings.ini` `language 1` traduce el 100 % del UI; añadir un
idioma = añadir 1 archivo de cadenas.

**Fase CW**: `mods/coldwar/` activo → el juego arranca en guerra fría sin
recompilar, con mapa Europa, unidades blindadas y condiciones de victoria propias.
