# Going Catch You — versão 0.3.0 (primeira que altera o jogo)

Até aqui o plugin só lia. A partir desta versão, **um caçador que fica parado diante de um obstáculo baixo é posto a saltar por cima dele**.

> **Aviso:** é a primeira versão que mexe nos NPCs. Pode haver defeitos visíveis (a deslizar em pose de corrida, a atravessar a geometria, a ficar um pouco enterrado ou a flutuar). Nada disto foi testado em jogo. Se algo correr mal: `bJump=0` no INI volta ao modo só de leitura, e `bJumpDryRun=1` faz o plugin planear e escrever no log o que faria, sem fazer nada.

## A regra

Um NPC é **caçador** quando o motor diz que está em combate e o seu alvo de combate é o jogador. O alvo está em `actor+0x90`, descoberto pela sonda: o handle do jogador aparecia ali em 338 de 410 amostras em combate, e nunca fora de combate.

Um caçador salta quando, ao mesmo tempo:
1. está **parado no chão** há mais de 1,2 s (a velocidade dele, calculada pelas posições, abaixo de 30 u/s) e não é arqueiro;
2. o jogador está a **mais de 200 unidades**;
3. à frente dele, na direção do jogador, há um **obstáculo baixo**: bloqueia o raio à altura dos pés (25) e não o do joelho (70), e está a menos de 120 unidades;
4. o topo do obstáculo mede **até 70 unidades** (um metro, a altura da cintura);
5. há **onde aterrar** depois dele: chão a mais ou menos a mesma altura, nada por cima e corredor livre à altura do arco, sem passar do jogador.

## Como é o salto

Não é um salto físico. A física do NPC é desligada, o controlador dele é movido a cada frame ao longo de um arco curto (meio segundo a pouco mais de um segundo) e a física volta onde ele aterra. São as três ferramentas que o projeto anterior provou em NPCs. Antes de começar é enviado o evento de animação `JumpDirectionalStart` e, no fim, `JumpLand`; se o jogo não os aceitar, o log diz e o NPC desliza em pose de corrida.

Fica tudo protegido: se o salto falhar duas vezes por um erro de memória, desliga-se sozinho para o resto da sessão.

## 1. Compilar e instalar

Apaga a pasta `going-catch-you` do repositório e carrega a deste ZIP (há ficheiros novos: `Jump.cpp/.h`, `Vault.cpp/.h`). Faz o commit diretamente no `main`, corre o workflow e instala `GoingCatchYou.dll` **e** `GoingCatchYou.ini` (há chaves novas) em `Data\SKSE\Plugins\`. A primeira linha do log tem de dizer `0.3.0-jump`.

## 2. Testar

Precisas de um inimigo **parado diante de um obstáculo baixo** com o jogador do outro lado. Uns sítios onde isto acontece: uma vedação ou muro à altura da cintura, uma rocha baixa, um degrau, um tronco caído. Põe-te a mais de 200 unidades (uns 3 metros), a ser visto pelo inimigo, e espera. Se ele der a volta pelo navmesh, repete noutro sítio: o salto só aparece quando ele fica mesmo preso.

Repara no que **vês**: saltou? Com animação de salto ou a deslizar? Atravessou o obstáculo? Ficou enterrado ou a flutuar? Foi atrás de ti depois?

## 3. O que o log mostra

| Linha | Significado |
|---|---|
| `jump: skip <id>: …` | tinha as condições de tempo e distância mas não saltou, e diz porquê (parede, alto demais, sem onde aterrar…) com os números do mundo logo a seguir |
| `jump: plan <id>: …` | planeou um salto: distância ao obstáculo, altura, onde aterra, altura e duração do arco |
| `jump: dry run, <id> stays where it is` | só no modo `bJumpDryRun=1` |
| `jump: begin <id>; start event … -> accepted/not accepted` | começou, e se o jogo aceitou o evento de animação |
| `jump: landed <id> after … s` | chegou ao fim do arco: passos dados, estado do controlador, apoio, evento final |
| `jump: settled <id> (+0.3 s)` | 0,3 s depois: a que distância do ponto de aterragem ficou, e se a **física voltou a ligar** (`physics on`; `STILL OFF` seria um defeito) |
| `jump: aftermath <id> (+2.5 s)` | 2,5 s depois: se ele foi atrás do jogador (mais perto, mais longe) |
| `observer: STOPPED` / `track` | agora mostram também o alvo de combate (`target …`, e `(the player)` se for o jogador) |
| `observer: cell …` | agora diz quantos dos NPCs em combate caçam o jogador |
| `observer: world <id> …` | a linha de visão já está corrigida (antes dava sempre "bloqueada") |

## 4. Limitações

- Só salta obstáculos **baixos** (até 70 unidades). Paredes, desníveis grandes e trepar ficam para depois.
- Só vê a célula onde o jogador está.
- A sonda comparativa está desligada por defeito (`bProbe=0`), porque já cumpriu o que se queria.
- O salto não roda o NPC: ele vai na direção do jogador com a orientação que já tinha.
