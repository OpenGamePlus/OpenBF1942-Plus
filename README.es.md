# OpenBF1942-Plus

[English](README.md)

Reimplementación clean-room del motor Refractor 1 para que Battlefield 1942 funcione en sistemas modernos.

**No se incluye ni redistribuye ningún dato del juego.** El motor lee los recursos de tu propia instalación, indicada como `<BF1942_DIR>`.

## Estado

| Paso | Estado |
|---|---|
| 1. Archivos (RFA) | hecho — listar/extraer, verificado con la instalación original completa |
| 2. Scripts (`.con`) | hecho — intérprete; cargan todos los mapas de los tres mods |
| 3. Mallas estáticas (`.sm`) | hecho — todas las mallas se leen; exportación OBJ abre en Blender |
| 4. Terreno + objetos | hecho — el visor C++ muestra todos los mapas con edificios y árboles |
| 5. Mallas con esqueleto + animaciones | en curso — soldados y banderas animados en el visor (`--soldier USSoldier`) |
| 6. Física + vehículos | en curso — coches, tanques, aviones y barcos en el visor (`--drive Willy`, `Sherman`, `Spitfire`, `LCVP`); manejo aún sin calibrar |
| 7. Jugabilidad | empezado — jugar un equipo con su kit (`--play 2 --kit 1`) contra soldados enemigos que disparan y corren hacia las banderas; conducir cualquier vehículo aparcado (E), disparar (clic izquierdo); blindaje y tablas de daño originales |
| 8 | sin empezar (ver [hoja de ruta](#hoja-de-ruta)) |

## Herramientas

Python 3, solo librería estándar.

```
python3 tools/rfa.py list    "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa"
python3 tools/rfa.py extract "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa" out/
python3 tools/con.py "<BF1942_DIR>" bf1942 Wake    # cargar un mapa, ver resumen
python3 tools/sm.py "<BF1942_DIR>" bf1942 standardMesh/willy_hul_m1 out/jeep   # OBJ + texturas
python3 -m unittest discover tools     # tests
python3 tools/check_docs.py            # chequeo de documentación
```

## Motor

C++17, CMake, SDL2, OpenGL 3.3 (libepoxy), Jolt Physics (CMake lo descarga al configurar la primera vez). En Debian/Ubuntu: `apt install cmake g++ git libsdl2-dev libepoxy-dev`.

```
cmake -S . -B build && cmake --build build -j
ctest --test-dir build
build/obf_mapview "<BF1942_DIR>" bf1942 Wake      # botón derecho: mirar, WASD/QE: mover, shift: rápido
build/obf_mapview "<BF1942_DIR>" bf1942 El_Alamein --drive Willy   # WASD conduce, botón derecho apunta torretas, clic izquierdo dispara; aviones: W/S gas, flechas, A/D timón
```

## Hoja de ruta

1. **Archivos (RFA)** — leer todos los ficheros del juego.
2. **Scripts (`.con`)** — intérprete + sistema de plantillas de objetos.
3. **Mallas estáticas (`.sm`) + texturas (`.dds`/`.tga`)** — exportación a Blender.
4. **Terreno** — visor de mapas: recorrer un mapa.
5. **Mallas con esqueleto + animaciones (`.skn`/`.ske`/`.baf`)** — soldados.
6. **Física + vehículos** — conducir un jeep.
7. **Reglas de juego, armas, IA** — partida de un jugador.
8. **Red** — protocolo original sin documentar.

## Documentación

[docs/README.md](docs/README.md) (en inglés) — formatos, decisiones de arquitectura, deuda. Asistentes IA: empezar en [AGENTS.md](AGENTS.md).
