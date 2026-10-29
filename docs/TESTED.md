# O que foi realmente executado

Este arquivo registra cada comando executado e o resultado. Se algo não está
aqui, **não foi testado**. Nenhuma afirmação do README é mais forte que este
arquivo.

Ambiente: Linux x86-64, 4 vCPU, 15 GB de RAM, sem GPU, **sem aparelho Android**,
sem SDK/NDK/Gradle instalados localmente (o APK é compilado só no GitHub
Actions, por decisão de projeto).

## 2026-10-29

### Reconhecimento e ferramentas

| # | O que | Comando | Resultado |
|---|---|---|---|
| 1 | Clonar a referência | `git clone --depth 1 kittycache9/OpenSA2` | ok; arquitetura real é Remill/LLVM/Ghidra/SDL3 (ver `docs/REFERENCE.md`) |
| 2 | PE/hashes/imports de todos os módulos | `tools/recon_swat4.py "SWAT 4" analysis/swat4_recon.json` | ok; base 34 arquivos `.exe/.dll`, expansão 34 |
| 3 | Tabela de importações | `tools/imports_report.py analysis/swat4_recon.json` | 1.201 APIs de SO distintas |
| 4 | Discovery `SwatGui.dll` | `python3 tools/discover.py …` | 87 funções, 0,35 s |
| 5 | Discovery `Core.dll` | `python3 tools/discover.py … 0x1000000` | **6.887 funções**, 11,8 s, pico 931 MB; 72% do `.text`; 160 gaps, 18 indirect jumps |
| 6 | Lifting `Core.dll` | `LIFT_FILES=28 LIFT_GAME=swat4 python3 tools/lift.py …` | **6.880/6.887**, 13 MB de C, 13,5 s; 7 rejeições (`daa`, `int1`, `aas`, `das`, `loopne`, regs de segmento) |
| 6b | Difftest lifted × unicorn | `tools/difftest.py …Core.dll …/functions_core.json … 1 0x01000000` | 216 shapes, 216 testes, 214 liftados; **19 divergências em 16/79 mnemônicos** (63/79 ok). x87 domina; `imul`/`mul`/`push`/`xchg`/`emms` também |

### Validação local (sem gerar APK)

| # | O que | Comando | Resultado |
|---|---|---|---|
| 7 | Sintaxe C do runtime | `gcc -fsyntax-only -DGAME_SWAT4=1 -Iruntime -Ici/gen_stub runtime/*.c` | **rc=0** |
| 8 | Sintaxe C++ (dxvk) | `clang++ -fsyntax-only -DDXVK_NATIVE -I…/dxvk/include/native/… runtime/*.cpp` | **rc=0** |
| 9 | Link do conjunto app-only | `gcc runtime/modules.c ci/gen_stub/*.c /tmp/mm.c` | **rc=0**; `modules.c` e as tabelas vazias batem |

### Geração completa do `gen/`

| # | O que | Comando | Resultado |
|---|---|---|---|
| 10 | Discovery+lift dos módulos base | `tools/regen_gen.sh "SWAT 4"` | **33 módulos, 80.848 funções, 434 arquivos, 182 MB; 347 s discovery + 184 s lifting; pico 2,4 GB**. `m4d.dll` reprocessado após correção de bounds; `dinterface.dll` excluído (não importado) |
| 10b | Tabelas do gen satisfazem `modules.c` | `gcc -c gen/*_table.c` + `nm` | **rc=0**; 33 `g_fn_table_*`/`g_fn_count_*` |
| 10c | C levantado compila | `gcc -fsyntax-only gen/{ogg,core,exe,swatgame,engine}_00.c` | **rc=0** (o `engine_00.c` tem 27 MB) |
| 10d | Sem colisão de símbolos entre módulos | parse de `funcs_*.h` | 80.848 `f_*`, **0 duplicados** |

### CI e aparelho

| # | O que | Comando | Resultado |
|---|---|---|---|
| 11 | APK app-only no GitHub Actions | `build.yml` | **ver "CI" abaixo** |
| 12 | APK completo (gen privado) no Actions | `build.yml` com `GEN_REPO` | **ver "CI" abaixo** |
| 13 | Instalar o APK | — | **não testado: sem aparelho** |
| 14 | Launcher validar a pasta no celular | — | **não testado: sem aparelho** |
| 15 | Jogo chegar ao menu principal | — | **NÃO** |
| 16 | FPS / CPU / RAM / tempo de carga | — | **não medido** |

## Resultado do gen

(preenchido após `regen_gen.sh`; números por módulo em `analysis/swat4_regen_report.json`)

## CI

(preenchido após o primeiro run do workflow)

## Não testado de jeito nenhum

* Instalação, UI do launcher, checagem de SHA-1 e Vulkan/dxvk num aparelho.
* Qualquer gameplay, inclusive o menu principal.
* Áudio OpenAL, Bink, input físico e overlay de toque.
* `tools/difftest.py` (lifted vs. unicorn) contra o SWAT 4.
* Desempenho, memória e tempo de carregamento em hardware.
* A expansão *The Stetchkov Syndicate*.