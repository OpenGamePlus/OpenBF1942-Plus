# OpenBF1942-Plus

<p align="center"><img src="docs/banner.svg" alt="OpenBF1942-Plus" width="100%"></p>

![Status: early prototype](https://img.shields.io/badge/status-early%20prototype-orange)
![License: AGPL-3.0](https://img.shields.io/badge/license-AGPL--3.0-blue)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-lightgrey)
![C++17 | OpenGL 3.3](https://img.shields.io/badge/C%2B%2B17-OpenGL%203.3-lightgrey)
![Battlefield 1942](https://img.shields.io/badge/Battlefield-1942-8a7a52)

[Español](README.es.md) · [English](README.md) · [Português](README.pt.md) · **Français** · [Deutsch](README.de.md) · [Русский](README.ru.md)

**Battlefield 1942 sur un nouveau moteur.** OpenBF1942-Plus est une réimplémentation en salle blanche de Refractor 1, le moteur de Battlefield 1942, écrite de zéro : elle lit les fichiers du jeu depuis votre installation et les exécute sur des systèmes modernes. L'objectif est de jouer avec et contre le jeu original : des clients originaux sur notre serveur, notre client sur des serveurs originaux.

<table>
  <tr>
    <td><img src="docs/images/first_person.jpg" alt="El Alamein in first person with the BAR"></td>
    <td><img src="docs/images/third_person.jpg" alt="A British soldier in third person at El Alamein"></td>
  </tr>
  <tr>
    <td colspan="2"><img src="docs/images/berlin.jpg" alt="A street in Berlin with tank traps"></td>
  </tr>
</table>
<p align="center"><sub>El Alamein et Berlin, rendus par OpenBF1942-Plus à partir des fichiers du jeu commercial.</sub></p>

## Ce qui fonctionne aujourd'hui

- **Chaque niveau de Battlefield 1942, Road to Rome et Secret Weapons of WWII se charge et s'affiche** : terrain, bâtiments, arbres, eau, ciel, brouillard et éclairage selon les réglages propres à chaque niveau.
- **Une partie de conquête contre des bots** sur n'importe quel niveau : drapeaux, tickets, kits, chargeurs et rechargements, grenades, mines et charges explosives, dépôts de ravitaillement, accroupi et couché, parachutes, première et troisième personne.
- **Véhicules** : voitures, chars, avions et bateaux que l'on peut conduire, avec des places pour les mitrailleurs et les passagers ; les bots conduisent et servent les armes des véhicules terrestres et des péniches de débarquement.
- **Son et HUD** à partir des scripts sonores et des polices du jeu.
- **Comparé au client original** aux mêmes endroits de Berlin et d'El Alamein : bâtiments, brume, ciel, véhicules et sable à environ 15 % près.
- **Recherche réseau** : une session client/serveur originale enregistrée ; le protocole de requête du serveur documenté.

> [!NOTE]
> **Prototype précoce.** Seules de petites parties ont été essayées par une personne, beaucoup de règles sont des suppositions et rien n'est calibré. Il n'y a pas de menus, pas encore de multijoueur, et les rues de Berlin sont encore plus sombres que dans l'original.

> [!IMPORTANT]
> **Aucun contenu du jeu n'est inclus — apportez vos propres données.** Ce dépôt ne contient que du code source : pas d'exécutables, d'archives, de cartes, de modèles, de textures ni de sons. Le moteur les lit depuis une copie de Battlefield 1942 que **vous possédez légalement**, désignée ici par `<BF1942_DIR>`.

## Comment jouer

Il n'y a pas encore de builds prêts à l'emploi : compilez le moteur (voir [Compilation](#compilation)) et indiquez-lui votre dossier Battlefield 1942, celui qui contient `Mods/` :

```sh
build/obf_mapview "<BF1942_DIR>" bf1942 El_Alamein --play 2      # conquête dans l'équipe 2, kit d'assaut
build/obf_mapview "<BF1942_DIR>" bf1942 Wake                     # caméra libre
build/obf_mapview "<BF1942_DIR>" bf1942 El_Alamein --drive Willy  # un véhicule devant la caméra
build/obf_sim "<BF1942_DIR>" bf1942 El_Alamein --play 2 --sim 600 # sans affichage : 10 minutes de conquête, imprimées
```

| Commandes | |
|---|---|
| Se déplacer, regarder | WASD, souris (capturée ; F10 la libère), shift pour marcher, espace pour sauter (en chute : ouvre le parachute) |
| Armes | clic gauche pour tirer, R pour recharger, 1–9 armes du kit, G fait exploser les charges explosives |
| Posture, vue | ctrl gauche pour s'accroupir, Z pour se coucher, V première / troisième personne |
| Véhicules | E pour monter / descendre, F1–F8 changent de place, WASD pour conduire, la souris vise avec les tourelles |
| Avions | W / S gaz, flèches tangage et roulis, A / D gouvernail |
| Caméra libre | clic droit pour regarder, WASD / QE pour se déplacer, shift rapide ; C affiche les maillages de collision ; Esc quitte |

## Feuille de route

L'ordre, avec les raisons et les critères de sortie, se trouve dans [docs/roadmap/roadmap.md](docs/roadmap/roadmap.md) :

1. **Consolider** (fait) : le jeu sous forme de bibliothèque, un moteur de rendu et des exécutables minces ; CI.
2. **Recherche sur le protocole réseau** (une session enregistrée) : décoder le trafic client/serveur original, en parallèle des étapes ci-dessous.
3. **Navigation de l'IA**, **audio**, **HUD** (premières parties faites).
4. **Soldats et armes complets** (en grande partie fait), **équipages de véhicules** (terre et mer faits, pilotes en cours), **effets**.
5. **Fidélité** : mesurer l'original et calibrer.
6. **Modes de jeu**, **menus**.
7. **Réseau** : notre propre serveur et client, puis jouer ensemble avec l'original.
8. **Rendu**, **distribution** (builds Windows et Linux, écran large, mods).

## Pour les contributeurs

Toute forme d'aide compte : l'essayer et signaler ce qui ne va pas, comparer avec le jeu original, ou du code. La documentation technique, en anglais, commence à [docs/README.md](docs/README.md) : formats de fichiers, décisions d'architecture et dette connue. [AGENTS.md](AGENTS.md) est le point d'entrée pour les agents de programmation IA.

## Compilation

C++17, CMake, SDL2, OpenGL 3.3 (libepoxy) et Jolt Physics, que CMake télécharge lors de la première configuration. Sur Debian ou Ubuntu :

```sh
sudo apt install cmake g++ git libsdl2-dev libepoxy-dev
cmake -S . -B build && cmake --build build -j
ctest --test-dir build
# -> build/obf_mapview (fenêtre), build/obf_sim (sans affichage)
```

<details>
<summary><b>Outils Python</b></summary>

Lecteurs pour les formats du jeu, uniquement avec la bibliothèque standard de Python 3.

```sh
python3 tools/rfa.py list    "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa"
python3 tools/rfa.py extract "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa" out/
python3 tools/con.py "<BF1942_DIR>" bf1942 Wake    # charger un niveau, afficher un résumé
python3 tools/sm.py "<BF1942_DIR>" bf1942 standardMesh/willy_hul_m1 out/jeep   # OBJ + textures
python3 -m unittest discover tools     # tests
python3 tools/check_docs.py            # vérifications de la documentation
```

</details>

<details>
<summary><b>Où se trouve quoi</b></summary>

| Chemin | Contenu |
|---|---|
| `engine` | Le moteur C++ : archives, scripts, maillages, terrain, animation, physique, règles du jeu, IA, audio, HUD, moteur de rendu, et les exécutables |
| `tools` | Lecteurs Python pour les formats, le vérificateur de documentation et leurs tests |
| `docs/formats` | Les spécifications des formats de fichiers et de réseau |
| `docs/architecture` | Couches, frontières et décisions d'architecture (ADR) |
| `docs/roadmap` | La feuille de route et la dette connue |
| `docs/ai` | La couche de mémoire pour les agents IA |

</details>

## Transparence

- **Qui maintient ce projet :** OpenGamePlus. J'aime les vieux jeux, et je veux continuer à jouer à Battlefield 1942 avec des amis sur les systèmes actuels, avec un moteur ouvert que la communauté peut faire vivre.
- **Comment le travail est fait :** je travaille avec [Claude Code](https://claude.com/claude-code), l'assistant de programmation d'Anthropic. Le code et la documentation, y compris ce README et ses traductions, sont écrits avec son aide. Les commits faits de cette façon portent une ligne `Co-Authored-By: Claude` dans leur message, ce qui permet de les identifier dans l'historique avec `git log`.
- **Salle blanche :** aucun code du moteur original n'est utilisé ni décompilé. Le moteur est écrit à partir des fichiers de données du jeu et de l'observation du jeu original en fonctionnement.
- **Ce que ce n'est pas :** ce n'est pas un projet officiel et il n'est pas affilié à Electronic Arts ni à DICE. Il ne contient aucun fichier du jeu.

## Avis

Ceci est une réimplémentation indépendante du moteur Refractor 1. Elle n'est pas affiliée à, autorisée par, sponsorisée par ni approuvée par Electronic Arts Inc., EA Digital Illusions CE (DICE) ou l'une de leurs sociétés affiliées.

« Battlefield » et « Battlefield 1942 » sont des marques d'Electronic Arts Inc. Elles ne sont utilisées dans ce dépôt qu'à des fins d'identification et d'interopérabilité, pour décrire ce que le moteur exécute. Aucun droit n'est revendiqué sur ces marques.

Ce projet ne fournit pas de données de jeu protégées par le droit d'auteur et n'aide pas à en obtenir. Pour l'utiliser avec du vrai contenu de jeu, vous devez fournir les fichiers de données d'une copie de Battlefield 1942 que vous possédez légalement.

Si vous êtes titulaire de droits et pensez que quelque chose ici ne devrait pas être distribué, veuillez ouvrir une issue ou contacter le mainteneur, et le problème sera traité rapidement.

Les captures d'écran montrent du contenu de Battlefield 1942, propriété d'Electronic Arts Inc., à des fins d'identification uniquement.

## Licence

[GNU Affero General Public License v3.0](LICENSE). Quiconque distribue le moteur, ou exécute une version modifiée comme serveur réseau pour d'autres, doit en proposer le code source sous la même licence. Les bibliothèques tierces conservent la leur : SDL2 (zlib), libepoxy (MIT) et Jolt Physics (MIT).

## Crédits

- [Jolt Physics](https://github.com/jrouwe/JoltPhysics), [SDL](https://www.libsdl.org/) et [libepoxy](https://github.com/anholt/libepoxy)
- Electronic Arts / DICE, pour le jeu
