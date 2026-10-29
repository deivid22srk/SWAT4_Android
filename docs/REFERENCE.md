# Fase 0 — Estudo da referência `kittycache9/OpenSA2`

Este documento registra o que foi **lido no código** do repositório de referência
(clonado em `/tmp/opencode/OpenSA2`, commit do branch `main`) e separa o que é
específico do jogo da referência do que é reutilizável.

> **Achado principal, e ele contraria o enunciado:** o `OpenSA2` real **não** usa
> `capstone` + `pefile` para discovery/lifting, **não** tem `gen/`, **não** tem
> `ci/gen_stub/`, **não** tem `tools/discover.py`/`tools/lift.py` e **não** tem
> `.github/workflows/build.yml`. O enunciado descreve um pipeline que o OpenSA2
> não implementa. Tudo abaixo é o que existe de fato no repositório.

## 1. O que o OpenSA2 é, de fato

| Evidência | Arquivo |
|---|---|
| "translates the game's x86 machine code into native code at build time" | `README.md` L7 |
| Tradução via **Remill + LLVM**, mapa de funções do **Ghidra** | `README.md` L58–59, `docs/architecture.md` L8–24 |
| Runtime separado do player: o jogo grava um **stream** de gráficos/áudio e um processo player desenha/toca | `docs/architecture.md` L34–49 |
| Renderer padrão em **SDL3 GPU API**; DXVK Native só como referência no Linux | `README.md` L61, `docs/architecture.md` L49 |
| Layout: `runtime/`, `host/` (install, launch, player, d3d9gpu, android), `harness/`, `tools/s2..s8`, `Documentation/`, `docs/` | `find` do clone |
| CI público: só `.github/workflows/source-checks.yml` (checagens de fonte), **não** um `build.yml` de APK | `.github/` |
| Licença **GPL-3.0-only** | `LICENSE`, `README.md` L75 |
| Alvos: Linux x86-64 testado, Windows x86-64 testado, Android arm64 experimental em aparelhos reais | `README.md` L47–53 |

O OpenSA2 é, portanto, um projeto **muito** maior e de arquitetura diferente:
tradução para IR (Remill/LLVM/Ghidra), dois processos com stream, SDL3. Ele não
é um "lifter x86→C com capstone" como o enunciado supõe.

## 2. Específico do jogo da referência (não serve para o SWAT 4)

* Endereços fixos, mapa de funções Ghidra, tweaks (67), saves e nomes de
  arquivos do *Sonic Adventure 2* (2012 PC).
* `host/install/` valida a instalação Steam do SA2; `host/launch/` e
  `host/player/` falam o contrato de stream `host/sa2_stream.h`.
* O renderer `host/d3d9gpu/` traduz o estado D3D9 do SA2 para a GPU API do SDL3.
* Os passes de x87 (`tools/spike/pc24pass/`) e os testes de replay são
  moldados ao executável do SA2.

## 3. Reutilizável (conceitos, não código)

* **Separação build-time × run-time**: o código do jogo é traduzido no build; o
  runtime fornece serviços de SO.
* **Endereços guest preservados** e tabela de despacho para chamadas indiretas.
* **Semântica de falha explícita**: alvo não traduzido vira trap/diagnóstico, não
  "sucesso silencioso" (`docs/architecture.md` L24).
* **Verificação em camadas**: testes de função (estado de máquina), replays,
  testes de renderer, testes de instalador/launcher (`docs/architecture.md`
  L53–65).
* **Distribuição**: repositório público sem código do jogo; binários completos
  só em release, com o usuário fornecendo a cópia original.
* **Android**: app + lifecycle + overlay de toque em volta do mesmo player.

## 4. Decisão tomada para este repositório

O workspace já continha um **scaffold funcional** que corresponde exatamente ao
pipeline *descrito no enunciado* (não ao OpenSA2 real): lifter x86-32 em C com
`capstone`, `gen/`, `ci/gen_stub/`, runtime com shims Win32, camada
D3D9→Vulkan via `dxvk-native`, app Android/SDL2 e `.github/workflows/build.yml`.

Como o OpenSA2 real é um sistema Remill/LLVM/GPL-3.0 com arquitetura de dois
processos e stream, **não é possível "portar o build.yml do OpenSA2"** — ele não
existe — nem reaproveitar o lifter dele sem trocar toda a arquitetura. A decisão
foi:

1. **Reutilizar o scaffold do workspace** (que já implementa o pipeline descrito
   no enunciado) e **re-apontá-lo para o SWAT 4**, reescrevendo tudo que é
   específico de jogo: mapa de módulos/endereços, conjunto de shims, validação
   do launcher, opções, nomes de pacote e workflow.
2. Manter a **honestidade** sobre a origem: este repositório não é um fork do
   OpenSA2 e não alega sê-lo. O OpenSA2 foi estudado e citado; a arquitetura
   usada é a do scaffold pré-existente.
3. Registrar em `docs/TESTED.md` o que foi realmente executado.

## 5. O que isto implica para o SWAT 4

O SWAT 4 é **Unreal Engine 2** com o jogo dividido em ~30 módulos nativos
(`Core.dll`, `Engine.dll`, drivers, DLLs de gameplay) **mais** bytecode de
UnrealScript (`.u`). O caminho do OpenSA2 (Remill/LLVM + mapa Ghidra) seria o
mais adequado a esse volume, mas está fora do escopo do scaffold disponível.
O scaffold com lifter próprio foi validado no código UE2 (ver `docs/RECON.md`):
`Core.dll` → 6.880/6.887 funções convertidas. O que **não** está provado é que o
runtime resultante inicialize a engine e chegue ao menu.