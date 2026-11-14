# OpenBF1942-Plus

<p align="center"><img src="docs/banner.svg" alt="OpenBF1942-Plus" width="100%"></p>

![Status: early prototype](https://img.shields.io/badge/status-early%20prototype-orange)
![License: AGPL-3.0](https://img.shields.io/badge/license-AGPL--3.0-blue)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-lightgrey)
![C++17 | OpenGL 3.3](https://img.shields.io/badge/C%2B%2B17-OpenGL%203.3-lightgrey)
![Battlefield 1942](https://img.shields.io/badge/Battlefield-1942-8a7a52)

[Español](README.es.md) · [English](README.md) · **Português** · [Français](README.fr.md) · [Deutsch](README.de.md) · [Русский](README.ru.md)

**Battlefield 1942 em um motor novo.** OpenBF1942-Plus é uma reimplementação clean-room do Refractor 1, o motor do Battlefield 1942, escrita do zero: ela lê os arquivos do próprio jogo a partir da sua instalação e os executa em sistemas modernos. O objetivo é jogar com e contra o jogo original: clientes originais no nosso servidor, nosso cliente em servidores originais.

<table>
  <tr>
    <td><img src="docs/images/first_person.jpg" alt="El Alamein em primeira pessoa com a BAR"></td>
    <td><img src="docs/images/third_person.jpg" alt="Um soldado britânico em terceira pessoa em El Alamein"></td>
  </tr>
  <tr>
    <td colspan="2"><img src="docs/images/berlin.jpg" alt="Uma rua de Berlim com ouriços antitanque"></td>
  </tr>
</table>
<p align="center"><sub>El Alamein e Berlim, desenhados pelo OpenBF1942-Plus a partir dos arquivos do jogo original.</sub></p>

## O que funciona hoje

- **Todos os mapas de Battlefield 1942, Road to Rome e Secret Weapons of WWII carregam e são renderizados**: terreno, construções, árvores, água, céu, neblina e iluminação conforme as configurações de cada mapa.
- **Uma rodada de conquista contra bots** em qualquer mapa: bandeiras, tickets, kits, carregadores e recargas, granadas, minas e cargas explosivas, depósitos de suprimentos, agachar e deitar, paraquedas, primeira e terceira pessoa.
- **Veículos**: carros, tanques, aviões e barcos que você pode pilotar, com assentos para artilheiros e passageiros; os bots dirigem e atiram com veículos terrestres e lanchas de desembarque.
- **Som e HUD** a partir dos scripts de som e das fontes do jogo.
- **Comparado com o cliente original** nos mesmos pontos de Berlim e El Alamein: construções, névoa, céu, veículos e areia dentro de cerca de 15 %.
- **Pesquisa de rede**: uma sessão cliente/servidor original gravada; o protocolo de consulta do servidor documentado.

> [!NOTE]
> **Protótipo inicial.** Só pequenas partes foram testadas por uma pessoa, muitas regras são suposições e nada está calibrado. Ainda não há menus nem multijogador, e as ruas de Berlim continuam mais escuras que no original.

> [!IMPORTANT]
> **Nenhum conteúdo do jogo está incluído: traga seus próprios dados.** Este repositório é só código-fonte: sem executáveis, arquivos compactados, mapas, modelos, texturas ou sons. O motor os lê de uma cópia do Battlefield 1942 que **você possui legalmente**, chamada aqui de `<BF1942_DIR>`.

## Como jogar

Ainda não há builds prontas para usar: compile o motor (veja [Compilação](#compilação)) e aponte-o para a sua pasta do Battlefield 1942, a que contém `Mods/`:

```sh
build/obf_mapview "<BF1942_DIR>" bf1942 El_Alamein --play 2      # conquista no time 2, kit de assalto
build/obf_mapview "<BF1942_DIR>" bf1942 Wake                     # câmera livre
build/obf_mapview "<BF1942_DIR>" bf1942 El_Alamein --drive Willy  # um veículo na frente da câmera
build/obf_sim "<BF1942_DIR>" bf1942 El_Alamein --play 2 --sim 600 # sem janela: 10 minutos de conquista, em texto
```

| Controles | |
|---|---|
| Mover, olhar | WASD, mouse (capturado; F10 libera), shift anda, espaço pula (caindo: abre o paraquedas) |
| Armas | botão esquerdo atira, R recarrega, 1–9 armas do kit, G detona as cargas explosivas |
| Postura, visão | ctrl esquerdo agacha, Z deita, V primeira / terceira pessoa |
| Veículos | E entra / sai, 1–8 troca de assento, WASD dirige, o mouse mira as torres |
| Rádio | F1–F7 abre um menu, depois F1–F8 envia uma mensagem |
| Aviões | W / S acelerador, setas arfagem e rolagem, A / D leme |
| Câmera livre | botão direito olha, WASD / QE movem, shift rápido; C mostra as malhas de colisão; Esc sai |

## Roteiro

A ordem, com os motivos e os critérios de saída, está em [docs/roadmap/roadmap.md](docs/roadmap/roadmap.md):

1. **Consolidar** (feito): o jogo como biblioteca, um renderizador e executáveis enxutos; CI.
2. **Pesquisa do protocolo de rede** (uma sessão gravada): decodificar o tráfego entre cliente e servidor originais, em paralelo com as etapas abaixo.
3. **Navegação da IA**, **áudio**, **HUD** (primeiras partes feitas).
4. **Soldados e armas completos** (quase feito), **tripulações de veículos** (terra e mar feitos, pilotos pendentes), **efeitos**.
5. **Fidelidade**: medir o original e calibrar.
6. **Modos de jogo**, **menus**.
7. **Rede**: servidor e cliente próprios, depois jogar junto com o original.
8. **Renderização**, **distribuição** (builds para Windows e Linux, widescreen, mods).

## Para colaboradores

Todo tipo de ajuda conta: testar e relatar o que está errado, comparar com o jogo original, ou código. A documentação técnica, em inglês, começa em [docs/README.md](docs/README.md): formatos de arquivo, decisões de arquitetura e dívida conhecida. [AGENTS.md](AGENTS.md) é o ponto de entrada para agentes de programação com IA.

## Compilação

C++17, CMake, SDL2, OpenGL 3.3 (libepoxy) e Jolt Physics, que o CMake baixa na primeira configuração. No Debian ou Ubuntu:

```sh
sudo apt install cmake g++ git libsdl2-dev libepoxy-dev
cmake -S . -B build && cmake --build build -j
ctest --test-dir build
# -> build/obf_mapview (janela), build/obf_sim (sem janela)
```

<details>
<summary><b>Ferramentas em Python</b></summary>

Leitores dos formatos do jogo, só com a biblioteca padrão do Python 3.

```sh
python3 tools/rfa.py list    "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa"
python3 tools/rfa.py extract "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa" out/
python3 tools/con.py "<BF1942_DIR>" bf1942 Wake    # carregar um mapa, imprimir um resumo
python3 tools/sm.py "<BF1942_DIR>" bf1942 standardMesh/willy_hul_m1 out/jeep   # OBJ + texturas
python3 -m unittest discover tools     # testes
python3 tools/check_docs.py            # verificações da documentação
```

</details>

<details>
<summary><b>O que fica onde</b></summary>

| Caminho | Conteúdo |
|---|---|
| `engine` | O motor em C++: arquivos compactados, scripts, malhas, terreno, animação, física, regras de jogo, IA, áudio, HUD, renderizador e os executáveis |
| `tools` | Leitores dos formatos em Python, o verificador de documentação e seus testes |
| `docs/formats` | As especificações dos formatos de arquivo e de rede |
| `docs/architecture` | Camadas, fronteiras e as decisões de arquitetura (ADRs) |
| `docs/roadmap` | O roteiro e a dívida conhecida |
| `docs/ai` | A camada de memória para agentes de IA |

</details>

## Transparência

- **Quem mantém:** OpenGamePlus. Gosto de jogos antigos e quero continuar jogando Battlefield 1942 com amigos nos sistemas de hoje, com um motor aberto que a comunidade possa manter vivo.
- **Como o trabalho é feito:** trabalho com o [Claude Code](https://claude.com/claude-code), o assistente de programação da Anthropic. O código e a documentação, incluindo este README e suas traduções, são escritos com a ajuda dele. Os commits feitos assim levam uma linha `Co-Authored-By: Claude` na mensagem, para que possam ser identificados no histórico com `git log`.
- **Clean room:** nenhum código do motor original é usado ou descompilado. O motor é escrito a partir dos arquivos de dados do jogo e da observação do jogo original em execução.
- **O que não é:** não é um projeto oficial e não é afiliado à Electronic Arts nem à DICE. Não contém arquivos do jogo.

## Aviso

Esta é uma reimplementação independente do motor Refractor 1. Ela não é afiliada, autorizada, patrocinada nem endossada pela Electronic Arts Inc., EA Digital Illusions CE (DICE) ou qualquer de suas afiliadas.

"Battlefield" e "Battlefield 1942" são marcas registradas da Electronic Arts Inc. Elas são usadas neste repositório apenas para identificação e interoperabilidade, para descrever o que o motor executa. Nenhum direito sobre essas marcas é reivindicado.

Este projeto não fornece nem ajuda a obter dados do jogo protegidos por direitos autorais. Para usá-lo com conteúdo real do jogo, você deve fornecer os arquivos de dados de uma cópia do Battlefield 1942 que possua legalmente.

Se você é titular de direitos e acredita que algo aqui não deveria ser distribuído, abra uma issue ou entre em contato com o mantenedor e isso será tratado prontamente.

As capturas de tela mostram conteúdo do Battlefield 1942, propriedade da Electronic Arts Inc., apenas para identificação.

## Licença

[GNU Affero General Public License v3.0](LICENSE). Quem distribuir o motor, ou executar uma versão modificada como servidor de rede para outras pessoas, deve oferecer o código-fonte sob a mesma licença. As bibliotecas de terceiros mantêm as suas: SDL2 (zlib), libepoxy (MIT) e Jolt Physics (MIT).

## Créditos

- [Jolt Physics](https://github.com/jrouwe/JoltPhysics), [SDL](https://www.libsdl.org/) e [libepoxy](https://github.com/anholt/libepoxy)
- Electronic Arts / DICE, pelo jogo
