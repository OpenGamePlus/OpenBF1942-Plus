# OpenBF1942-Plus

[English](README.md)

Reimplementación clean-room del motor Refractor 1 para que Battlefield 1942 funcione en sistemas modernos.

**No se incluye ni redistribuye ningún dato del juego.** El motor lee los recursos de tu propia instalación, indicada como `<BF1942_DIR>`.

## Estado

| Paso | Estado |
|---|---|
| 1. Archivos (RFA) | hecho — listar/extraer, verificado con la instalación original completa |
| 2. Scripts (`.con`) | hecho — intérprete; cargan todos los mapas de los tres mods |
| 3–8 | sin empezar (ver [hoja de ruta](#hoja-de-ruta)) |

## Herramientas

Python 3, solo librería estándar.

```
python3 tools/rfa.py list    "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa"
python3 tools/rfa.py extract "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa" out/
python3 tools/con.py "<BF1942_DIR>" bf1942 Wake    # cargar un mapa, ver resumen
python3 -m unittest discover tools     # tests
python3 tools/check_docs.py            # chequeo de documentación
```

## Hoja de ruta

1. **Archivos (RFA)** — leer todos los ficheros del juego.
2. **Scripts (`.con`)** — intérprete + sistema de plantillas de objetos.
3. **Mallas estáticas (`.sm`) + texturas (`.dds`/`.tga`)** — visor de modelos.
4. **Terreno** — visor de mapas: recorrer un mapa.
5. **Mallas con esqueleto + animaciones (`.skn`/`.ske`/`.baf`)** — soldados.
6. **Física + vehículos** — conducir un jeep.
7. **Reglas de juego, armas, IA** — partida de un jugador.
8. **Red** — protocolo original sin documentar.

## Documentación

[docs/README.md](docs/README.md) (en inglés) — formatos, decisiones de arquitectura, deuda. Asistentes IA: empezar en [AGENTS.md](AGENTS.md).
