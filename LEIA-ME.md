# Going Catch You — versão 0.2.0 (só lê)

Este passo **não altera nada no jogo**. Serve para responder, de uma vez, a tudo o que o mod precisa de saber antes de começar a mexer nos NPCs. Depois dele, o passo seguinte já é construir o mod.

> **Estado:** a base (carregar, verificar endereços, relógio por frame, ler NPCs) foi testada em jogo nas versões 0.1.0 a 0.1.2. **Tudo o que é novo na 0.2.0 ainda não foi testado**: o combate pelo motor, a leitura do mundo (raycasts), a leitura do controlador e a sonda comparativa. Se a compilação falhar, envia o `build.log`.

## O que mudou em relação à 0.1.2

- **Quem caça o jogador** passa a ser decidido pela chamada do próprio motor `IsInCombat` (a mesma que o GreenTea usa), em vez de "arma desembainhada". Funciona com pessoas, criaturas e cavalos.
- **Leitura do mundo em volta de um caçador parado** (`world`): altura do obstáculo à frente, se o chão desce adiante, e se vê o jogador. São os números que um salto vai precisar.
- **Leitura do controlador** do NPC (velocidade, tempo de queda, apoio), para comparar com a velocidade calculada pelas posições.
- **Sonda comparativa**: compara NPCs em combate com NPCs que não estão, campo a campo, para descobrir onde o motor guarda o alvo de combate. Já não precisas de anotar IDs na consola.
- **Custo**: o log diz quantos ticks por segundo recebe e quanto tempo demora cada amostra.

## 1. Compilar

Substitui a pasta `going-catch-you` do repositório pela deste ZIP (há ficheiros novos: `Havok.h` e `Havok.cpp`, e o `GoingCatchYou.vcxproj` mudou). O workflow não muda. Corre-o em **Actions** e descarrega o artefacto **resultado**.

## 2. Instalar

Copia `GoingCatchYou.dll` e `GoingCatchYou.ini` (de `dist\Data\SKSE\Plugins\`) para `Data\SKSE\Plugins\`. **Troca o INI antigo**: há chaves novas.

## 3. Testar (uns 10 minutos chegam)

1. Carrega um save e **fica perto de NPCs que não estão em combate** (uma cidade serve): a sonda precisa de pelo menos 5 desses.
2. Entra em combate com **inimigos de vários tipos**: um bandido ou outro inimigo corpo a corpo, uma criatura (lobo, urso, aranha), e se puderes um arqueiro. A sonda precisa de pelo menos 5 amostras de NPCs em combate.
3. **Foge para um sítio onde o inimigo não consiga chegar a pé**: em cima de uma rocha, de um muro ou de um telhado. É aí que aparecem as linhas `STOPPED` e `world`.
4. Se o cavalo entrar na luta, melhor.
5. Sai do jogo e envia `Documents\My Games\Skyrim\SKSE\GoingCatchYou.log`.

## 4. O que o log mostra

| Linha | Significado |
|---|---|
| `heartbeat: the first tick arrived` / `observer: the first sample` | o relógio e o jogador estão prontos |
| `observer: cell …` (a cada 5 s) | resumo: NPCs na célula, quantos em combate, se o jogador está em combate, ticks por segundo, e quanto tempo demora uma amostra |
| `observer: + <id> (base <id>) appeared` | um NPC em combate passou a ser observado (`base` é o tipo de NPC; serve para saber se é lobo, bandido, etc.) |
| `observer: track <id> …` | estado a cada 2 s: distância, altura relativa (`dz`), velocidade própria calculada pelas posições, velocidade do controlador (`ctrl`), tempo de queda, apoio, estado de ataque, flags `f1`/`f2` em hexadecimal |
| `observer: STOPPED <id> …` | o próprio NPC parou, longe do jogador, e não é arqueiro |
| `observer: world <id> …` | o que está à frente de um caçador parado: `[25:140(L1) 70:140(L1) 120:clear 170:clear]` é a distância a que cada raio (a 25, 70, 120 e 170 unidades de altura) bate e em que camada; `-> obstacle, top between 70 and 120` é a conclusão; `ground ahead [60:-5 140:-300]` é a altura do chão 60 e 140 unidades adiante em relação aos pés (negativo é uma queda); `line of sight` diz se ele vê o jogador |
| `observer: probe: … fields that tell them apart` (a cada 30 s) | os campos que separam NPCs em combate dos que não estão. Se o valor for o handle do jogador, a linha diz-o |
| `observer: - <id> left` | deixou de ser observado |

## 5. Limitações conhecidas

- **`IsInCombat` diz que o NPC está em combate, não contra quem.** A sonda procura o campo do alvo de combate; se não achar, o passo seguinte decide com a distância e a visão.
- **Só vê a célula onde o jogador está.** Num exterior, um inimigo na célula ao lado não aparece.
- **Arqueiros ficam fora dos `STOPPED`**: os estados de ataque 7 a 15 são o ciclo do arco e aparecem como `(ranged)`.
- **O tipo de NPC só aparece como `base <id>`**, não como nome. Se quiseres saber qual é, abre a consola e escreve o id.
- **A velocidade e as flags de movimento que o motor dá não são fiáveis** nesta edição; por isso a velocidade é calculada pelas posições.

## 6. Depois deste passo

O passo seguinte já é construir: com a classificação, o obstáculo e o apoio lidos, os caçadores parados passam a saltar o que os trava.
