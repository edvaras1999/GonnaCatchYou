# Going Catch You — versão 0.3.1

Faz **caçadores saltarem obstáculos baixos** e traz **duas teclas de teste** para ver o salto sem esperar que um NPC fique preso.

> **Aviso:** altera os NPCs. Pode haver defeitos visíveis (a deslizar em pose de corrida, a atravessar a geometria, enterrado ou a flutuar). **Nada disto foi testado em jogo**: o salto nunca chegou a correr na 0.3.0. Se algo correr mal: `bJump=0` no INI volta ao modo só de leitura; `bJumpDryRun=1` planeia e escreve no log, sem fazer nada; `bManualKeys=0` desliga as teclas.

## As teclas de teste

| Tecla | O que faz |
|---|---|
| **Seta para cima** | o **arco do plugin**: o NPC em combate mais perto de ti salta na tua direção. Se há um obstáculo baixo à frente (até 110 de altura, a menos de 200 unidades) salta por cima dele; senão faz um salto simples de 160 unidades |
| **Seta para baixo** | a **animação de vault do FreeRun**: o mesmo NPC, se estiver a menos de 110 unidades de um obstáculo com 42 a 120 de altura, virado para ele |

As duas ignoram todas as condições do salto automático (ser caçador, estar parado, a distância ao jogador). Escolhem o NPC em combate mais perto de ti, dando preferência a quem te tem como alvo, até 2500 unidades. Só funcionam com a janela do jogo em primeiro plano, e **não as uses com a consola ou um menu abertos**.

Para o teste do arco basta um inimigo em combate perto de ti em terreno plano. Para o do FreeRun é preciso um inimigo **mesmo encostado a um obstáculo e virado para ele**; se não for o caso o log diz porquê. O FreeRun usa o patch de behavior que já tens instalado; se o grafo do NPC não reagir, o log diz `did not start` e que provavelmente o patch não está nesse grafo.

## O que mudou em relação à 0.3.0

1. **Os raios ignoram personagens.** Antes, o corpo do jogador a 200 unidades aparecia como "parede".
2. **O limite de altura subiu para 110** (era 70; os obstáculos reais do log tinham 87 e 110).
3. **Gatilho novo, o desvio.** Além de "parado", um caçador que **anda mas com o rumo a mais de 60° da linha reta para o jogador, durante 2 s**, e tem um obstáculo saltável nessa linha, salta em vez de dar a volta. A linha `observer: DETOUR` regista cada desvio, mesmo quando não há obstáculo.

## O salto automático, resumido

Um **caçador** (em combate, com o jogador como alvo em `actor+0x90`) salta quando está parado há mais de 1,2 s **ou** em desvio há mais de 2 s, o jogador está a mais de 200 unidades, e há um obstáculo baixo a menos de 120 unidades à frente, com onde aterrar do outro lado. Não é um salto físico: a física do NPC é desligada, o controlador dele é movido num arco curto e a física volta onde ele aterra.

## 1. Compilar e instalar

Apaga a pasta `going-catch-you` do repositório e carrega a deste ZIP (há ficheiros novos: `FreeRun.*` e `Manual.*`; o `.vcxproj` mudou). Faz o commit diretamente no `main`, corre o workflow e instala `GoingCatchYou.dll` **e** `GoingCatchYou.ini` (há chaves novas). A primeira linha do log tem de dizer `0.3.1-jump`.

## 2. O que o log mostra

| Linha | Significado |
|---|---|
| `manual: the arc key was pressed` / `…FreeRun key…` | a tecla foi apanhada |
| `manual: ARC on <id>: …` / `manual: FREERUN on <id>: …` | o que foi escolhido para o NPC e porquê; se não há NPC em combate ou o NPC está perto de mais, diz-o |
| `jump: begin / landed / settled / aftermath` | o arco: se o jogo aceitou os eventos de animação, quando acabou, a que distância do ponto previsto ficou, se a **física voltou a ligar** (`STILL OFF` seria um defeito), e se ele foi atrás de ti |
| `freerun: before … / begin / started / finished / aftermath` | o vault do FreeRun: o que o grafo dizia antes, se aceitou o evento, se arrancou (`SkyParkourOngoing`), quanto se moveu |
| `observer: DETOUR <id>` | um caçador anda há 2 s sem ir direto ao jogador |
| `jump: skip <id> (stopped/detour): …` | tinha o gatilho mas não saltou, e porquê, com os números do mundo |
| `jump: plan <id> (stopped/detour): …` | planeou um salto automático |

## 3. Limitações

- O arco só salta obstáculos **baixos** (até 110). Paredes e trepar ficam para depois.
- O salto não roda o NPC: ele vai na direção do jogador com a orientação que já tinha.
- O vault do FreeRun é só o movimento por cima de um obstáculo, **sem** o que o projeto antigo fazia à volta (levar o NPC a um ponto de partida e corrigir a aterragem), porque foi aí que apareceram os defeitos.
- Só vê a célula onde o jogador está.
