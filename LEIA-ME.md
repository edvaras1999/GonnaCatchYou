# Going Catch You — passo 1 (só observa)

Este passo **não altera nada no jogo**. Serve para confirmar três coisas antes de qualquer movimento:

1. o plugin carrega na LE 1.9.32.0.8 e os endereços batem certo;
2. o "relógio" por frame funciona;
3. dá para ver os NPCs que andam atrás do jogador e perceber onde ficam presos.

> **Aviso honesto:** este código foi escrito e verificado só na sintaxe, **não foi compilado com o SKSE real nem testado em jogo**. Pode haver erros de compilação (ver o ponto 5). Se houver, envia o `build.log`.

## 1. Compilar (GitHub)

1. Põe o conteúdo desta pasta num repositório teu (incluindo a pasta oculta `.github`).
2. Aba **Actions** → **Compilar Going Catch You (passo 1: observar)** → **Run workflow**.
3. Quando acabar, descarrega o artefacto **resultado**: tem `GoingCatchYou.dll` e `build.log`.
4. Se falhar, envia o `build.log` (ou as primeiras linhas com `error`).

O workflow descarrega o SKSE 1.7.3. Se o download falhar, o plano B é o mesmo de antes: pôr `skse_1_07_03.7z` em `third_party/`.

## 2. Instalar

Copia para `Data\SKSE\Plugins\`:

- `GoingCatchYou.dll` (do artefacto)
- `GoingCatchYou.ini` (de `dist\Data\SKSE\Plugins\`)

O plugin é independente: não precisa do FreeRun. Para o log sair limpo, não uses ao mesmo tempo o módulo de teste de NPCs do projeto anterior (o `FreerunLE.dll` com `[Npc]`), porque ele também mexe nos NPCs.

## 3. Testar

1. **Carrega um save.** O relógio é o `Update` da câmara (primeira ou terceira pessoa), por isso só bate com o jogo a correr: não bate no menu principal, nem a cavalo ou num dragão (câmaras próprias).
2. Provoca perseguições: inimigos hostis com arma desembainhada a ir atrás de ti; depois sobe a um sítio alto ou atrás de um obstáculo onde eles não cheguem a pé, e espera uns segundos.
3. Uns 5 a 10 minutos chegam. Sai do jogo.
4. Envia `Documents\My Games\Skyrim\SKSE\GoingCatchYou.log`.

## 4. O que o log mostra

| Linha | Significado |
|---|---|
| `heartbeat: the first tick arrived` | o relógio funciona (sem esta linha, nada mais vai aparecer) |
| `observer: the first sample` | o jogador está pronto e o plugin começou a olhar à volta |
| `observer: ticks arrive, but the player is not ready` | o relógio bate mas o jogador não passa a verificação de leitura; diz-me o que esta linha mostra |
| `verify: … sites - 0 wrong` | os endereços batem com o executável |
| `observer: cell …: N references, M NPCs, K within …, W watched` | resumo a cada 5 s |
| `observer: + <id> appeared` / `- <id> left` | um NPC começou / deixou de ser observado |
| `observer: track <id> …` | estado de cada NPC a cada 2 s: distância, dz (diferença de altura), velocidade de aproximação (`closing`), velocidade própria do NPC e do jogador (calculadas a partir das posições), estado no chão ou no ar, e as palavras de flags do motor em hexadecimal (`f1`, `f2`) |
| `observer: STOPPED <id> …` | o próprio NPC parou (velocidade dele abaixo de `fStoppedSpeed`) estando longe do jogador |
| `observer: moving again <id> …` | voltou a andar |

Os ids são formIDs: com a consola aberta, clica num NPC e vês o id dele.

## 5. Limitações conhecidas

- **"Em combate com o jogador" é uma aproximação.** O endereço do alvo de combate ainda não foi encontrado nesta edição. Um NPC é observado se estiver vivo, perto e com **arma desembainhada**. Seguidores em combate também entram; caçadores com a arma guardada não. `bAllActors=1` observa todos, para comparar.
- **Só vê a célula onde o jogador está.** Num exterior, um inimigo na célula ao lado não aparece.
- **Arqueiros, magos e seguidores parados dão `STOPPED` normalmente.** O estado de ataque (`attack`) e as flags (`f1`, `f2`) vêm em cada linha para os filtrar mais tarde.
- **A velocidade e as flags de movimento que o motor dá não são fiáveis nesta edição** (a primeira versão mostrava sempre o mesmo texto de movimento e velocidades sem sentido). Por isso a velocidade passou a ser calculada a partir das posições.
- **Ponto de risco na compilação:** a lista de referências da célula (`cell->objectList`) vem dos cabeçalhos do SKSE 1.7.3 e foi escrita de memória. Se o nome ou o `#include "skse/GameForms.h"` não existir, o erro aponta para `Observer.cpp` ou `Engine.h`, e a correção é de uma linha.

## 6. Passos seguintes

2. **Salto:** os NPCs parados (`STOPPED`) passam o que os trava.
3. **Trepar:** os que não saltam sobem com animações, opcionalmente as do FreeRun se estiver instalado.
