# Going Catch You — versão 0.5.1 (a perseguição, corrigida)

Um caçador que **não se aproxima de ti** passa a **saltar na tua direção**, em vez de ficar parado a encarar-te ou a dar a volta. **Esta versão altera os NPCs.**

> **Estado:** a mecânica do salto funciona (confirmado na 0.5.0: 25 aterragens, todos no chão). A 0.5.1 corrige três problemas dessa versão (ver abaixo). Ainda não foi testada em jogo. Se algo correr mal, `bJump=0` no INI volta ao modo só de leitura, `bJumpDryRun=1` planeia e escreve no log sem fazer nada, e `bManualKeys=0` desliga a tecla de teste.

## O que mudou na 0.5.1 (em relação à 0.5.0)

1. **Gatilho "detour" retirado do salto.** Na 0.5.0, um NPC a lutar corpo a corpo que dava meia volta ou se esquivava disparava o gatilho de desvio e era mandado saltar. Resultado: saltava quando não estava preso, às vezes por trás do jogador. Agora o salto só dispara com **"standing"**: parado, sem se aproximar, e a ver o jogador.
2. **Cooldown duplo após resgate.** Na 0.5.0, um NPC resgatado voltava a tentar imediatamente e caía de novo. Agora aguarda o dobro do cooldown normal (8 s em vez de 4 s).
3. **Distância mínima de 150 unidades.** Um NPC em combate corpo a corpo pode estar a 30 unidades; não faz sentido mandá-lo saltar 500 unidades.

## O que está na 0.5.0 e se mantém

O que falhou na 0.3.1 foi o salto simples da tecla: aterrou praticamente sobre o jogador, o NPC foi empurrado através do chão e morreu. Também flutuava. Agora:

1. **Voo como o do FluidMotion.** A cada frame: o NPC é posto onde o arco diz, o controlador é marcado **"no ar"** (o jogo toca a pose de queda), recebe a **velocidade** do arco, e a **altura de início de queda** é mantida no ponto atual, para a aterragem **não dar dano de queda**. A física **não** é desligada.
2. **Aterragem escolhida, nunca à sorte.** O planeador testa pontos entre o NPC e ti e só aceita um com **chão sólido** (estático, terreno), **plano**, **ao nível do jogador**, ao alcance (sobe até 180, desce até 420), **sem nada por cima**, **ninguém lá**, a **90 ou mais do jogador**, e o arco é **varrido** (pés, tronco e cabeça) à procura de obstáculos. Sem ponto seguro, não salta, e o log diz porquê, com contagens.
3. **Resgate.** Depois da aterragem, o NPC é vigiado durante 1,8 s; se estiver 90 abaixo do ponto previsto, ou a cair há quase um segundo, é **posto de volta onde estava**.
4. **Quando salta.** Um caçador (em combate, com o jogador como alvo) salta se estiver **parado há 1,5 s e a ver-te** (`standing`), ou **a andar há 2 s com o rumo a mais de 60° da linha reta** (`detour`), com o jogador a mais de 220 unidades e depois de 3,5 s desde a última tentativa. Longe de ti, vai aos poucos: cada salto leva-o até 520 unidades.

O explorador e a sonda estão **desligados** por defeito: já cumpriram a missão e eram pesados.

## Tecla de teste

**Seta para cima** faz o inimigo em combate mais perto de ti saltar já, sem as condições de tempo e de visão (usa o mesmo planeador, por isso continua a recusar aterragens inseguras). Só funciona com o jogo em primeiro plano; não a uses com a consola ou um menu abertos. A seta para baixo (vault do FreeRun) está desligada.

## 1. Compilar e instalar

Substitui a pasta `going-catch-you` do repositório pela deste ZIP (o `.vcxproj` não mudou). Faz o commit diretamente no `main`, corre o workflow e instala `GoingCatchYou.dll` **e** `GoingCatchYou.ini` (há chaves novas). A primeira linha do log tem de dizer `0.5.0-leap`.

## 2. Testar

1. Põe-te num sítio com inimigos que costumem ficar parados à distância ou dar a volta: em cima de uma rocha, de um muro, do outro lado de uma vedação, de um desnível.
2. **Repara no que vês:** a pose durante o voo (queda ou a deslizar?), a aterragem (suave, enterrado, a flutuar, a girar), e se vão atrás de ti depois.
3. Pode usar a **seta para cima** num inimigo em combate para ver já um salto.
4. Envia o log.

## 3. O que o log mostra

| Linha | Significado |
|---|---|
| `jump: skip <id> (standing/detour): …` | tinha o gatilho mas não saltou, e porquê: sem linha de visão, ou **sem aterragem segura** com a contagem dos motivos (sem chão, chão de outro tipo, não nivelado com o jogador, fora de alcance, demasiado perto do jogador, não plano, algo por cima, gente lá, demasiado rápido, arco bloqueado) |
| `jump: plan <id> (…): lands … ahead, … from the player, ground … ; … u/s along, … u/s up, … s` | planeou um salto |
| `jump: begin / landed <id>` | o salto começou e acabou |
| `jump: settled <id> (+0.3 s)` | 0,3 s depois: a que distância do ponto previsto ficou, estado do controlador, apoio, tempo de queda |
| `jump: RESCUED <id>: …` | o NPC foi posto de volta onde estava (e porquê) |
| `jump: aftermath <id> (+2.5 s)` | se foi atrás de ti (mais perto, mais longe) e se foi resgatado |
| `manual: LEAP on <id>` | a tecla de teste |
| `observer: DETOUR <id>` | um caçador anda há 2 s sem ir direto a ti |

## 4. Limitações

- **Não conhece o navmesh.** O gatilho é comportamental: um caçador que está só à espera, ou que tem um caminho curto a que o jogo ainda não deu importância, também pode ser mandado saltar.
- **Ainda não lê o caminho do NPC** (o explorador não achou os pontos do percurso), por isso não sabe se o caminho normal resolveria.
- O NPC **não é rodado**: salta com a orientação que já tinha.
- Só vê a célula onde o jogador está.
- A altura de início de queda é escrita em unidades do jogo (como o projeto FreeRun faz); se o dano de queda aparecer na aterragem, o log mostra-o.
