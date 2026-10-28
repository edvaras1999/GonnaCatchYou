# Going Catch You — versão 0.4.0 (só lê, com explorador)

Esta versão **não altera nada no jogo**: o salto e as teclas de teste vêm desligados. Serve para descobrir, numa só ida ao jogo, o que falta para o mod usar o **caminho que a IA do jogo calcula para cada NPC** (o que o FluidMotion faz na SE) e perceber **porque os NPCs ficam parados a encarar-te**.

## O que descobrimos antes de escrever isto

O `TESV.exe` que enviaste é exatamente o que o plugin já usa, e tem RTTI completo (6224 classes). Por isso o plugin consegue dar **nome** a qualquer objeto na memória a partir da sua vtable. A IA de combate da LE tem classes para tudo o que queremos: `CombatPath`, `CombatPathDestination`, `CombatPathingRequestStandard`, uma família de políticas de objetivo (`FindTargetLocation`, `FindAttackLocation`, `Flank`, `Search`, `SearchWander`, `Retreat`, `ReturnToCombatArea`), `CombatTargetSelector`, `CombatApproachTargetSpeedController` e `CombatAreaHoldPosition`. O andar dos NPCs vive no `MovementControllerNPC` (não há na LE o agente de seguir caminhos da SE, por isso os offsets do FluidMotion não servem).

## O que o explorador faz

1. **Mapa de objetos nomeados.** Para cada caçador (e para uns 40 NPCs sem combate, para comparar) percorre ator → processos → objetos apontados, até 5 níveis, e dá nome a cada um. As linhas `explore: new in a hunter: <caminho> <classe>` mostram o que um caçador tem.
2. **Procura de caminhos.** Procura sequências de posições (x, y, z) com ar de percurso, com qualquer espaçamento entre nós, e diz onde estão, quantos nós têm e a que distância ficam do NPC e do jogador. Um percurso que acaba perto de ti é um caminho completo; um que acaba longe é um caminho que não chega.
3. **Correlação de campos.** Regista que campos dos objetos de movimento e combate mudam quando o NPC anda, para, se aproxima ou se afasta, e aponta os que separam os casos.
4. **Resumo a cada 30 s** (`explore: summary`), com o que só os caçadores têm, onde há sequências de posições e que campos separam andar de parar.
5. **Despejo:** **Shift+Insert** escreve no log, nesse momento, todos os objetos de cada caçador perto (até 4) e do jogador, com os primeiros 16 valores de cada objeto e os nós dos caminhos encontrados. Usa-o quando vires um NPC a dar a volta ou parado a encarar-te.

O explorador foi testado aqui contra a imagem real do `TESV.exe` (nomes das vtables, procura de um caminho de 6 nós inventado, correlação de um campo de estado). Isso valida a lógica; não valida o que acontece dentro do jogo.

## 1. Compilar e instalar

Apaga a pasta `going-catch-you` do repositório e carrega a deste ZIP (ficheiro novo: `Explorer.cpp/.h`; o `.vcxproj` mudou). Faz o commit diretamente no `main`, corre o workflow e instala `GoingCatchYou.dll` **e** `GoingCatchYou.ini` (há chaves novas). A primeira linha do log tem de dizer `0.4.0-explore`.

## 2. Testar (uns 15 minutos)

1. Carrega um save e **fica perto de NPCs sem combate** (uma cidade): o explorador precisa de uns 40 para comparar.
2. Entra em combate com **vários inimigos**, em sítios onde dêem voltas: terreno com muros, rochas, desníveis.
3. **Põe-te num sítio onde não te alcancem** (em cima de uma rocha ou de um muro) e espera que alguns fiquem parados a encarar-te.
4. Nos momentos interessantes (um a dar a volta, um parado a encarar-te, um que passa para o outro lado), carrega **Shift+Insert** uma vez. Três ou quatro vezes chegam.
5. Sai do jogo e envia o log.

## 3. Linhas do log

| Linha | Significado |
|---|---|
| `explore: new in a hunter: <caminho> <classe>` | um objeto que um caçador tem, e onde está (`a` é o ator, `a>88` o processo, `a>88>4` o processo médio, `a>88>8` o alto). Só as primeiras 180 |
| `explore: path? <id> <onde> +<offset> stride <n>` | uma sequência que pode ser um caminho: quantos nós, o comprimento, e as distâncias do primeiro e do último ao NPC e ao jogador |
| `explore: summary` | a cada 30 s: objetos que só os caçadores têm; sítios onde há sequências de posições; campos que separam andar de parar e aproximar-se de afastar-se |
| `dump: …` | o despejo do Shift+Insert, objeto a objeto |
| `observer: …` | tudo o que já havia (`track`, `STOPPED`, `DETOUR`, `world`) |

## 4. Limitações

- Lê memória por ponteiros, protegida por verificações e por um tratador de exceções; se falhar 3 vezes o explorador desliga-se e o log diz.
- Só vê a célula onde o jogador está.
- A procura de caminhos é heurística: pode dar falsos positivos (matrizes, vetores) e perder caminhos guardados de outra forma. O despejo e a correlação ajudam a decidir.
- O salto e as teclas de teste continuam no código, desligados (`bJump=0`, `bManualKeys=0`); a sonda antiga também (`bProbe=0`).
