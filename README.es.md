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
| 4. Terreno + objetos | hecho — todos los mapas con edificios, árboles, texturas de detalle, niebla y cielo de cada mapa |
| 5. Mallas con esqueleto + animaciones | hecho — soldados, armas en la mano, brazos en primera persona, animaciones de disparo y recarga |
| 6. Física + vehículos | hecho, sin calibrar — coches, tanques, aviones, barcos; tripulaciones con asientos (F1–F8), soldados sentados |
| 7. Jugabilidad | jugable — conquista contra bots en cualquier mapa: kits, cargadores, granadas, explosiones, depósitos de munición, agacharse / tumbarse, paracaídas; los bots siguen los mapas de navegación originales, conducen y disparan vehículos terrestres y lanchas de desembarco; sonido y HUD |
| 8. Red | investigación — una sesión cliente/servidor original grabada, protocolo de consulta documentado |

Comparado lado a lado con el cliente original (Berlín, El Alamein): edificios, bruma, cielo, vehículos y arena dentro de un 15 % aproximado; las calles de Berlín siguen más oscuras que en el original.

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
build/obf_mapview "<BF1942_DIR>" bf1942 El_Alamein --play 2      # jugar conquista (equipo 2, asalto)
build/obf_mapview "<BF1942_DIR>" bf1942 Wake                     # cámara libre
build/obf_mapview "<BF1942_DIR>" bf1942 El_Alamein --drive Willy  # un vehículo delante de la cámara
build/obf_sim "<BF1942_DIR>" bf1942 El_Alamein --play 2 --sim 600 # sin ventana: 10 minutos de conquista, en texto
```

Controles al jugar:

| | |
|---|---|
| Moverse, mirar | WASD, ratón (capturado; F10 lo libera), shift camina, espacio salta (cayendo: abre el paracaídas) |
| Armas | clic izquierdo dispara, R recarga, 1–9 armas del kit, G detona cargas explosivas |
| Postura, vista | ctrl izquierdo agacharse, Z tumbarse, V primera / tercera persona |
| Vehículos | E subir / bajar, F1–F8 cambiar de asiento, WASD conducir, el ratón apunta torretas |
| Aviones | W / S gas, flechas cabeceo y alabeo, A / D timón |
| Cámara libre | botón derecho mira, WASD / QE mueven, shift rápido; C muestra mallas de colisión; Esc sale |

## Hoja de ruta

Objetivo: jugar con clientes y servidores originales de Battlefield 1942. Lo siguiente, en orden ([hoja de ruta completa, en inglés](docs/roadmap/roadmap.md)):

1. **Consolidar** (hecho) — separar el visor en una biblioteca de juego y un renderizador; integración continua.
2. **Investigación del protocolo de red** (una sesión grabada) — decodificar el tráfico entre cliente y servidor originales (en paralelo con lo siguiente).
3. **Navegación de la IA**, **audio**, **HUD** (primeras partes hechas).
4. **Soldados y armas completos** (casi hecho), **tripulaciones de vehículos** (tierra y mar hechos, pilotos pendientes), **efectos**.
5. **Fidelidad** — medir el original y calibrar.
6. **Modos de juego**, **menús**.
7. **Red** — servidor y cliente propios, luego compatibilidad con el original.
8. **Render**, **distribución** (Windows/Linux, pantalla panorámica, mods).

## Documentación

[docs/README.md](docs/README.md) (en inglés) — formatos, decisiones de arquitectura, deuda. Asistentes IA: empezar en [AGENTS.md](AGENTS.md).
