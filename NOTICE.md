# Créditos e licença

O Going Catch You é um plugin novo e independente. Não altera o FreeRun e não precisa dele.

Parte do código vem de projetos anteriores, e por isso o repositório mantém a licença **AGPL-3.0** (ver `LICENSE`) até cada ficheiro ser revisto:

- **`Engine.h`, `Engine.cpp`**: vêm do projeto FreeClimb LE (FreeClimbe-Beta) através do projeto FreeRun NPC, reduzidos ao que é só leitura. A chamada `IsInCombat` (slot 0xE2 da vtable do ator) foi lida do `GreenTea.dll` (Felisky), que a usa tal e qual; só se aprendeu **qual** é a chamada, nenhum código dele foi copiado. Cada endereço e offset foi lido do executável 1.9.32.0.8 e testado em jogo nesses projetos.
- **`Verify.cpp`, `Verify.h`, `Signatures.h`**: do projeto FreeRun NPC. `Signatures.h` tem apenas os endereços que este plugin usa.
- **`Tick.cpp`**: o relógio por frame é um gancho no `Update` dos estados de câmara em primeira e terceira pessoa, com os mesmos endereços e a mesma técnica que o `Camera.cpp` do **FreeRun LE** (Mhacks79, AGPL-3.0), que por sua vez é um port do **SkyParkour NG** (Tsptds, AGPL-3.0).
- **`Havok.h`, `Havok.cpp`**: os raycasts e shape casts contra o mundo físico, do projeto FreeRun NPC (que os trouxe do FreeClimb LE), sem alterações de lógica.
- **`FreeRun.cpp`**: o protocolo de eventos e variáveis do grafo (`SkyParkour`, `SkyParkourLedge`, `SkyParkourOngoing`, `bAnimationDriven`) é o do patch de behavior do **FreeRun LE** (Mhacks79, AGPL-3.0), tal como o projeto FreeRun NPC o usou. Não precisa da DLL do FreeRun, só do patch de behavior; sem ele, o log diz que o grafo não reagiu.
- **`Jump.*`, `Vault.*`, `Manual.*`**: escritos de raiz para este projeto, sobre as ferramentas de movimento que o projeto FreeRun NPC provou em NPCs.
- **`Observer.cpp`, `Settings.*`, `main.cpp`**: escritos de raiz para este projeto.

Como referência de comportamento (não como código), o projeto olha para o **FluidMotionNPC** (megakuutti, fork de Tony Torres / tonycubed2, GPL-3.0). Nada do código dele está neste passo. Qualquer parte que venha a ser portada nos passos seguintes mantém a licença GPL-3.0 e os créditos.

O SKSE 1.7.3 (Silverlock) é descarregado pelo workflow para compilar e não é redistribuído aqui.
