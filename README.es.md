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
| 7. Jugabilidad | empezado — una partida de conquista contra bots (`--play 2 --kit 1`): banderas, tickets, victoria; conducir cualquier vehículo aparcado (E), disparar (clic izquierdo); blindaje y tablas de daño originales |
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
build/obf_sim "<BF1942_DIR>" bf1942 El_Alamein --play 2 --sim 600   # sin ventana: 10 minutos de conquista, en texto
build/obf_mapview "<BF1942_DIR>" bf1942 El_Alamein --drive Willy   # WASD conduce, botón derecho apunta torretas, clic izquierdo dispara, R recarga; aviones: W/S gas, flechas, A/D timón
```

## Hoja de ruta

Objetivo: jugar con clientes y servidores originales de Battlefield 1942. Lo siguiente, en orden ([hoja de ruta completa, en inglés](docs/roadmap/roadmap.md)):

1. **Consolidar** (hecho) — separar el visor en una biblioteca de juego y un renderizador; integración continua.
2. **Investigación del protocolo de red** — grabar y decodificar el tráfico entre cliente y servidor originales (en paralelo con lo siguiente).
3. **Navegación de la IA** (primera parte hecha), **audio** (suenan armas y motores), **HUD** (primera parte hecha).
4. **Soldados y armas completos**, **tripulaciones de vehículos**, **efectos**.
5. **Fidelidad** — medir el original y calibrar.
6. **Modos de juego**, **menús**.
7. **Red** — servidor y cliente propios, luego compatibilidad con el original.
8. **Render**, **distribución** (Windows/Linux, pantalla panorámica, mods).

## Documentación

[docs/README.md](docs/README.md) (en inglés) — formatos, decisiones de arquitectura, deuda. Asistentes IA: empezar en [AGENTS.md](AGENTS.md).
