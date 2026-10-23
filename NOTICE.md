# Créditos e licença

O Going Catch You é um plugin novo e independente. Não altera o FreeRun e não precisa dele.

Parte do código vem de projetos anteriores, e por isso o repositório mantém a licença **AGPL-3.0** (ver `LICENSE`) até cada ficheiro ser revisto:

- **`Engine.h`, `Engine.cpp`**: vêm do projeto FreeClimb LE (FreeClimbe-Beta) através do projeto FreeRun NPC, reduzidos ao que é só leitura. Cada endereço e offset foi lido do executável 1.9.32.0.8 e testado em jogo nesses projetos.
- **`Verify.cpp`, `Verify.h`, `Signatures.h`**: do projeto FreeRun NPC. `Signatures.h` tem apenas os endereços que este plugin usa.
- **`Tick.cpp`**: o relógio por frame é um gancho no `Update` dos estados de câmara em primeira e terceira pessoa, com os mesmos endereços e a mesma técnica que o `Camera.cpp` do **FreeRun LE** (Mhacks79, AGPL-3.0), que por sua vez é um port do **SkyParkour NG** (Tsptds, AGPL-3.0).
- **`Observer.cpp`, `Settings.*`, `main.cpp`**: escritos de raiz para este projeto.

Como referência de comportamento (não como código), o projeto olha para o **FluidMotionNPC** (megakuutti, fork de Tony Torres / tonycubed2, GPL-3.0). Nada do código dele está neste passo. Qualquer parte que venha a ser portada nos passos seguintes mantém a licença GPL-3.0 e os créditos.

O SKSE 1.7.3 (Silverlock) é descarregado pelo workflow para compilar e não é redistribuído aqui.
