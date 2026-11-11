# OpenBF1942-Plus

<p align="center"><img src="docs/banner.svg" alt="OpenBF1942-Plus" width="100%"></p>

![Status: early prototype](https://img.shields.io/badge/status-early%20prototype-orange)
![License: AGPL-3.0](https://img.shields.io/badge/license-AGPL--3.0-blue)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-lightgrey)
![C++17 | OpenGL 3.3](https://img.shields.io/badge/C%2B%2B17-OpenGL%203.3-lightgrey)
![Battlefield 1942](https://img.shields.io/badge/Battlefield-1942-8a7a52)

**Español** · [English](README.md) · [Português](README.pt.md) · [Français](README.fr.md) · [Deutsch](README.de.md) · [Русский](README.ru.md)

**Battlefield 1942 en un motor nuevo.** OpenBF1942-Plus es una reimplementación clean-room de Refractor 1, el motor de Battlefield 1942, escrita desde cero: lee los archivos del propio juego desde tu instalación y los ejecuta en sistemas modernos. El objetivo es jugar con y contra el juego original: clientes originales en nuestro servidor, nuestro cliente en servidores originales.

<table>
  <tr>
    <td><img src="docs/images/first_person.jpg" alt="El Alamein en primera persona con el BAR"></td>
    <td><img src="docs/images/third_person.jpg" alt="Un soldado británico en tercera persona en El Alamein"></td>
  </tr>
  <tr>
    <td colspan="2"><img src="docs/images/berlin.jpg" alt="Una calle de Berlín con erizos antitanque"></td>
  </tr>
</table>
<p align="center"><sub>El Alamein y Berlín, dibujados por OpenBF1942-Plus a partir de los archivos del juego original.</sub></p>

## Qué funciona hoy

- **Todos los mapas de Battlefield 1942, Road to Rome y Secret Weapons of WWII cargan y se dibujan**: terreno, edificios, árboles, agua, cielo, niebla e iluminación según la configuración de cada mapa.
- **Una ronda de conquista contra bots** en cualquier mapa: banderas, tickets, kits, cargadores y recargas, granadas, minas y cargas explosivas, depósitos de suministros, agacharse y tumbarse, paracaídas, primera y tercera persona.
- **Vehículos**: coches, tanques, aviones y barcos que puedes conducir, con asientos para artilleros y pasajeros; los bots conducen y disparan vehículos terrestres y lanchas de desembarco.
- **Sonido y HUD** a partir de los scripts de sonido y las fuentes del juego.
- **Comparado con el cliente original** en los mismos puntos de Berlín y El Alamein: edificios, bruma, cielo, vehículos y arena dentro de un 15 % aproximado.
- **Investigación de red**: una sesión cliente/servidor original grabada; el protocolo de consulta del servidor documentado.

> [!NOTE]
> **Prototipo temprano.** Una persona solo ha probado partes pequeñas, muchas reglas son suposiciones y nada está calibrado. Todavía no hay menús ni multijugador, y las calles de Berlín siguen más oscuras que en el original.

> [!IMPORTANT]
> **No se incluye contenido del juego: trae tus propios datos.** Este repositorio es solo código fuente: sin ejecutables, archivos, mapas, modelos, texturas ni sonidos. El motor los lee de una copia de Battlefield 1942 que **posees legalmente**, indicada aquí como `<BF1942_DIR>`.

## Cómo jugar

Todavía no hay builds listas para usar: compila el motor (ver [Compilación](#compilación)) y apúntalo a tu carpeta de Battlefield 1942, la que contiene `Mods/`:

```sh
build/obf_mapview "<BF1942_DIR>" bf1942 El_Alamein --play 2      # conquista en el equipo 2, kit de asalto
build/obf_mapview "<BF1942_DIR>" bf1942 Wake                     # cámara libre
build/obf_mapview "<BF1942_DIR>" bf1942 El_Alamein --drive Willy  # un vehículo delante de la cámara
build/obf_sim "<BF1942_DIR>" bf1942 El_Alamein --play 2 --sim 600 # sin ventana: 10 minutos de conquista, en texto
```

| Controles | |
|---|---|
| Moverse, mirar | WASD, ratón (capturado; F10 lo libera), shift camina, espacio salta (cayendo: abre el paracaídas) |
| Armas | clic izquierdo dispara, R recarga, 1–9 armas del kit, G detona las cargas explosivas |
| Postura, vista | ctrl izquierdo agacharse, Z tumbarse, V primera / tercera persona |
| Vehículos | E subir / bajar, F1–F8 cambiar de asiento, WASD conducir, el ratón apunta las torretas |
| Aviones | W / S gas, flechas cabeceo y alabeo, A / D timón |
| Cámara libre | botón derecho mira, WASD / QE mueven, shift rápido; C muestra las mallas de colisión; Esc sale |

## Hoja de ruta

El orden, con los motivos y los criterios de salida, está en [docs/roadmap/roadmap.md](docs/roadmap/roadmap.md):

1. **Consolidar** (hecho): el juego como biblioteca, un renderizador y ejecutables finos; integración continua.
2. **Investigación del protocolo de red** (una sesión grabada): decodificar el tráfico entre cliente y servidor originales, en paralelo con los pasos siguientes.
3. **Navegación de la IA**, **audio**, **HUD** (primeras partes hechas).
4. **Soldados y armas completos** (casi hecho), **tripulaciones de vehículos** (tierra y mar hechos, pilotos pendientes), **efectos**.
5. **Fidelidad**: medir el original y calibrar.
6. **Modos de juego**, **menús**.
7. **Red**: servidor y cliente propios, y luego jugar junto con el original.
8. **Render**, **distribución** (builds para Windows y Linux, pantalla panorámica, mods).

## Para colaboradores

Toda ayuda cuenta: probarlo e informar de lo que falla, compararlo con el juego original, o código. La documentación técnica, en inglés, empieza en [docs/README.md](docs/README.md): formatos de archivo, decisiones de arquitectura y deuda conocida. [AGENTS.md](AGENTS.md) es el punto de entrada para agentes de programación con IA.

## Compilación

C++17, CMake, SDL2, OpenGL 3.3 (libepoxy) y Jolt Physics, que CMake descarga al configurar la primera vez. En Debian o Ubuntu:

```sh
sudo apt install cmake g++ git libsdl2-dev libepoxy-dev
cmake -S . -B build && cmake --build build -j
ctest --test-dir build
# -> build/obf_mapview (ventana), build/obf_sim (sin ventana)
```

<details>
<summary><b>Herramientas en Python</b></summary>

Lectores de los formatos del juego, solo con la librería estándar de Python 3.

```sh
python3 tools/rfa.py list    "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa"
python3 tools/rfa.py extract "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa" out/
python3 tools/con.py "<BF1942_DIR>" bf1942 Wake    # cargar un mapa, imprimir un resumen
python3 tools/sm.py "<BF1942_DIR>" bf1942 standardMesh/willy_hul_m1 out/jeep   # OBJ + texturas
python3 -m unittest discover tools     # tests
python3 tools/check_docs.py            # chequeos de documentación
```

</details>

<details>
<summary><b>Qué hay en cada sitio</b></summary>

| Ruta | Contenido |
|---|---|
| `engine` | El motor en C++: archivos, scripts, mallas, terreno, animación, física, reglas de juego, IA, audio, HUD, renderizador y los ejecutables |
| `tools` | Lectores de los formatos en Python, el comprobador de documentación y sus tests |
| `docs/formats` | Las especificaciones de los formatos de archivo y de red |
| `docs/architecture` | Capas, límites y las decisiones de arquitectura (ADR) |
| `docs/roadmap` | La hoja de ruta y la deuda conocida |
| `docs/ai` | La capa de memoria para agentes de IA |

</details>

## Transparencia

- **Quién lo mantiene:** OpenGamePlus. Me gustan los juegos antiguos y quiero seguir jugando a Battlefield 1942 con amigos en los sistemas actuales, con un motor abierto que la comunidad pueda mantener vivo.
- **Cómo se hace el trabajo:** trabajo con [Claude Code](https://claude.com/claude-code), el asistente de programación de Anthropic. El código y la documentación, incluidos este README y sus traducciones, se escriben con su ayuda. Los commits hechos así llevan una línea `Co-Authored-By: Claude` en su mensaje, de modo que se pueden identificar en el historial con `git log`.
- **Clean room:** no se usa ni se descompila código del motor original. El motor se escribe a partir de los archivos de datos del juego y de observar el juego original en ejecución.
- **Qué no es:** no es un proyecto oficial y no está afiliado a Electronic Arts ni a DICE. No contiene archivos del juego.

## Aviso

Esta es una reimplementación independiente del motor Refractor 1. No está afiliada, autorizada, patrocinada ni respaldada por Electronic Arts Inc., EA Digital Illusions CE (DICE) ni ninguna de sus filiales.

"Battlefield" y "Battlefield 1942" son marcas comerciales de Electronic Arts Inc. En este repositorio se usan solo con fines de identificación e interoperabilidad, para describir lo que ejecuta el motor. No se reclama ningún derecho sobre esas marcas.

Este proyecto no proporciona datos del juego protegidos por derechos de autor ni ayuda a obtenerlos. Para usarlo con contenido real del juego, debes aportar los archivos de datos de una copia de Battlefield 1942 que poseas legalmente.

Si eres titular de derechos y crees que algo de aquí no debería distribuirse, abre una issue o contacta con el mantenedor y se atenderá con prontitud.

Las capturas de pantalla muestran contenido de Battlefield 1942, propiedad de Electronic Arts Inc., solo con fines de identificación.

## Licencia

[GNU Affero General Public License v3.0](LICENSE). Quien distribuya el motor, o ejecute una versión modificada como servidor de red para otros, debe ofrecer su código fuente bajo la misma licencia. Las bibliotecas de terceros mantienen las suyas: SDL2 (zlib), libepoxy (MIT) y Jolt Physics (MIT).

## Créditos

- [Jolt Physics](https://github.com/jrouwe/JoltPhysics), [SDL](https://www.libsdl.org/) y [libepoxy](https://github.com/anholt/libepoxy)
- Electronic Arts / DICE, por el juego
