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

`tools/regen_gen.sh "SWAT 4"` rodou sobre a cópia GOG base:

* **33 módulos** (`Swat4.exe` + 32 DLLs), **80.848 funções**.
* **434 arquivos `.c`**, **182 MB**; 33 tabelas `g_fn_table_*`.
* 347 s de discovery + 184 s de lifting (≈ 9 min); pico de RAM **2,4 GB**
  (`Engine.dll`).
* `m4d.dll` falhou no primeiro lote (jump table fora da imagem) e foi
  reprocessado depois da correção de bounds: 1.323 funções, 1.317 liftadas.
* `dinterface.dll` foi excluído: nenhum módulo o importa.
* Tabelas compilam e satisfazem `runtime/modules.c`; `gcc -fsyntax-only` passou
  em `ogg/core/exe/swatgame/engine_00.c`; 0 símbolos `f_*` duplicados.
* Por módulo: `analysis/swat4_regen_report.json` (não versionado).

## CI

O workflow foi depurado em runs reais; cada falha abaixo foi corrigida e
re-executada, não simulada.

| Run | Cenário | Resultado | Causa / correção |
|---|---|---|---|
| `40936429673` | app-only | falhou em 47 s | `./gradlew: Permission denied` — modo do arquivo. `chmod +x` + `git update-index --chmod=+x` |
| `40936667919` | app-only | falhou em 1m29 | `CMake Error game/CMakeLists.txt:6`: o glob resolvia `android/gen`, não a raiz. `../../../` → `../../../../` |
| `40937235775` | app-only | falhou em 1m17 | `ld.lld: undefined symbol touch_draw(IDirect3DDevice9*)`: declaração C++ sem `extern "C"`. Movida para escopo de arquivo como `extern "C" void touch_draw(void *)` |
| `40937766811` | **app-only** | **sucesso** | APK `SWAT4-debug-apponly-8cfa7b8.apk`, 2,0 MB, artefato público. Inspeção: `lib/arm64-v8a/{libmain,libSDL2,libdxvk_d3d9,libc++_shared}.so`, `classes3.dex` com `LauncherActivity`/`GameActivity`, APK assinado |
| `40938210435` | **completo** | **ver abaixo** | baixa `gen.tar.zst` da release privada `gen-v1` e compila |

O APK app-only **não foi instalado** (não há aparelho Android neste ambiente);
a inspeção acima é do arquivo, não de execução.

### Build completo

Run **`40938210435`** (`workflow_dispatch`, `gen_tag=gen-v1`, `jobs=3`):
**sucesso em 9m53s**. O passo `Fetch gen/` registrou `gen files: 434; modules: 33`.

Prova de que o codegen foi compilado e linkado (inspeção do APK baixado da
release **privada** `SWAT4_codegen@gen-v1`):

| Item | app-only | completo |
|---|---|---|
| APK | 2.071.446 bytes | **24.244.530 bytes** |
| `lib/arm64-v8a/libmain.so` | 387.920 bytes | **58.398.176 bytes (56 MB)** |
| símbolos `f_*` no `.so` | 0 | **80.848** |
| tabelas `g_fn_table_*` | 33 (vazias) | **33** |
| `g_fn_table_engine` | 1 entrada `{0,0}` | **0x99E40 bytes = 630.336 B / 39.396 entradas** |

O `libmain.so` é ELF aarch64, `BuildID=fcbc4b56…`, *stripped* (o *strip* do
Gradle remove nomes locais, mas os símbolos globais das tabelas e os `f_*`
permanecem). O APK completo **não** é artefato público: só existe na release
privada.

**Não instalado / não executado**: não há aparelho Android neste ambiente.

## Teste no aparelho (relatado pelo usuário, 2026-10-29)

O usuário instalou o APK completo e abriu o jogo. O launcher funcionou e o jogo
começou a executar. Parou em:

```
unimplemented import MSVCR71!wcslen (called from 010611f1)
```

Log completo: <https://pastebin.com/raw/NcWki3sC>. Ele mostra os **33 módulos
carregando nos endereços previstos**, o `Swat4.exe` em `0x10900000`, o CRT
chamando `_initterm`/`malloc`, e o primeiro import sem shim em `Core.dll`.

## Teste desktop (x86-64, este ambiente)

Compilado `desktop/build.sh` (mesmo `gen/`, `runtime/`, dxvk-native) → `build/lb`
(63 MB). Executado sob `Xvfb` + Vulkan lavapipe (llvmpipe), sem GPU:

```sh
XDG_RUNTIME_DIR=/tmp/xdg \
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json LB_LOG=2 \
xvfb-run -a -s "-screen 0 1280x720x24" ./build/lb "/caminho/SWAT 4"
```

Progresso observado, em ordem, ao longo das iterações:

1. `MSVCR71!wcslen` (o mesmo bloqueio do celular) — implementado o lote wide-char.
2. "Called appMalloc before memory init" em `AllocateNameEntry` — causa: todos os
   `DllMain` rodavam antes do `appInit`. Corrigido: só o *closure* estático do
   exe é inicializado antes do entry; o resto vai por `LoadLibrary(A/W/ExW)`.
3. `__set_app_type`/`__p__commode`/`_cexit` — plumbing do CRT implementado.
4. `FindWindowExA`, splash/dialogs, menus — stubs seguros.
5. `memcpy` com Duff's device (`jmp ecx` para o meio de `movsd`) em `Swat4.exe`
   e `Core.dll` — corrigido no **discovery** (reconhece `add reg, imm; jmp reg`
   e adiciona os 16 alvos como blocos) + overrides de `appStrcpy`/`appMemmove`/
   `appMemset`/`appMemcmp`.
6. `GetModuleHandle(NULL)` devolvia `0x00400000` em vez de `0x10900000` —
   corrigido para `GAME_EXE_BASE`.

7. Áudio OpenAL: a engine logava `Missing function 'alEnable' for '_AL'
   support`. Causa: `ALAudio` pede `OpenAL32.dll`, o módulo do jogo é
   `DefOpenAL32.dll`. Alias no loader (`OpenAL32` → `DefOpenAL32`) + fallback no
   `GetProcAddress` para símbolos `al*`. O erro sumiu e o device ALAudio
   inicializa no desktop.
8. `LoadLibraryExW` estava definido mas não registrado no `k32_init` — corrigido.
9. `?appInit@@YAX...` é **`void`** (o `YAX`), então o `eax=0` do primeiro
   diagnóstico não indicava falha. Instrumentei `FOutputDevice::Logf` para ler
   as mensagens da engine (formatação wide) e removi os hooks de diagnóstico
   depois.

Estado final do desktop: o jogo carrega todos os módulos, inicializa o áudio
(OpenAL), inicializa o **D3D9 via DXVK v1.9.2** (`Driver: 25.2.8`), grava
`S.ini` e sai com `exit(0)` — **sem crash**. `Swat4.log` fica vazio e não há
chamada a `GetMessage`/`PeekMessage` antes da saída. **Não chegou ao menu
principal**: o `WinMain` retorna depois da inicialização do renderer; isolar
qual checagem de device/capability faz a engine desistir é o próximo passo
(hook em `UGameEngine::Init`/criação do device D3D9).

Correções desta sessão estão no código (`runtime/swat4_shims.c`,
`runtime/main.c`, `runtime/kernel32.c`, `runtime/msvcrt.c`,
`runtime/dispatch.c`, `tools/discover.py`, `tools/lift.py`).

### Build #7 (`ebe6f7c`) no aparelho — log do usuário

O usuário instalou o APK de `ebe6f7c` e enviou o log
(<https://github.com/deivid22srk/SWAT4_Android/releases/download/Log/log.txt>,
295.342 linhas). Resultado:

* janela SDL `2400x1080`;
* **DXVK v1.9.2 sobe no celular**, `Vulkan: 1.1.128`;
* `Direct3DCreate9(31)` chamado por `RTCShader.dll` (`ret=027022a7`);
* `GetProcAddress(OpenAL32.dll, alEnable) = 0` (bug corrigido em `658bd68`);
* termina em `msvcrt exit(0)`, **sem crash**;
* **nenhum** `CreateFileA/W` de `.u`/`.utx`/`.s4m` aparece, **nenhum**
  `CreateDevice` é chamado, e `Swat4.log` fica vazio.

Conclusão: o aparelho chega ao mesmo ponto do desktop — inicializa áudio e
D3D9/DXVK e sai limpo antes de carregar pacotes/criar o device. O próximo
bloco é o início do `InitEngine`/`SetRenderConfiguration`, não um crash de
lifting.

### Bloqueio técnico conhecido (2026-10-29)

* `D3DDrv.dll`: **1.854 funções liftadas, 109 rejeitadas** por instruções
  MMX/3DNow! que o lifter ainda não implementa (`pfmul` 45, `pi2fd` 27,
  `pfadd` 6, `pcmpgtd` 6, `pfsub` 4, `pf2id` 4, `pfrcp`, `pfrsqrt`,
  `pswapd`, `pmaxsw`, ...). `?Init@UD3DRenderDevice@@UAEHXZ` **não** está
  entre as rejeitadas, mas os helpers de math que ela pode chamar estão.
  Sem 3DNow! no lifter, partes do renderer não podem ser levantadas.
* A engine sai antes de `CreateDevice`/`.u`; a causa ainda não foi isolada
  (próximo passo: instrumentar `UGameEngine::Init` e o `SetRenderDevice`).
* `Swat4.log` fica vazio (a engine não descarrega o buffer antes de sair).

### Correção da permissão de escrita (2026-10-29, pós-build #9)

A engine chama, logo após `appInit`:

```
CreateFileW(game_dir, GENERIC_WRITE, share=7, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS)
```

para testar a permissão de escrita na **pasta** do jogo. No Linux,
`open(dir, O_WRONLY)` falha com `EISDIR`; a engine concluía
`NoWritePermission`, mostrava uma `MessageBoxW` ("UNKNOWN", por falta de
`W.i.ini`) e saía. **Corrigido** em `runtime/kernel32.c`: quando o caminho é
um diretório, abre com `O_RDONLY | O_DIRECTORY` e devolve o handle.

Depois da correção o jogo **passa da checagem de escrita**, inicializa o
COMCTL32/janela e chega a uma **exceção C++**: `_ThrowInfo` em `0x109210C8`
(exe), tipo `_TypeDescriptor` `.H` (**`int`**), valor **`1`**. É um
`throw <código>` do exe capturado pelo handler SEH do `WinMain`. O runtime
ainda não implementa o unwinding de SEH/C++ do MSVC
(`__CxxFrameHandler`/`_except_handler3`), então aborta com o diagnóstico.
Esse é o próximo bloqueio de runtime, junto com o 3DNow! do `D3DDrv`.

Também foi adicionado um stub do COMCTL32 (`#17`, `_TrackMouseEvent`,
`InitCommonControls`) para a inicialização da janela.

### Janela / GUI (pós-build #9)

Depois da correção da permissão de escrita, a engine passou a falhar em
`GetClassInfoExW(NULL, WinBaseClass, &Cls)` (`Window.cpp:1195`). Correções:

* **registro de classes de janela** (`RegisterClassW/ExW`, `GetClassInfoW/ExW`,
  `UnregisterClassW`) em `runtime/swat4_shims.c`;
* fallback para as **classes padrão/comctl32** que o jogo consulta
  (`SCROLLBAR`, `RICHEDIT`, `ToolbarWindow32`, `SysTreeView32`, ...);
* `DefWindowProcW`;
* `CreateWindowExW` agora envia `WM_NCCREATE`/`WM_CREATE` ao WNDPROC
  registrado (com limite de profundidade, porque `WM_CREATE` cria janelas
  filhas); `hWndCreated==hWnd` deixou de disparar;
* `_vsnwprintf` reescrito para copiar só os literais do formato (o va_list
  vindo de `Window.dll` não é compatível com o formatador genérico e fazia
  `snprintf` andar até o fim do mmap).

Estado atual: a engine passa das asserções e chega ao fim da criação da
janela, mas ainda chama `appRequestExit(1)` (`throw int 1`, capturado pelo
SEH do `WinMain`). O unwinding SEH/C++ continua não implementado.

## Não testado de jeito nenhum

* Instalação/launcher num aparelho controlado por mim (o usuário testou; eu
  não tenho o aparelho para reproduzir).
* Qualquer gameplay, inclusive o menu principal.
* Áudio OpenAL, Bink, input físico e overlay de toque.
* `tools/difftest.py` (lifted vs. unicorn) contra o SWAT 4.
* Desempenho, memória e tempo de carregamento em hardware.
* A expansão *The Stetchkov Syndicate*.
## 2026-10-30 — tela preta no celular (kklog.txt) e bring-up desktop com DXVK real

### Análise do log do aparelho (`kklog.txt`, build `b05c410`)

2.869.066 linhas de trace. Linha do tempo: 33 módulos carregam nos endereços
previstos → DllMains em ordem de loader → splash/janelas → **4540 CreateFileA
de pacotes .u** (SwatGame.u 313K, Engine.u, Core.u, SwatEquipmentModels.u com
fd=123 ✓) → binding nativo de UFunctions (2,77M) → 30 tentativas de abrir
`../Classes/ALAudio(.u)` em todos os paths (o pacote ALAudio não existe como
.u; resolve por LoadPackageDLL e segue) → caminhada de diretórios
(FindFirstFileW no mesmo call site, ~75 varreduras de ~8 entradas) → fim do
log com o jogo ainda em execução. **Sem appError, sem C++ exception.**

O Direct3DCreate9 só aparece 2x, ambos de `RTCShader.dll!QueryDX9Support`
(sucesso, DXVK inicializa 2x). O `D3DDrv.dll` (que importa
`d3d9.dll!Direct3DCreate9` pelo IAT, thunk compartilhado) **nunca chama** —
o renderer não chegou a inicializar no aparelho quando o log foi capturado.

### Bring-up desktop com DXVK real (não o stub)

O stub `dxvk_stub/libdxvk_d3d9.so` da sessão anterior retorna NULL de propósito
("Direct3DCreate9 sempre falha") — o erro *Please install DirectX 8.1b or
later* do desktop era artefato do teste. Ambiente reconstruído sem root: SDL2
e Vulkan headers/libs extraídos de .deb para `prefix/` (dpkg -x), cmake/ninja
via pip, gcc no lugar do clang, dxvk-native d3d9 compilado via
`desktop/CMakeLists.txt`. O `gen/` exigiu divisão dos arquivos >6 MB
(`engine_00.c` 27 MB e afins) em pedaços por fronteira de função para caber nos
4 GB deste ambiente (`scripts/split_gen.py`, somente local; o CI usa o gen do
repo privado, intacto).

Correções, cada uma reproduzida no desktop e commitada (`2c18fab`):

| # | Bloqueio (ordem do boot) | Causa | Correção |
|---|---|---|---|
| 1 | `FATAL: unimplemented import WS2_32!#115` | `m4d.dll` chama WSAStartup durante o init do D3D; WinSock não tinha shim nenhum | `runtime/ws2.c`: WS2_32+WSOCK32 por nome e ordinal (WSAStartup preenche WSADATA 2.2; sockets falsos; escrita OK, leitura EWOULDBLOCK) |
| 2 | `FATAL: unimplemented import MSVCR71!mbstowcs` | Engine.dll converte a descrição do device na init do renderer | `mbstowcs` em `runtime/msvcrt.c` |
| 3 | `Can't find 'ini:Engine.Engine.DefaultPlayerMenu' in configuration file` durante `LoadMap: Entry.s4m` | chave UE2.5 que o ini do SWAT 4 nunca teve; a engine resolve `ini:` com Filename≠NULL e `appThrowf` — no Windows um catch C++ engole, no lifted não há unwinding | `fixup_game_data()` adiciona `DefaultPlayerMenu=SwatGui.SwatMainMenu` ao Swat4.ini no boot |
| 4 | `Failed to find object 'Texture gui_tex.resetbutton'` na carga dos estilos | o GuiBase.ini de fábrica referencia uma textura que **não está em nenhum .utx do jogo** (verificado por parser de name table em todos os pacotes; só essa de 436 refs falta); mesmo throw não capturável | `fixup_game_data()` reescreve as 5 referências para `gui_tex.backbox` |

Estado desktop após `2c18fab` (LB_LOG=1, Xvfb + lavapipe): 33 módulos,
pacotes, binding, Havok, `Unreal engine initialized`, `Client initialized`,
device D3D9 criado com caps PS/VS 3.0, `LoadMap: Entry.s4m` executado
(0,15 s), loop de render com swapchain DXVK 1280x720/3 imagens. O 2º
CreateDevice (troca de modo na carga do level) leva minutos no lavapipe
(compilação de pipeline em CPU) — no Adreno é GPU real. Sem crash em 300 s de
execução; framebuffer do Xvfb ainda preto (intro/entry), verificação visual do
menu pendente.

Diagnóstico instrumentado que sustenta a tabela: `_vsnwprintf` agora loga a
mensagem formatada (`  _vsnwprintf -> "..."` no trace), e o trace do
FindFirstFileW mostra spec/host de cada varredura.

## 2026-10-30 (tarde) — tela preta do celular (olalog.txt) reproduzida e corrigida no desktop

Ambiente reconstruído após reset do sandbox (clang/cmake/ninja de
`*.deb` extraídos com `dpkg-deb -x` para `deproot/`; Xvfb + xauth +
mesa-vulkan lavapipe + `libxkbcommon-x11-0` idem; SDL2 completa
`libsdl2-2.0-0` porque a do sistema só tem drivers dummy/offscreen —
sem `libxkbcommon-x11-0` o SDL não escolhe o driver x11 e o DXVK
exige janela com Vulkan). Xvfb precisa rodar **na mesma chamada** do
jogo (o sandbox mata processos de fundo entre comandos):

```sh
Xvfb :99 -screen 0 1280x720x24 -nolisten tcp & XPID=$!; sleep 2
DISPLAY=:99 LB_LOG=1 ./build/lb "<SWAT 4>"
kill $XPID
```

### Reprodução do travamento do celular

O desktop com os shims antigos reproduziu **exatamente** o log
`olalog.txt` do aparelho: `Assertion failed: hWndCreated==hWnd
[File:.\Window.cpp] [Line: 1083]` em loop infinito (31 M linhas de log
em ~100 s). Instrumentei o shim `_vsnwprintf` para despejar a cadeia
guest (ebp-chain + scan de pilha, módulo+offset) nas 4 primeiras
assertions — a pilha simbolizada apontou:
`f_2006990 → f_2028F40 (Window.dll) → CreateWindowExW → check`.

### Causa raiz 1: binding objeto↔HWND

O `WndProc` do jogo (f_2024930) grava o ponteiro do objeto com
`SetWindowLongPtrW(hwnd, 0, ((CREATESTRUCT*)lp)->lpCreateParams)` no
WM_NCCREATE e lê de volta no WM_CREATE para fazer `obj->hWnd = hwnd`;
o `check(hWndCreated==hWnd)` do Window.cpp:1083 compara o retorno de
`CreateWindowExW` com esse campo. Os shims `Set/GetWindowLongW`
descartavam tudo → campo sempre 0 → assert → o caminho de erro da
UE2 reinseria a janela num loop. Correções (`runtime/user32.c`,
`runtime/swat4_shims.c`):

* **registro de janelas** compartilhado (A/W): HWND único por janela
  (0x00010001 + 4n), wndproc por hwnd, slots `GWL_*` e class-extra
  reais em `Set/GetWindowLongA/W` (antes: `GetWindowLongA` devolvia
  estilo constante e `SetWindowLong` não guardava nada);
* `CreateWindowExA` (user32.c) entregava WM_NCCREATE/WM_CREATE só na
  variante W — agora as duas entregam e registram a janela após o
  WM_CREATE (contrato Win32: `IsWindow` só vira TRUE depois que
  `CreateWindowEx` retorna);
* `IsWindow` consulta o registro (era sempre 1 — durante o
  WM_NCCREATE o wndproc virtual de Window.dll consulta IsWindow e
  seguia por `CallWindowProc(prevproc=0)` → 0 → "CreateWindowEx
  failed");
* `CallWindowProcA/W` novos (encaminham ao guest; prevproc NULL →
  resposta de DefWindowProc); `DispatchMessageW` despacha ao wndproc
  do hwnd da mensagem; `PeekMessageA` carrega o hwnd da janela
  principal; `EnableWindow`/`WindowFromPoint` novos.

### Causa raiz 2: `ShowCursor` sem contador

Após as janelas, o desktop travou num loop infinito de
`USER32!ShowCursor` vindo de WinDrv.dll+0x55b4: o Win32 ShowCursor
mantém contador (retorna o novo valor; visível se >= 0) e o WinDrv
põe o cursor escondido com `while (ShowCursor(FALSE) >= 0)`. O shim
devolvia constante → loop eterno (a "tela preta" da sessão anterior
no desktop). Corrigido com contador real.

### Causa raiz 3: IMM32 ausente

`unimplemented import IMM32!ImmAssociateContext` (WinDrv): lote de
shims IMM32 (contexto fake não-nulo, `ImmGetContext` etc.).

### Estado após as correções (desktop, LB_LOG=1, Xvfb+lavapipe)

* 5 janelas criadas, todas com WM_NCCREATE → 1 e `CreateDevice →
  00000000` (DXVK 1.9.2, swapchain 1280x720/3 imagens);
* carregamento de pacotes avança: SwatProcedures.u, SwatObjectives.u,
  SwatGui.u ("Updating Revision level for <GUID> to 0"), centenas de
  .int consultados (SwatGame/SwatGui/SwatAmmo/GUI.int);
* **novo bloqueio (em investigação)**: crash com leitura guest em
  `0x1190` durante o binding das classes SwatGui — cadeia
  `f_1090A350 → f_10909770 → (SwatGame) → f_1411BF8/f_1410E38/
  f_140E755/f_140A187 (Engine) → VM Core (ProcessInternal) ×4 níveis
  GUI↔VM → f_10903E00 → f_109041A0+0x2e6` — a lookup de FName
  (`ebx>>24` = bucket em `this+0x25C`, nó em `(byte<<5)+bucket+0x10`)
  achou bucket vazio onde o Windows tem nó sentinela. O caminho de
  mensagens antes do crash mostra serialização de defaults de classes
  GUI ("SaveColor.", "LinkOverrides[n]", "TransitionSpec.",
  "ClientBounds[n]").

Instrumentação mantida (com limites): dump de cadeia guest nas 4
primeiras "Assertion failed" (`runtime/msvcrt.c`), log dedup de
`GetFileAttributesW` + watchdog de pilha após 3000 chamadas
(`runtime/swat4_shims.c`), log do retorno WM_NCCREATE por janela.

## 2026-10-30 (noite) — tela preta no celular (kslog.txt): sondas de arquivo afogadas em FUSE

Log do aparelho: `kslog.txt` (85,3 MB, 2.014.962 linhas, baixado de
`releases/download/Log/kslog.txt`). Corrida com o APK que já contém as
correções de janela/ShowCursor/IMM32: o boot do aparelho agora cria
janelas (WM_NCCREATE → 1), DXVK 1.9.2 carrega, `Direct3DCreate9` é
chamado 2× do exe (`GetAdapterIdentifier`/`GetDeviceCaps`/`Release` —
ainda sem `CreateDevice`, que só vem depois do preload de pacotes), e o
jogo entra no **preload dos 135 pacotes do boot** ("Loading file …",
contador 0..134). Carrega 134 pacotes e **trava no último**,
`FP_paintball`:

* bind do "FP_paintball.dll" ok (pseudo-módulo, `load_library`);
* `FindPackageFile` sonda `System\.\FP_paintball` (falha), depois
  `System\..\..\Content\System\FP_paintball` — chamada sem resultado no
  log, seguida só do loop de poll (`Enter/LeaveCriticalSection` em
  Core.dll+0x8543f/0x85483 + `Sleep` em +0x85494) até o fim do log.

### Causa raiz: cada sonda custa um readdir de FUSE

O `CreateFileA` do aparelho custa 53–512 iterações do poll (mediana 53,
p90 199, máx 512) porque **cada sonda de arquivo inexistente re-listava
o diretório**: o dircache de 16 slots era chaveado pela grafia literal
do caminho, e o jogo sonda os mesmos ~14 diretórios físicos através das
grafias do `Startup.ini [Filesystem]
FilePath=..\..\Content\System;..\..\Content\Scenarios;..\..\Content\Maps;..\..\Content\Test\Maps`
+ CWD → **56 grafias distintas** para ~14 diretórios (5.743 sondas
falhas contadas no log) → thrash garantido → `opendir/readdir` via FUSE
a cada sonda → o preload de 135 pacotes (centenas de sondas por pacote)
leva dezenas de minutos de tela preta. `FP_paintball.ukx` **existe** no
dados (`Content/Animations/FP_paintball.ukx`, 392.029 bytes —
confirmado no SW4.zip); o boot só nunca chega na sonda
`../Animations/FP_paintball.ukx`.

### Correção (`runtime/kernel32.c`)

* `path_canonicalize()`: colapso lexical de `//`, `/./` e `/../` no
  caminho host dentro de `rt_host_path()` → as 56 grafias colapsam para
  os diretórios reais; chaves do dircache (e `dircache_forget`) ficam
  consistentes;
* dircache 16 → 256 slots.

### Validação (replay das sondas reais do kslog)

`tools/replay_probes.py` extrai o bloco real de `runtime/kernel32.c`
(dircache/dir_lookup_ci/resolve_ci/path_canonicalize/rt_host_path) e
gera `canon_test.c` (com a variante pré-fix) (sem canonização, 16 slots)
lado a lado; `tools/replay_probes.py extract` gera 5.496 caminhos CreateFileA
distintos do kslog. Resultado contra a árvore do jogo extraída
(SW4.zip):

| Métrica | Antes | Depois |
|---|---|---|
| Divergências de resolução (stat inode) | — | **0** (obrigatório) |
| Arquivos encontrados | 112 | 112 |
| Grafias de diretório distintas | 56 | **13** |

Além disso: `gcc -DGAME_SWAT4 -std=gnu11 -O2 -Iruntime -c
runtime/kernel32.c` rc=0. O replay não mede FUSE (o aparelho simula os
`readdir` a cada miss; aqui só o número de chaves importa — 13 ≤ 256 →
cada diretório é listado 1×).

Estado esperado no aparelho após o fix: preload dos 135 pacotes em
segundos, boot segue para `CreateDevice` (DXVK) — a partir daí vale o
estado do desktop: bloqueio seguinte conhecido é o crash de FName no
binding do SwatGui (seção 2026-10-30 tarde).

## 2026-10-31 — boot "travado em 65%" (inog.txt): trace de API + sondas negativas afogando o FUSE

Log do aparelho: `inog.txt` (373 MB, **9.097.149 linhas**,
`releases/download/Log/inog.zip`, APK `eef089c` com o fix de canonicalização).
Screenshot `Screenshot_20261030-135257.png`: splash com a barra parada em
~65%.

### O que o log mostra (linha do tempo)

| Linhas | Fase |
|---|---|
| 0–245 k | 33 módulos carregam nos endereços previstos, DllMains, splash sobe |
| 245 k | começa o loop de poll (`Enter/LeaveCriticalSection` + `Sleep` em `Core.dll+0x8543f/0x85483/0x85494`) — thread de fundo, inofensiva |
| 277 k–7,69 M | **preload de pacotes avança**: 155 eventos `Loading file X...` (SwatGame.u → ... → Editor.u, Packages.md5, Entry.s4m, SwatGui.u, IpDrv.u) |
| 7,69 M–9,09 M | serialização de defaults de classes GUI ("SaveColor.", "LinkOverrides[n]", "TransitionSpec.NewPos.", "SelectedBKColor.") + centenas de sondas `.int` (Transient.int ×330, SwatGame.int ×225, todas fd=-1) |
| 9.097.149 | log termina no meio do loop (app morto travado) |

**O boot não estava morto — estava progredindo a ~48 k linhas de trace por
pacote**, ou seja, invisível (a barra para de se mover por design depois do
último bind de DLL) e lento demais para terminar.

### Causas raiz medidas no log

1. **Trace de API em nível 2 com log line-buffered em FUSE.** O launcher
   grava `log=2` no `port.cfg` quando o usuário marca "verbose"; cada linha
   do trace virava um `write()` no armazenamento compartilhado. 9 M de
   linhas = 9 M de escritas FUSE só de log. Contagem por API: `timeGetTime`
   208 k, `_vsnwprintf` 145 k, `GetFileAttributesW` 60 k, `Sleep` 60 k,
   `GetFullPathNameA` 51 k, `CreateFileW` 26 k.
2. **Sondas negativas repetidas.** O mesmo arquivo ausente é sondado
   centenas de vezes (o UE2 reabre o `.int` de localização a cada consulta
   que falha); cada sonda custa canonicalização + `resolve_ci` + `stat` no
   FUSE. O dircache de 256 slots já evita o `readdir`, mas o `stat`/open
   falho repete.
3. **Barra do splash congelada por design.** `pct = 1 - e^(-binds/90)` só
   avança com `LoadLibrary`; após os ~95 binds ela para (~65%) e o preload
   inteiro acontece sem feedback — é exatamente o screenshot.

### Correções (runtime/, um commit)

* **Negcache de sondas negativas** (`runtime/kernel32.c`): `rt_host_path`
  agora devolve `-1` (miss já confirmado antes, servido por hash FNV de
  4096 slots), `0` (miss novo) ou `1` (achou); misses novos entram no
  cache. `CreateFileA` falha imediato (ERROR_FILE_NOT_FOUND) no cache hit —
  **exceto** com `O_CREAT` (criação é permitida); `GetFileAttributesW/ExW`
  pulam o `stat` no hit. Invalidação: `dircache_forget` chama
  `negcache_flush()` (e `DeleteFileA`/`MoveFileA`/`CreateDirectoryA`
  passam a invalidar o diretório).
* **Log tamponado + flushes periódicos** (`runtime/main.c`,
  `runtime/platform_sdl.c`): `log.txt` sai de `_IOLBF` (1 write/linha) para
  `_IOFBF` de 256 KB; flush a cada tick do splash (120 ms) e a cada 60
  frames — o fim do log sobrevive a kill e o custo despenca ~100×.
* **Dedup do trace de API** (`runtime/dispatch.c`): chamadas cujo par
  (nome, esp, ret) já apareceu na janela das últimas 64 são contadas, não
  impressas (`(×N)` a cada 20 k) — o spam do loop de poll (Enter/Leave/Sleep
  ×460 k) colapsa.
* **Splash avança no preload** (`runtime/platform_sdl.c`,
  `runtime/msvcrt.c`): o shim `_vsnwprintf` reconhece `"Loading file "`
  (strncmp, custo desprezível) e chama `splash_load_bump()`; a barra passa a
  andar de ~66% (≈95 binds) até 95% ao longo dos ~160 pacotes.

### Validação

* `gcc -fsyntax-only -DGAME_SWAT4=1 -Iruntime -Ici/gen_stub -I<SDL2>`
  sobre `runtime/*.c`: **rc=0**.
* Harness novo `tools/negcache_test.c` (bloco real de `kernel32.c`
  extraído na compilação, árvore falsa em /tmp): 9/9 checagens ok —
  sonda ausente 0 → -1 (cache) → criação + `dircache_forget` → 1; caixa
  diferente e grafias `./`/`../` resolvem para o mesmo host path.

### Achados para o próximo bloqueio (crash no GMalloc, corrigido em 2026-10-31)

* **Correção da interpretação anterior**: a estrutura em `this+0x25C`
  (256 buckets, nós de 32 bytes, LUT de 0x8001 entradas, contadores em
  `+0x20664/68/6C/70/74/78`) **não é a tabela de FName — é o malloc
  customizado do jogo**. Provas: a cadeia do crash passa por
  `f_1004994` (Core: `TArray::Resize` → `M32[0x10F53A0]` = global GMalloc
  → vtbl[1]) e `f_10041BA` (Core: atribuição de `FString`); o objeto
  `this` no crash é `0x1092ABE8` (estático do exe), cujo construtor
  (`f_109189A0`, exe_07) grava `M32[0x1092ABE8] = 0x1091CAA0` — a vtable;
  `f_1004994` chama vtbl[1] com `(ptr, newSize, tag)` = **Realloc** e o
  padrão `M32[ecx]→vtbl[2](ptr)` usado em todo o Core = **Free**.
* A anotação de desktop `f_109041A0+0x2e6` (seção acima) foi lida como
  offset **guest** e reatribuída a `f_10904350`; a anotação do handler é
  offset do **código compilado**, portanto o crash do desktop também era
  em `f_109041A0` mesmo (a "formatadora de estatísticas" não acessa
  buckets — revisado no gen).

## 2026-10-31 (noite) — polog.txt: passa do loading e fecha (crash real)

Log do aparelho: `polog.txt` (65 MB, 1.801.765 linhas,
`releases/download/Log/polog.zip`, APK `124bb2c`).

### Progresso enorme

* `Init: Game engine initialized in 19.427 seconds` (antes o boot nem
  terminava);
* `Browse/LoadMap: swat_splashscene`, ~30 pacotes ligados
  (`SwatEffects.dll`, `REPOhelicopter.dll`, …),
  `[dkaplan]: URL.Map = swat_splashscene`;
* o splash passa e a barra anda (as correções de FUSE/negcache/splash
  funcionaram).

### O crash

`CRASH: signal 11 code 2 at address 0x75b2677190` = leitura guest em
**0x1190** (página nula), thread principal, ~176 mil linhas depois do
início do LoadMap:

```
SDL_main → f_109173DC → f_109115C0 → f_10910280 (loop principal)
  → f_1090A350 → f_10909770 → f_1411BF8/f_1410E38/f_140E755/f_140A187 (Engine)
  → f_1003BF2 (Core, ProcessEvent) → VM ×4 níveis → f_140A0CE (Engine)
  → f_10041BA (Core, FString::operator=) → f_1004994 (Core, TArray::Resize)
  → vtbl[1] f_10903E00 (exe, Realloc; newSize==0 → vtbl[2])
  → f_109041A0 (exe, Free) → bucket[0x01] == 0 → M32[0x1180+0x10] → 0x1190
```

* O ponteiro sendo liberado é **0x018C8AB0** — um **buffer estático do
  Engine.dll** (imagem 0x1400000-0x1C29000): `f_140A0CE` grava
  `M32[ebp-0x34] = 0x18C8AB0` (FString local com data = estático) e o
  pipeline de serialização de defaults das classes GUI
  ("SaveColor.", "TransitionSpec.NewPos.", "ClientBounds[n]",
  "Transient.int") termina liberando/realocando esse estático.
* `Free` computa região `ptr>>24 = 0x01`, lê `M32[this + 0x04 + 0x25C]`
  (**bucket vazio** — o alocador nunca usou a região 0x01: as arenas vêm
  de VirtualAlloc ≥ 0x40000000) e desreferencia `bucket + (byte2<<5) +
  0x10` sem checar → página nula. `byte2 = 0x8C` → 0x8C*32+0x10 = 0x1190.
* No Windows o mesmo código também crashearia; o estado upstream diverge
  (uma atribuição de string vazia chega a esse caminho na porta).

### Correção: guard de ponteiro estrangeiro no GMalloc

`runtime/swat4_shims.c` (`swat4_gmalloc_guard_install`, chamada de
`main.c` logo após `load_pe(GAME_EXE)`): a vtable do alocador
(`0x1091CAA0`) tem vtbl[1]=Realloc e vtbl[2]=Free substituídos por thunks
da runtime que classificam o ponteiro:

* imagens dos módulos (0x01000000-0x04000000, exe 0x10900000-0x10A50000),
  stacks/TEB/PEB (0x1A000000-0x1CA00000) e heap da runtime
  (0x20000000-0x40000000) → **estrangeiro**;
* demais faixas → estrangeiro **somente se** `bucket[ptr>>24] == 0` (a
  própria tabela do alocador decide — as arenas em 0x40000000+ ficam
  rastreadas e intocadas).

Emulação: `Free(estrangeiro)` = no-op; `Realloc(estrangeiro, 0)` = 0 (o
chamador grava `Data=0`, FString vazia e sã); `Realloc(estrangeiro, n)` =
bloco novo via o caminho Malloc original (os chamadores conhecidos
sobrescrevem o buffer inteiro em seguida). Ponteiros rastreados seguem
pelo código original. Cada evento loga `gmalloc guard: …` (8 primeiros +
cada 1000º) — o próximo log dirá quem está liberando estáticos.

### Validação

* `gcc -fsyntax-only -DGAME_SWAT4=1 -Iruntime -Ici/gen_stub` sobre
  `runtime/swat4_shims.c` e `runtime/main.c`: **rc=0**.
* Não executável aqui (sem gen compilado local/NDK); a verificação real é
  o build.yml + o próximo log do aparelho. A instalação tem auto-proteção:
  se a vtable não contiver 3 endereços do exe, não patcha e loga.

## 2026-10-31 (noite) — 45log.txt: depois do loading só tela preta (sem fechar)

Log do aparelho: `45log.txt` (576 MB, **16.240.901 linhas**,
`releases/download/Log/45log.zip`, APK `dc0bd0c` com o guard do GMalloc).

### O guard funcionou

Nenhum `CRASH`, nenhum exit: o app ficou vivo do início ao fim do log. O
guard disparou só 2× (`foreign realloc ptr=018c8ab0 size=0`, o estático do
Engine.dll do crash anterior) e os dois viraram `Data=0` limpos — o jogo
seguiu em frente.

### Linha do tempo (marcos UE2 no trace)

| Linha do trace | Evento |
|---|---|
| 11,93 M | `Game engine initialized in 42.331s`, `Startup time: 48.523s` |
| 11,92 M | DXVK 1.9.2: `CreateDevice -> 0`, swapchain 2285x1080x5 (MAILBOX) |
| 11,93 M | `LoadMap: swat_splashscene`, level carregado em 1,035s |
| 15,37 M | `Loadmap executed in 11.75 seconds` |
| 15,43 M | `Finished precaching textures in 4.604 seconds` |
| 15,73 M | subsistemas: `VisualEffectsSubsystem created`, config de som ok, `Bound to` dos 9 pacotes `SW_feet…SW_meta` |
| 15,74 M–16,24 M | **construção do banco de som IFC**: 765 `SoundSet`, 2509 `StreamSoundRef`, 896 `EventResponse`, 78 `NormalSoundRef` |
| 16.240.901 | log termina no meio do churn de FString (main thread viva, poller vivo) |

### Por que a tela fica preta (e por que não é crash)

Depois da barra de loading o jogo entra no carregamento do banco de som
(`SoundEffectsSubsystem`/IFC23): **zero chamadas D3D** no trecho (sem
`Present`, sem draw) — a janela mostra o último frame ou preto, igual no PC,
onde essa fase também é uma pausa preta entre a barra e o menu. É trabalho
100% de CPU: FString/wcsicmp/wcscat + GMalloc (~1,5 M chamadas de API no
trecho). As provas de vida: o poller de fundo continua (`Enter/Leave/Sleep`
em `Core.dll+0x8543f/83/94`, `Sleep(1)` — confirmado no gen: `f_10853F0`
empurra `1` antes de chamar o ponteiro de Sleep), o main thread segue lendo
pacotes (`ReadFile` até o fim) e os contadores bare do debugf são os totais
correntes: o "765" e o "2509" do fim do log batem exatamente com o número de
mensagens `SoundSet`/`StreamSoundRef` — **a construção estava ~90% completa
quando o log foi capturado**.

Custo extra medido: 3710 sondas de arquivo negativas no trecho (negcache
segura) e o **trace em log=2 custando mais que o jogo**: a chave do dedup
fazia `snprintf` + varredura linear de 64 `strcmp` por chamada (×2,
enter+leave) — ~44 M chamadas no boot inteiro.

### Correções (runtime/, um commit)

* **trace_thunk barato** (`runtime/dispatch.c`): chave montada à mão, hash
  FNV-1a de 32 bits, janela de 64 hashes (`uint32`) em vez de 64 strings de
  96 bytes, emissão com `fwrite` + hex manual. Mesma semântica de janela e
  de `(xN)`/`(repeated calls xN)`; custo por chamada cai ~10×.
* **Heartbeat de 5 s** (`runtime/kernel32.c`, shim `Sleep`): o thread de
  poll do jogo dorme 1 ms em loop durante TODAS as fases silenciosas; o shim
  agora loga `[heartbeat] t=Ns (Sleep xN, ms=N)` a cada 5 s com
  `fflush` — o próximo log tem eixo de tempo de verdade e sobrevive a kill.
* **Progresso do banco de som** (`runtime/msvcrt.c`, shim `_vsnwprintf`):
  conta `SoundSet `/`StreamSoundRef `/`EventResponse_SoundEffectsSubsystem `
  (1 strncmp por mensagem) e loga `[sound db] sets=N refs=M evt=K t=Xms`
  a cada 100 sets / 500 refs / 200 event responses, com flush — a próxima
  sessão mostra o ritmo real da fase preta e se ela termina.

### O que esperar no próximo log

* `[sound db] … t=Xms` deve chegar ao total (~800–900 sets / ~3000+ refs) e
  parar; logo depois entram as chamadas de áudio (`DirectSoundCreate8` /
  `CoCreateInstance(CLSID_DirectSound8)`), o init do engine termina e o loop
  principal começa a presentar frames (primeiros `IDirect3DDevice9#17`).
* Se a tela continuar preta com heartbeats avançando e sem D3D, o log dirá
  em qual fase o tempo está indo — sem mais "travou ou não?".
* Pedir ao usuário: **esperar vários minutos** depois do fim da barra antes
  de julgar; a fase é longa também no PC (pausa preta entre a barra e o
  menu), só que no aparelho ela é mais longa ainda.

### Validação

* `gcc -fsyntax-only -DGAME_SWAT4=1 -Iruntime -Ici/gen_stub` sobre
  `runtime/dispatch.c`, `runtime/kernel32.c`, `runtime/msvcrt.c`: **rc=0**
  (sem warnings novos; os de `-Wall -Wextra` são os pré-existentes de
  `x86.h`).

## 2026-10-30 (noite) — bnlog: crash de heap depois do loading

### O log (`ekk.zip` → `bnlog.zip`, build `85dd9d8`)

Progresso real desta build: a barra anda, o banco de som termina e o menu/
cena começa a desenhar (aparecem `SetStreamSource`/`DrawIndexedPrimitive`).
Aos **52 s de uptime** o SDLThread morre:

```
Fatal signal 11 (SIGSEGV), code 2 (SEGV_ACCERR), fault addr 0x75b3e5a009
  #00 gmalloc+284            (libmain.so, heap.c)
  #02 B_IDirect3DIndexBuffer9_Lock(CPU*)+312
  #03 rt_call+156
  #07 f_222EFD0+0x384        (D3D9Drv.dll+0x2EFD0 — GetIndexBuffer dinâmico)
  #08 f_2226EB0              (D3D9Drv — DrawPrimitives)
  #09..#15 f_14017A3/140F7EF/14030D5/1401889/1412F26/14103D4/1411DFB (Engine)
  #16/#17 f_21010A0/2101028  (WinDrv)
  #18..#23 f_10909770/…      (Swat4.exe)
```

Diagnóstico: o fault addr é **MEM+9** — a primeira página do guest é
`PROT_NONE` (guarda de null), então o `gmalloc` dereferenciou um "bloco" no
endereço guest ~5. Os registradores no crash (`x10/x11/x14` apontando para
`0x20C4E3F8..400`, `x19=0x1c`, `x20/x21=0x30` — valores típicos de índices
16-bit) mostram que **dados de índice/vértice sobrescreveram links da free
list ou headers de bloco** do heap guest. O `gmalloc` não tem como saber
disso — ele só anda a lista.

Vetores prováveis (o heap guest serve a tudo):
1. **Escrita através de ponteiro de lock depois do Unlock** — o próprio
   bridge comenta que jogos fazem isso com texturas SYSTEMMEM (por isso
   existe o caminho *persist*); para vertex/index buffer o shadow é
   `gfree` no Unlock, e escrita tardia cai em bloco livre.
2. **Overrun da região travada** (o `+16` de folga já existia porque
   overruns pequenos são conhecidos).
3. `gfree`/`grealloc` recebendo ponteiro inválido que por acaso cai na
   faixa do heap (o guard do GMalloc só cobre os vtables do jogo).

### Correções (runtime/, um commit)

* **`runtime/heap.c` endurecido** — o alocador agora valida tudo que toca:
  * `block_sane`/`link_sane`: intervalo `[GUEST_HEAP_BASE+8, g_top)`,
    alinhamento (headers ≡ 8 mod 16, payload ≡ 0), tamanho ≥ MINBLK e
    múltiplo de 16, bloco dentro de `g_top`;
  * `gmalloc` valida cada entrada da free list (e conta passos contra
    ciclo); ao achar lixo: loga `heap: CORRUPTION #N`, **reconstrói as free
    lists pela cadeia de boundary tags** e recomeça — o jogo continua;
  * a reconstrução **quarentena** a memória a partir do primeiro header
    insano (`g_poison_min`): blocos vivos ali continuam funcionando, só não
    é mais entregue memória daquela região (vazamento controlado em vez de
    crash);
  * `gfree`/`gsize` rejeitam ponteiro inválido (`bad free pointer`) e
    double free com log, sem tocar no heap; merges validam os headers do
    vizinho antes de fundir;
  * anel de 64 eventos `alloc/free` (com *tag* da origem) logado junto de
    cada corrupção — o próximo log diz **qual alocação estava perto** do
    bloco estragado;
  * `gmalloc_set_tag()`: "idx-lock"/"vtx-lock"/"tex-rect"/"tex-box" nas
    alocações de shadow do bridge.
* **`runtime/d3d9_bridge.cpp`**:
  * shadow de lock agora é `[dados][+16 folga][+32 canário 0xA9]`; o
    canário é verificado no Unlock/flush e loga
    `d3d9: GUEST OVERRUN … tag=idx-lock … wrote at least N bytes past the
    locked region` — pega o vetor 2 em flagrante com o objeto e tamanho;
  **vertex/index (e locks simples) passam a reusar UM buffer guest por
  objeto** (`g_buf_shadow`) em vez de `gmalloc`/`gfree` por lock (vetor 1:
  escrita tardia cai no próprio buffer em cache, que o próximo lock do
  mesmo objeto reaproveita); liberado no Release via `shadow_forget`;
  * `Lock(off ≥ desc.Size)` volta `D3DERR_INVALIDCALL` com log em vez de
    `size = desc.Size - off` com underflow.
* **`runtime/main.c`**: log de boot `heap: hardened allocator active …`
  para confirmar em campo que a build nova está rodando.

### Validação local (sem aparelho)

* **Teste novo `tools/heap_test.c` + `tools/test_heap.sh`** (compila o
  `runtime/heap.c` real com stub mínimo de rt.h): smoke, reuse/split de
  bins, **corrupção de link → rebuild e o bloco volta a ser reusado**,
  `gfree` selvagem/desalinhado/double-free descartados, header corrompido
  no `gfree`, **chain quebrada → quarentena e gmalloc vivo**, e martelada
  de 2×20000 alloc/free em 2 threads sem evento de corrupção: **ALL PASS**.
  Durante o teste o próprio alocador achou e corrigiu um bug do primeiro
  rascunho (mínimo de `gfree` rejeitava o primeiro bloco do heap).
* `gcc -fsyntax-only -DGAME_SWAT4=1 -Iruntime -Ici/gen_stub` sobre os `.c`
  sem dependência de SDL (inclui `heap.c`, `main.c`): **rc=0**.
* `g++ -fsyntax-only -std=c++17 -DGAME_SWAT4 -DDXVK_NATIVE` (stubs de SDL2
  no sandbox) sobre `d3d9_bridge.cpp`, `d3d9_bcemu.cpp`,
  `d3d9_bridge_gen.cpp`, `touch_swat4.cpp`: **rc=0**.

### O que esperar no próximo log

* Se o crash era corrupção de heap: ou o jogo **passa** do ponto (rebuild
  em ação), ou morre longe disso com `heap: CORRUPTION` + anel de eventos +
  `GUEST OVERRUN` dizendo tag/objeto/tamanho — ou seja, o próximo log
  **nomeia o culpado** em vez de dar SIGSEGV seco no `gmalloc`.
* `heap: hardened allocator active` no boot confirma a build nova.

## 2026-10-31 — lahlog: "out of guest memory for a 1579392 byte lock"

### Leitura do log (653 MB, 17,88 M linhas — o jogo chegou MUITO mais longe)

Cadeia completa, em ordem:

1. `d3d9: GUEST OVERRUN #1 (unlock): tag=tex-rect obj=… guest=20ee3c40
   size=1579392 wrote at least 17 bytes past the locked region (canary +0=00)`
   — o vídeo Bink do loading escreveu **além do fim** do shadow de lock de
   1,5 MB; como o overrun passou das 48 bytes de folga+canário, alcançou os
   boundary tags do bloco seguinte (header `00000000 00000000`).
2. `heap: CORRUPTION #1: next-block header on free (a=210655f8 b=20ee3c38)`
   e `#2/#3: free-list entry … c=00616880` — links das free lists com lixo.
3. `heap: block chain broken at 1d5fb228 … memory [1d5fb228..216b65a8)
   quarantined` — **bug do fix anterior**: um tamanho estragado (0xFFFFFFF0)
   fez a walk do rebuild **rolar 32 bits** (b+size → abaixo da base do heap!),
   então a quarentena começou num endereço **fora do heap** e, como
   `QUARANTINED(p) = p >= g_poison_min`, **todo o heap** — incluindo TODA
   alocação futura — ficou "em quarentena".
4. 1.302× `heap: free of quarantined block … dropped` — todo `gfree` virou
   no-op; o topo (`g_top`) marchou de 549 MB até 1 GB em ~120 mil linhas.
5. `guest heap exhausted (1579440 bytes requested)` →
   `FATAL: out of guest memory for a 1579392 byte lock` — o lock do próximo
   frame do vídeo não achou memória. **Não era falta de memória: era o
   descarte de frees.**

### Correções (runtime/, um commit)

* **`runtime/heap.c`**:
  * quarentena agora é a faixa fechada `[g_poison_min, g_poison_max)` com
    `g_poison_max = g_top` no momento do rebuild — alocações feitas **depois**
    do rebuild são blocos normais e libertam de verdade;
  * a walk do rebuild usa aritmética 64-bit no passo (`b + sz > g_top` em
    `uint64_t`) e `block_sane` idem — tamanho estragado agora é detectado
    **no bloco em que está**, não depois de rolar para fora do heap;
  * `gfree` não devolve a cauda ao bump se o bloco cair na quarentena;
  * novo `gbigalloc(n,&va)`/`gbigfree(p,va)`: buffers **escritos pelo jogo**
    com ≥256 KiB vão para regiões de `VirtualAlloc` ([0x40000000,0x60000000),
    livro-razão no host): overrun/stale write cai em página zero, **nunca em
    metadados de alocador**; o arredondamento de 64 KiB da região serve de
    folga extra; se a região esgotar, cai para o heap (com canário).
* **`runtime/d3d9_bridge.cpp`**: `Shadow`/`Persist`/`BufShadow` guardam a
  bandeira `va`; todos os frees de shadow passam por `gbigfree`
  (`shadow_forget` no Release incluído).
* **`runtime/d3d9_bcemu.cpp`**: níveis BC (cópias DXT) idem via `BcLevel.va`.
* **`runtime/dsound.c`**: `DSData.va` — dados de buffers de som grandes idem
  (o `Lock` entrega ponteiro direto do dado ao jogo).
* **`runtime/main.c`**: preload do Bink usa `gbigalloc` (o código Bink
  emulado escreve nesse buffer — o mesmo código do overrun).

### Validação local

* `sh tools/test_heap.sh` — **ALL PASS**, agora com 2 cenários novos:
  * `test_wrap_quarantine` (exatamente o lahlog): tamanho 0xFFFFFFF1 no
    header + link insano → rebuild **sem enrolar**, quarentena com fim,
    `gfree` pós-rebuild liberta de verdade (`p3 == p2`), e 1.000 iterações
    de churn aloc/liberta estáveis (nada vaza para o topo);
  * `test_gbigalloc`: 1 MiB → região VA (reciclada first-fit), 1 KiB → heap.
* `gcc/g++ -fsyntax-only` sobre `heap.c`, `dsound.c`, `main.c`,
  `d3d9_bridge.cpp`, `d3d9_bcemu.cpp`: **rc=0**.

### O que esperar no próximo log

* O overrun do Bink (canário) **pode ainda ocorrer**, mas cai na folga da
  região VA: sem `CORRUPTION`, sem quarentena, sem exaustão — só o log
  `GUEST OVERRUN … tag=tex-rect` continua (informativo);
* Se houver corrupção de heap de novo: quarentena **limitada** ao intervalo
  `[min, topo-no-rebuild)`, o jogo continua e o heap segue utilizável;
* Boot mostra a linha nova `… bounded quarantine; game-writable buffers
  >=256 KiB on VirtualAlloc regions`.

## 2026-10-31 — sjx.log (build #33, fc43de0): morte ao aplicar vídeo no menu

Sintaxe do usuário: depois do loading, tela preta e o app fecha consumindo
toda a RAM. Log de 609 MB (log=2, trace de API).

O que o log mostra (linha: 11.209.576 → 16.683.281):

* Boot **inteiro funcionou**: splash ok, 33 módulos, `CreateDevice 1142x540`
  ok (`-> 00000000`), overlay de toque inicializou (`overlay: init ok window
  2285x1080 ... quads=75`, nenhum erro de D3D do overlay), e o jogo rodou
  **5,5 milhões de linhas de trace** (minutos de renderização = menu vivo).
  **O HUD não quebrou o boot do #30 — ele deixou o jogo chegar onde o bug
  latente aparecia.**
* A morte: ao entrar no menu o jogo re-aplica configurações de vídeo e chama
  `CreateDevice` de novo (1142x540 → 914x432, 40% de escala). O primeiro
  device ainda estava vivo; Android só concede **uma** VkSurface por janela
  nativa → `vkCreateAndroidSurfaceKHR: VK_ERROR_NATIVE_WINDOW_IN_USE` →
  `CreateDevice -> 8876086a` → o jogo fica em **loop infinito de retry**
  (cada tentativa reimprime o dump inteiro do DXVK) → log/RAM explodem.

Correções (commit "d3d9: one host device..."):

* `d3d9_bridge.cpp`: o bridge agora tem **um** host device por processo. O
  primeiro `CreateDevice` cria; os seguintes fazem `Reset` para o modo
  pedido e devolvem o mesmo wrapper. O AddRef próprio do bridge mantém o
  host vivo mesmo depois do Release do wrapper anterior (senão `com_forget`
  invalidaria o handle). Reset no mesmo objeto também preserva recursos,
  casando com o que o caminho SetRes da UE2 espera.
* `dispatch.c`: teto de ~96 MB por run no trace de API (o log=2 chegou a
  609 MB num único run; LOG() normal continua fluindo, só o trace cala).

## 2026-10-31 — HUD contextual, etapa 2: consciência de missão (estado real do jogo)

### Fundação medida no jogo real (reanálise da cópia GOG `94d1a40f…`)

* Exports extraídos com `pefile` (Core 2.675, Engine 7.497, SwatGame 1.505
  nomes) → `game_src/exports.json` (fora do repo). Âncoras usadas pelo HUD:
  `UObject::GObjObjects` = Core RVA `0x294354` (guest `0x01294354`),
  `ASwatPlayer::PrivateStaticClass` = SwatGame RVA `0xab848` (guest
  `0x032AB848`).
* Layout pinado por desassembly (capstone) do código real:
  `UObject::Class` em `+0x24` (`IsA`), `UObject::Name.Index` em `+0x20`
  (`HashObject`/`GetFullName`), `AActor::Location` em `+0x174`
  (`AActor::GetAimLocation` e `ASwatDoor::GetBreachAimLocation` usam o
  mesmo offset). `UClass` vive no heap guest (o valor do slot de
  `PrivateStaticClass` é ponteiro de heap), validado no código.
* Binds reais confirmados em `DefUser.ini` (F=IssueComplianceOrInteract,
  G=Grab, 1-0/F1-F10=CommandOrEquip, Espaço=GiveDefaultCommand,
  Botão direito=OpenGraphicCommandInterface, X=NextFireMode, Z=ToggleZoom,
  R=Reload, V=ToggleFlashlight). Motor de contexto do jogo mapeado em
  `PlayerInterface_Use.ini`/`PlayerInterface_Command_SP.ini` (trace de foco,
  Range=2000, portas Range=128). Tudo documentado em `docs/HUD_PLAN.md`.

### O que a etapa 2 entrega (código no runtime)

* **`runtime/hud_state.cpp/.h`** — leitor de estado real, sem SDL/D3D, sem
  alocação: caminha `GObjObjects`, casa `obj->Class` com a UClass de
  `ASwatPlayer` (comparação direta, sem nome), cacheia o pawn e a Location.
  Contexto: `UNKNOWN` (memória inválida: comportamento antigo), `MENU`
  (engine viva, sem pawn: menu/loading/debriefing), `MISSION` (pawn real).
  Escaneamento throttled (1 s), morte do pawn detectada em 1 tick (1 leitura),
  todos os ponteiros dinâmicos validados na faixa do heap guest
  (`[0x20000000,0x40000000)`) e contagem de objetos com teto (200 mil).
* **`touch_swat4.cpp`** — `MENU` ⇒ HUD some (fade suave, nenhum quad
  desenhado no fim) **e para de capturar toques** (o menu do jogo recebe
  tudo); transição para oculto solta todas as teclas/botões/stick pendentes
  (`ts_release_all`) para nada ficar latchado; `MISSION` ⇒ HUD volta.
  `hud.ini` ganha `always_show=1` (comportamento legado, escape honesto).
  Logs novos de campo: `overlay: mission context acquired (pawn=…)` e
  `overlay: mission context -> menu (hud hidden)`.

### Validação local

* `sh scripts/test_touch.sh` — **SELFTEST OK (0 falhas)**, agora com a seção
  14: imagem guest sintética de 4 GiB via `MAP_NORESERVE` cobrindo
  S1 (sem memória → UNKNOWN, HUD ativo), S2 (core não inicializado →
  UNKNOWN), S3 (engine viva sem pawn → MENU oculto, toque no FOGO não
  sintetiza nada nem consome o dedo), S4 (pawn aparece → MISSION em um
  tick; pawn e Location lidos de verdade), S5 (throttle: mudança dentro da
  janela não é vista; fora dela, rescan vê), S6 (objeto morto → MENU em um
  tick), S7 (virada para MENU durante FOGO pressionado solta o hold, toques
  passam direto, zero churn de teclado). **Este teste apanhou e corrigiu um
  defeito real do primeiro rascunho: a validação de cache confiava no objeto
  vivo mesmo removido do array (nunca voltaria ao menu); o array é a
  verdade, o cache é só caminho rápido.**
* Mesma suíte sob `-fsanitize=address,undefined`: **0 falhas, 0 vazamentos**.
* `g++` com os flags reais de build (DXVK native, C++17): `hud_state.cpp` e
  `touch_swat4.cpp` compilam limpos; contrato de símbolos verificado
  (`hud_*` C, `vinput_*` C puro).

### O que esperar no próximo log (aparelho)

* Boot: `overlay: hud.ini …` como antes; ao entrar no menu principal deve
  aparecer `overlay: mission context -> menu (hud hidden)` (o HUD deixa de
  flutuar sobre o menu e de injetar toques nele);
* Ao iniciar uma missão: `overlay: mission context acquired (pawn=xxxxxxxx)`;
* Ao voltar ao debriefing: `… -> menu` de novo, sem tecla presa.

## 2026-10-31 — HUD contextual, etapa 3: contexto de porta (proximidade real)

### Evidência nova

* `ASwatDoor::PrivateStaticClass` = SwatGame RVA `0xa6408` (guest
  `0x032A6408`) — export verificado. Portas são UObject com `Class` em
  `+0x24` e `Location` em `+0x174` (mesmos offsets já pinados).
* Faixa de porta: **128 unidades** — é o `Range=` que o próprio jogo usa nos
  contextos de porta do `PlayerInterface_Use.ini` (`[LockedDoor]`,
  `[ClosedDoor]`, etc.). O HUD usa a constante do jogo, não uma inventada.

### Código

* **`hud_state.cpp`**: o mesmo scan de 1 s agora também coleta até 32
  `ASwatDoor` (classe exata, posição lida uma vez — portas não andam).
  `hud_door_near(range)` mede a distância ao pawn **cada frame** usando a
  Location do pawn, que passou a ser renovada a cada tick (memcpy de 12
  bytes). O(32) por chamada, zero alocação.
* **`touch_swat4.cpp`**: 16º botão base `PORTA` (5,5%h de piso, como todos),
  no topo da coluna do USAR. Ação real: `DIK_F` = `IssueComplianceOrInteract`
  (bind real de `DefUser.ini`; `vk_of_dik` ganhou o F). **O chip é
  context-gated dos dois lados**: sem porta em 128 unidades ele não desenha
  (fade a 0, quads pulados) e o toque passa reto (vira dedo de look); com
  porta perto, desenha e dispara o F. Espelhamento (canhoto) e rebuild
  tratam o 16º botão como todos os outros (selftest 13 atualizado para 16
  botões sem sobreposição em 3 escalas × 3 proporções + notch).

### Validação local

* `sh scripts/test_touch.sh` — **241 asserts, 0 falhas**, incluindo:
  S4b (2 portas coletadas pelo mesmo scan; porta a 10 unidades →
  `hud_door_near` = 100.0 (10²); toque no PORTA → `DIK_F` down, tap de
  120 ms, down+up exatos nas edges), S8 (pawn sai de todas as portas → chip
  fecha **sem rescan** (Location renovada por tick) e o retângulo volta a
  ser área de look), e o S3/S4/S5/S6/S7 da etapa 2 continuam verdes.
* Mesma suíte sob ASan/UBSan: **0 falhas**; compilação com os flags reais
  (DXVK native, C++17) limpa.

### O que esperar no próximo log (aparelho)

* `overlay: mission pawn=xxxxxxxx doors=N` — N é o número real de portas do
  mapa lidas do array do engine (confirma a coleta em campo);
* Chegar perto de uma porta → chip PORTA aparece (fade ~120 ms); o toque
  dispara o mesmo caminho da tecla F física (menu contextual de porta do
  próprio jogo); afastar-se → some.

## 2026-11-01 — HUD Java sempre visível: todos os botões, todo o tempo

Pedido do usuário ("faça com que todos os botões seja mostrado a todos
tempo", confirmando o HUD Java sobreposto como a proposta certa).

### Mudança (`HudOverlay.java`, um arquivo)

O HUD completo — **17 controles: stick fixo, FOGO, CMD, SLOTS, USAR, PORTA,
REC, ZOOM, MODO, AGACH, CORRER, INC<, INC>, TAB, ESP, ESC, TROCA + grade
1-0/F1-F10** — passa a ser desenhado e a funcionar em **todos os contextos**:

* **Desenho**: `onDraw` não tem mais ramificação por contexto — stick +
  todos os botões em missão, menu, vídeo Bink e loading (o View Java é
  compostos pela sistema ACIMA da surface do jogo, então aparece sobre
  qualquer swapchain). Antes: MENU desenhava só o chip ESC e UNKNOWN não
  desenhava nada.
* **Toque**: `down()` testa botões ANTES de qualquer outra coisa, em todos
  os contextos. Dedo livre: missão = look; menu/boot = mouse do jogo
  (`EV_MMOVE`/`EV_MENUCLICK` nas coordenadas do cliente guest); stick =
  WASD na missão, setas no menu.
* **PORTA**: deixa de ser ocultado por proximidade — sempre visível,
  sempre ativo (F do jogo); `hud_door_near` agora só acende o realce azul
  quando há porta real em 128 unidades.
* **Chave `always_show` removida** do `hud.ini` (v2): o comportamento é
  incondicional; chave antiga em arquivo existente é ignorada no parse e
  não volta no rewrite.
* **Segurança**: `releaseAll()` agora roda em QUALQUER troca de contexto
  (antes só ao sair de missão) — nenhum hold (FOGO/CORRER/INC) sobrevive
  a missão→menu→vídeo.

### Validação local

* `ecj` (source 17) sobre `HudOverlay.java` + `HudBridge.java` com stubs
  Android mínimos: **0 erros**;
* `sh scripts/test_hud.sh` — `hud_jni selftest: ok`, `hud_state selftest:
  ok`, contrato de símbolos vinput: ok (runtime não mudou);
* Verificação de balanceamento e de referências órfãs (`cfgAlwaysShow`,
  `always_show`): limpa.

### O que esperar no aparelho

* Menu principal: TODOS os botões visíveis sobre o menu; tocar Career/
  Training direto com o dedo fora dos botões/stick funciona (mouse do
  jogo); ESC no canto superior direito; stick = setas para navegar.
* Vídeo de abertura: HUD inteiro visível; **ESC pula o vídeo** (Esc do
  próprio jogo), TROCA/TAB etc. ignorados pelo player — inofensivo.
* Missão: idêntico ao build anterior (stick, look, botões, grade), com
  PORTA sempre presente (realce azul perto de porta real).

## 2026-11-01 — glog (pastebin geT8Duxw): HUD input morto em menu/vídeo + abort FORTIFY no fechamento

Log do aparelho (motorola edge 30 fusion, build 58519e2): usuário parado nas
telas iniciais (vídeos), "clico em todos botões e ele não pula", e o app
fecha. Duas causas raiz distintas, ambas medidas no log.

### 1. Nenhum botão do HUD funcionava fora de missão — drain da ring condicionado a eventos SDL

`platform_present_hook` chamava `hud_pump()` (que drena a ring SPSC do HUD
Java) **dentro** do loop `while (SDL_PollEvent(&e))`. Com o overlay Java
consumindo 100% dos toques, a fila de eventos do SDL fica **vazia** exatamente
quando o usuário está em menu, vídeo ou missão parada — o corpo do loop não
roda, `hud_pump` não é chamado, e os intents (ESC do chip, clique do menu,
WASD do stick, look) se acumulam na ring **sem nunca chegar ao jogo**. Isso
explica em cadeia: vídeos não puláveis, Career/Training inclicável, "não sei
se o HUD está funcionando" — nada chegava.

**Correção**: `hud_pump()` uma vez por Present, FORA do loop de eventos
(mesma thread, mesmo contrato de single-thread). Custo: uma checagem de ring
vazia por frame.

### 2. "Ele tá fechando" — abort FORTIFY no teardown

Linha final do log:

```
1793464487.649 Surface::disconnect (GameActivity)
1793464487.757 F libc: FORTIFY: pthread_mutex_lock called on a destroyed mutex
```

Cadeia: saída → `exit(0)` no handler de SDL_QUIT → cadeia atexit → SDL_Quit
→ teardown de vídeo Android que trava mutex já destruído (`onDestroy` →
`nativeQuit` destrói `Android_ActivityMutex`; a SDL thread ainda pode bombá-lo
no caminho de saída). **Correção**: SDL_QUIT → `_exit(0)` (sem teardown; o
SO recupera tudo). Os outros caminhos de saída do runtime já usavam `_exit`
(`ExitProcess`, `TerminateProcess`, msvcrt `exit`, sinais).

### 3. Evidência de campo nova

A cada 300 frames o log passa a imprimir (só quando muda):

```
hud: N applied, M dropped, ctx C
```

`N` = intents aplicados na game thread (contador novo `hud_applied_events`),
`M` = drops por overflow, `C` = contexto (0 UNKNOWN / 1 MENU / 2 MISSION).
No próximo log do aparelho dá para PROVAR que o input do HUD chega: tocar
ESC no vídeo deve aumentar `N` na linha seguinte.

### Validação local

* `sh scripts/test_hud.sh` — `hud_jni selftest: ok`, `hud_state selftest:
  ok`, contrato vinput: ok;
* `gcc -fsyntax-only` runtime/*.c (flags reais): rc=0; `g++ -fsyntax-only`
  hud_state.cpp (DXVK native, C++17): rc=0.

### O que esperar no aparelho

* Vídeos: ESC (chip) e toque em qualquer lugar pulam (clique do jogo);
* Menu: toque direto nos itens funciona; stick = setas;
* Missão: stick/botões/look respondem de imediato;
* Fechar o jogo (voltar/sair) sem abort — processo encerra limpo;
* log.txt: linhas `hud: N applied…` crescendo conforme o HUD é usado.

## 2026-11-01 — iutlog (build #44): app fecha no SetRes após os vídeos + toque para pular vídeos

Log do aparelho (iutlog.zip, build dded5c1): os três vídeos de abertura
(SierraLogo.bik, Nvidia.bik, Swat4.bik) tocaram por ~115 s e, ao terminar o
último, `Enter SetRes: 1142x540` (volta pro menu) → `CreateDevice` → abort.

### 1. Crash real: a Release do jogo destruiu o device compartilhado

Sequência no log: 3× `CreateDevice` (1142x540 fresh → 914x432 reuse+reset →
1142x540 reuse+reset). A UE2 faz SetRes assim: **Release do device + novo
CreateDevice** para cada mudança de resolução. O bridge mantém UM device
hospedeiro (915efb0, o Android só concede um VkSurface por janela), mas o
caminho `reuse+reset` devolvia o MESMO wrapper sem conceder a referência COM
que o contrato de `CreateDevice` garante ao chamador. Contagem: 2 → 1 →
(no reset dos filmes) 1 → (Release do jogo antes do 3º) **0 → device
destruído** → o 3º `CreateDevice(reuse+reset)` rodou `Reset` em objeto
liberado → exceção dentro do dxvk (`D3D9CommonTexture`/`CreateBackBuffers`)
→ unwinding destruiu o `DxvkDevice` já morto → `waitForIdle` →
**`FORTIFY: pthread_mutex_lock called on a destroyed mutex`** → abort.

**Correção** (`d3d9_bridge.cpp`): o caminho reuse+reset faz `AddRef()` ao
conceder o wrapper (balanceia a Release do chamador) e só devolve wrapper
em caso de sucesso — espelhando o caminho fresh. Além disso, `CreateDevice`,
`Reset` e `Present` agora passam por um guard de exceção: qualquer falha
interna do dxvk vira `D3DERR_DRIVERINTERNALERROR`/`D3DERR_DEVICELOST`
(caminho de recuperação que a UE2 já conhece) em vez de terminar o processo.

### 2. Toque pula vídeos (canal direto na WndProc)

O build #44 fez os intents chegarem (`hud: N applied` cresce durante os
vídeos no iutlog), mas o loop de vídeo **não bombeia a fila de mensagens**
— os WM_KEYDOWN/WM_LBUTTONDOWN ficavam na fila sem leitor. Canal novo:
enquanto um .bik está aberto (`kernel32.c` conta os arquivos abertos,
`hud_movie_playing()`), o `vinput_key_tap` entrega o WM **diretamente na
WndProc do jogo** (`user32_key_direct`, semântica SendMessage, mesma thread,
fora de qualquer dxvk call). O HUD Java transforma o toque livre em ESC
(`HudOverlay`: poll bit 9 = filme; latched `everMenu` impede que o vídeo de
fundo do menu perca os cliques reais). ESC chip (K_KTAP) usa o mesmo canal.

### 3. Overruns de textura do Bink (cosmético, contido)

`d3d9: GUEST OVERRUN ... tex-rect 914x432` durante os vídeos: o Bink escreve
≥17 bytes além da região travada (slack 16 + canary 32 aguentam a detecção;
`gbigalloc` isola por página, o heap do guest não é corrompido). Sem ação
nesta rodada; registrar caso um dia apareça como artefato visual.

### Validação local

* `sh scripts/test_hud.sh` — vinput/user32/kernel32/hud_jni/hud_state
  compilam, `hud_jni selftest: ok`, `hud_state selftest: ok`;
* ecj (-source 17) + stubs Android mínimos: HudOverlay.java +
  HudBridge.java — 0 erros (stubs recriados em scripts/stubs/).

### O que esperar no aparelho

* Os três vídeos tocam; um toque em qualquer área livre (ou o chip ESC)
  agora tenta pulá-los pelo canal direto — se este build do motor não
  consultar input no loop de vídeo, eles simplesmente correm até o fim
  (~90 s, uma vez por boot) e o app **não fecha mais**;
* Menu volta em 1142x540 sem abort; cliques e stick funcionam;
* Se algum reset falhar de verdade, o jogo recebe um D3DERR tratável
  (retry do SetRes) em vez de fechar.

## 2026-11-01 — HUD morto no menu: foco do guest + fila W + cliques WM dos botões

Evidência: vídeo `screen-20261031-183211.mp4` (release tag `Log`) — menu
principal visível, HUD desenhado, cursor do jogo **congelado em (0,0)**,
toques em todos os botões sem efeito. O cursor em (0,0) é exatamente o
estado inicial do cursor virtual (`vinput`), provando que NENHUM input de
menu chegou ao guest. Quatro defeitos independentes, todos corrigidos:

### 1. Foco: o guest acreditava estar INATIVO para sempre

`GetFocus` devolvia 0 e `GetForegroundWindow`/`GetActiveWindow` devolviam
um HWND falso (0x00010010) que **nunca coincide** com as janelas criadas
pelo jogo (00010001/00010005/00010009/0001000d/00010011 no log de campo).
UE2 decide aceitar mouse/teclado comparando o foco com a própria janela;
com o teste sempre falso, a GUI ignorava tudo. Além disso nenhum
WM_ACTIVATE/WM_SETFOCUS era entregue (Win32 real envia os dois após
WM_CREATE). Agora: os três retornam a janela principal
(`user32_main_hwnd`) e o par de ativação é entregue nas versões A e W de
`CreateWindowEx`.

### 2. A fila de mensagens W era um poço sem fundo

A camada de janela do SWAT4 é a build **W** (só `CreateWindowExW` no log).
O pump do guest chama o par W — e `PeekMessageW` devolvia "sem mensagens"
para sempre, enquanto `PeekMessageA` (com a fila) nunca era chamado. Todo
WM enfileirado (setas do stick, ESC, cliques) jamais despachava. Agora A
e W drenam a MESMA fila; `GetMessageA/W` retornam -1 (0 seria WM_QUIT e
poderia encerrar o loop do jogo).

### 3. Botões do HUD não entregavam clique de mouse de verdade

FOGO/CMD/USAR só ligavam o bit do DirectInput — a GUI não lê DI, lê
mensagens WM na posição do cursor. Agora, fora de missão (menu/vídeo/
boot), MBTN entrega o par WM (0x201/0x202, 0x204/0x205, 0x207/0x208) na
posição atual do cursor via `menu_click_edge` (fonte única de borda,
espelha DI também); em missão continua só DI (o WM dispararia
IK_LeftMouse duas vezes). Toque livre (R_MENU) segue como mouse do menu
(WM_MOUSEMOVE + MENUCLICK). Cursor nunca coloca WM antes da 1ª posição
real (y>=0).

### 4. Import sem shim no caminho do mouse = crash latente

`GetCapture` e `GetCursor` não tinham shim: chamá-los é `rt_fatal`
(imediato) — e o caminho de captura do mouse é justamente o próximo a ser
executado quando o menu começa a responder. Shims adicionados
(GetCapture→0 = sem captura, GetCursor→handle fake, GetMessagePos→cursor
virtual empacotado).

### Diagnóstico de campo (próximo log responde onde param)

* `msg pump: first WM_MOUSEMOVE/WM_*BUTTONDOWN/WM_KEYDOWN delivered` —
  primeira entrega de cada classe (prova fila→pump→WndProc);
* `hud: N applied, M dropped, ctx C, wm X mm/Y click/Z key dispatched` —
  a cada 300 frames, com contadores da bomba;
* `hud: ctx A -> B (gen G pawn P doors D entry E)` — virada de contexto
  com a evidência do scan (o rampa VA 0x40000000-0x60000000 do commit
  925d813 agora alimenta o `everMenu` do Java).

### Validação local

* `sh scripts/test_hud.sh` — tudo ok (vinput/user32/swat4_shims/
  platform_sdl compilam; selftests do anel e do scan passam);
* novo teste de comportamento `scripts/behave/vinput_behav.c`: toques de
  botão em menu entregam WM_MOUSEMOVE/LBUTTONDOWN/LBUTTONUP + espelho DI,
  dedupe de borda, contexto de missão não gera WM, cursor não colocado não
  gera WM, EV_MENUCLICK e botão 0 compartilham a mesma borda.

## 2026-11-01 — Causa raiz do "só o Esq funciona": o mouse do motor só
existe no stream bufferizado do DirectInput (agora implementado)

### O que o vídeo screen-20261031-205831 mostra

18 s no menu principal: o cursor do jogo fica parado, as dicas dos itens
mudam de posição (navegação por setas do stick — teclado WM funciona) e
nenhum toque em botão do HUD produz reação. O usuário perguntou: "se tiver
suporte a xinput ou dinput, melhor mudar para virtual gamepad".

### Investigação no código recompilado do jogo (gen/)

* `DirectInput8Create` aparece **uma** vez no log de campo (iutlog) e o shim
  registrou **"DirectInput: created mouse"** — nenhum dispositivo DI de
  teclado foi criado.
* windrv recompilado (`upload/gen/windrv_00.c`): o poll de input do motor
  (1) lê o teclado do array de estado preenchido por WM_KEYDOWN/UP (loop de
  256 entradas -> `f_21011D6`), e (2) lê o **mouse exclusivamente** do
  stream bufferizado: `GetDeviceData(20, &um, &count, 0)` em laço com
  re-Acquire, cada evento -> `f_21010E1(dwOfs, dwData)` que mapeia
  DIMOFS_X/Y (0/4) para os eixos e DIMOFS_BUTTON0..2 (12..14) para
  IK_LeftMouse/IK_RightMouse/IK_MiddleMouse. Não há GetDeviceState do
  dispositivo mouse (a única chamada +0x24 é a do teclado, 0x110 bytes).
* Nosso `dev_GetDeviceData` só implementava dados bufferizados de
  **teclado**: o dispositivo mouse devolvia **zero eventos** para sempre.
  Resultado: teclado OK (stick/setas navegam menus), mouse morto em todos
  os contextos — exatamente "só o Esq funciona".
* XInput: `SWAT4.exe` (2005) não importa XINPUT1_3.dll — nenhuma
  LoadLibrary de XINPUT no log de campo. Os shims XInput existentes
  (input.c) são para outros jogos do runtime. Virtual gamepad via XInput
  seria invisível para este jogo; via DInput joystick o motor enumera pads
  mas só cria teclado/mouse, e os menus do SWAT 4 são mouse/teclado.
  **Conclusão: o caminho nativo correto é o mouse DI bufferizado** — que é
  o que os menus realmente consomem.

### Correções

1. `runtime/dinput.c` — `dev_GetDeviceData` do mouse: stream bufferizado
   real (FIFO por dispositivo): bordas de botão 0x80/0x00 em
   DIMOFS_BUTTON0..2 = 12..14, deltas relativos coalescidos em DIMOFS_X/Y =
   0/4, carimbos SDL_GetTicks e sequência; cap respeitado (o laço do motor
   pede 1 evento por chamada); DIGDD_PEEK enxerga mas não drena; absorção
   de transições acontece a cada chamada (um dispositivo real registra
   quando acontecem). Log único "DI mouse buffered: first event ofs=…"
   para o próximo log de campo provar o canal.
2. `runtime/dinput.c` — `dev_GetDeviceState` do mouse: só botões; deltas
   zerados e **sem consumir** `vinput_mouse_take` (o único consumidor dos
   deltas agora é o stream bufferizado; consumir nos dois roubava eventos).
3. `runtime/vinput.c` — `vinput_mouse_button` em contexto de menu: um
   clique **antes** de qualquer movimento do dedo de menu posiciona o
   cursor no centro do guest (um mouse real está sempre em algum lugar) em
   vez de descartar o clique (s_cur_y<0 derrubava os primeiros toques).

### Validação local

* `sh scripts/test_hud.sh` — tudo verde, agora incluindo o teste
  comportamental `scripts/behave/vinput_behav.c` (11 blocos): toque de menu
  cai no centro quando o cursor nunca foi colocado; dedupe de bordas; move
  de menu = WM_MOUSEMOVE absoluto com dedupe; missão = só DI; tecla =
  estado DIK + WM na fila; filme aberto = WM direto no WndProc; stream DI:
  bordas 12: 0x80->0x00, deltas coalescidos 0/4 com sinal, ordem FIFO sob
  cap=1, PEEK enxerga sem drenar, GetDeviceState não rouba deltas.
* Os shims de janela/mouse (WM pair) continuam como estavam: um mouse real
  entrega WM **e** DI — o motor trata os dois (Press duplicado no mesmo
  tick é no-op no estado de input).

## 2026-11-01 — Clique morto no menu, parte 2: o cursor da GUI nunca recebeu
deltas — steering em malha fechada pelo stream DI (CRIADOR 2 rodadas + CRÍTICO 3 rodadas, 9.6/10)

### O que o log shhxlog + vídeo screen-20261101-062041 provaram

O build anterior já entregava o clique ao guest por DOIS canais — WM pair
(31 WM_MOUSEMOVE + 31 WM clicks despachados) e evento DI bufferizado
(`DI mouse buffered: first event ofs=12 data=0080` consumido pela engine) —
e o menu continuou morto. Motivo, lido no windrv recompilado
(`upload/gen/windrv_00.c`, f_21010E1): o cursor da GUI só se move pelos
eixos IK_MouseX/IK_MouseY gerados dos deltas DIMOFS_X/Y (multiplicador
guardado para 1.0 no próprio código do motor; Y negado; delta assinado
int32). O canal WM é ignorado pela camada de input. Como o caminho de menu
nunca gerou deltas (e `SDL_HINT_TOUCH_MOUSE_EVENTS=0` garante que o SDL
também não), o cursor ficou congelado onde começou e todo clique caiu onde
não há controle — teclado OK (ESC abriu "Quit to Windows?"), mouse morto.

### Correção — steering em malha fechada (vinput.c)

* Modelo do cursor do motor (`s_eng_mx/my`, init 0,0 — posição inicial
  observada em campo) e `menu_steer(x,y)`: empurra `(alvo - modelo)` pelo
  MESMO acumulador de deltas que o stream DI consome; modelo absoluto
  atualizado no steer (idempotente: re-steer no mesmo alvo empurra zero).
* Premissa documentada: o cursor da GUI só se move com menus ativos
  (execMouseEmulation é nativo chamado por script da UGUIController; a
  interação da GUI não roda em missão) — look de missão NÃO contamina o
  modelo; o primeiro steer pós-missão re-anciona da última posição de menu.
  Falsificadores em campo: one-shot `menu steer: first delta ...` e campo
  `cur %d,%d steers %u` na linha periódica `hud:`.
* Ordem no poll (BLOCKER pego pelo CRÍTICO): um toque posta MMOVE+MENUCLICK
  no mesmo present hook e o poll do windrv roda no tick seguinte — deltas e
  borda pendem juntos num GetDeviceData. `dev_GetDeviceData` agora emite
  DELTAS ANTES das bordas de botão (ordem cronológica real: o dedo move
  antes de apertar); sem isso o clique disparava no cursor velho dentro do
  mesmo poll. Testes 7c/7e re-pinados em [delta..., borda] e 8d fixa a
  forma exata do toque num poll: [X(l/2), Y(a/2), 12/0x80].
* FIFO DI 32->64 entradas (mev_n uint16) + LOG one-shot se transbordar
  (delta caído dessincroniza modelo vs motor); one-shot agora registra os
  TRÊS primeiros eventos lidos (prova deltas-lideram em campo).
* GetCursorPos/GetMessagePos/WM continuam como estavam (canais paralelos
  que um mouse real também entrega).

### Validação

* `sh scripts/test_hud.sh` verde: selftests do anel e do scan + 17 blocos
  comportamentais (steering idempotente, re-ancoragem pós-missão, fallback
  do centro com steering, forma do toque num poll, PEEK, FIFO cap=1).
* Cadeia de qualidade: CRIADOR (implementação) -> CRÍTICO r1 6.5 REJEITADO
  (BLOCKER ordem no poll) -> CRIADOR r2 -> CRÍTICO r2 9.2 (premissa do
  modelo em aberto) -> verificação nos fontes decompilados (f_21010E1:
  multiplicador 1.0, delta assinado, Y negado; execMouseEmulation via
  binding de script) -> CRÍTICO r3 **9.6 PASSOU**.

### O que esperar no aparelho

* Menu: tocar um item move o cursor da GUI até o dedo (stream DI) e o
  clique cai no controle — itens do menu principal, Yes/No do "Quit to
  Windows?", Settings etc.
* Se após uma missão o menu clicar com desvio constante, o log mostrará
  `steers` subindo com `cur` nos alvos — premissa falsada; a correção é
  o modelo track-all + clamp (uma linha de inversão, já planejada).

## 2026-11-01 — shhxlog: cada toque virava Esc no menu (nenhum modo entrava) + D-pad do teclado

Pedido do usuário: "Cada clique o deslize na tela a ação tá sendo executada
como Esc ao invés de... mover o mouse... e ao o mouse tá em cima de um opção
exemplo carrear e clica na tela ele executa o clique do mouse como se fosse
em um Pc" + "sinto falta daqueles botões cima baixo esquerda e direita no hud".

### Causa raiz (prova no shhxlog, build 8c5483a)

* `hud: ctx -1 -> 0 (gen 7 pawn 0 doors 0 entry 0)` e o log TERMINA no menu
  principal: o scan de objetos saiu UNKNOWN a sessão inteira — bases de
  módulo corretas (Core 0x01000000, SwatGame 0x03200000 no próprio log),
  alocações todas na banda 0x4x-0x5x (aceita desde 925d813) — o motivo do
  bail era invisível (bail silencioso).
* Com scan morto, `everMenu` nunca trava; o vídeo de fundo do menu mantém
  MOVIE_BIT=1; o ramo `movie && !everMenu` do `down()` then converte CADA
  toque livre na tecla de pular vídeo (ESC). O maquinário do menu-mouse
  (steer DI + WM, 8c5483a/c505e70) nunca chega a rodar.

### Correção (processo multiagente pedido pelo usuário: 1 agente de correção Java + 2 críticos r1 + integração)

1. **Trava pela bomba de mensagens (runtime, independe do scan)**:
   `user32.c` trava `g_ever_pumped` no primeiro PeekMessageA/W do guest
   (menus/missões bombeiam a cada frame; o loop Bink NUNCA bombeia — bnlog
   iutlog). `hud_jni.c poll()` expõe bit 10 = `movie && pumped` = menu
   rodando atrás do vídeo de fundo. Java trava `everMenu` em CTX_MENU OU
   GUI_BIT (HudBridge.GUI_BIT) nos DOIS pontos de poll. Depois da trava,
   dedo livre = mouse do jogo (EV_MMOVE arrasto + EV_MENUCLICK toque) em
   TODOS os contextos não-missão — mesmo com o scan morto.
2. **Diagnóstico do bail silencioso**: `hud_scan_diag()` (hud_state.h/.cpp)
   registra código (0 ok, 1 sem memória, 2 slot de classe, 3 num, 4 data,
   5 range) + valores crus (cls/num/data); platform_sdl.c loga one-shot por
   estado: `hud scan: bail %u cls %08x num %u data %08x`. O próximo log de
   campo finalmente mostra POR QUE o scan não sai do UNKNOWN (necessário
   para look vs mouse em missão e proximidade de porta).
3. **D-pad (cima/baixo/esquerda/direita)**: 4 botões novos (K_ARROW,
   DIK_UP/DOWN/LEFT/RIGHT), semicírculo em cruz acima dos botões de
   inclinação, espelhado no modo canhoto, glifos em Path pré-alocado
   (sem dependência de fonte), manter pressionado repete a cada 280 ms
   (par keyup+keydown WM, mesmo cadência do seta-do-stick no menu),
   soltar solta a tecla. Visíveis e ativos em todos os contextos.
   O cluster ENCOLHE antes de sair da tela (crítico r1 Java: em scale 2.0
   o B_UP ficava em y=-186 e nunca podia ser tocado; agora ancora em
   safeTop).

### Validação

* `sh scripts/test_hud.sh`: hud_jni selftest ok, hud_state selftest ok,
  17 blocos behav ok (vinput/dinput, incluindo skip de vídeo via
  user32_key_direct intacto).
* `scripts/check_java.sh` (ecj + stubs): HudBridge+HudOverlay, 0 erros.
* Crítica: agente r1 runtime 8/10 (nada bloqueante; correção do código 5 do
  diag aplicada); agente r1 Java 8/10 (1 bloqueador: clipping do D-pad em
  scale>1.55 — corrigido com encolhimento do cluster; verificado à mão em
  scale 1.0/1.5/2.0: by[B_UP] nunca < safeTop, sem overlap com INC).
* A verificação de campo fica por conta de duas linhas novas no log:
  `user32: first message pump - GUI/game loop is live` (deve aparecer no
  menu, NÃO durante os vídeos) e `hud scan: bail ...`.

### O que esperar no aparelho

* Menu: arrastar o dedo move o cursor da GUI (stream DI, c505e70) e o
  toque clica onde o dedo está — Career/Instant Action/etc como num PC.
* Tocar durante os vídeos de abertura continua pulando (a trava só sobe
  com a bomba de mensagens; os vídeos não bombeiam).
* D-pad: navega qualquer lista/foco de menu com setas reais do teclado.
* Missão: dedo livre continua olhar (R_LOOK); o D-pad envia as setas
  (sem bind em missão, inofensivo).

## 2026-11-01 — njdjport: fecha ao entrar no modo Carreira (SP-FoodWall) + texto destacado "meio branco"

### Análise do log (112 MB) e do vídeo

* **Crash na entrada da Carreira**: o jogo carrega SP-FoodWall, cria o mundo
  físico do Havok, enumera 3 ragdolls (`..\HavokData\*.hke` — SWATofficer_rd,
  PaperTarget_rd, HotelSuitBag_rd) e morre com `FATAL: no recompiled code at
  00000010 (return address 0173b9eb)`. Backtrace: Engine.dll+0x33B810 (dispatch
  de criadores via `[tabela + (linha*32+col)*4 + 0x18c]` — importador de texto
  do Havok, chamado por `HkLevelData::readFromFile`), correntes 0x34F080 /
  0x33BE00 / 0x32B120 / 0x32D090 / 0x314B80 (RB_COLLECTION) / 0x312C80 /
  0x30FFD0 (WORLD/BEGIN_WORLD).
* **Causa raiz**: logo antes do crash o heap detecta `CORRUPTION #1` — o header
  do bloco em 20848d18 foi sobrescrito com floats do jogo (0.2f, 10.0f,
  -152.5f, 48.57f...). O ring mostra que o último evento foi `alloc 20846c90
  size=8328` (8328 = 694 × FVector) e 20848d18 é exatamente o fim desse bloco:
  **o convidado estourou o fim de uma alocação de 8328 bytes com dados
  analisados do ragdoll e destruiu o header vizinho + objetos vivos**; a
  quarentena salvou o alocador, mas o objeto corrompido morreu no virtual call.
  O .mopp nunca chegou a ser aberto (o crash é antes da leitura do arquivo).
* **"Meio branco"**: no vídeo, o item selecionado do menu ("TRAINING",
  "SETTINGS") renderiza parte branco/parte amarelo com corte vertical
  no meio da palavra, e no loading o texto "LOADING" só mostra o contorno
  (preenchimento ausente). O menu roda a **30 FPS** (300 frames por 10 s no
  heartbeat do log), então não é lentidão de quadros: é o preenchimento
  destacado sendo desenhado com textura/estado errado (varredura travada).
  Nesta rodada entram diagnósticos dirigidos (abaixo) para fechar a causa na
  próxima coleta.

### Correções

* **Contenção de overrun no heap (runtime/heap.c)** — o mesmo remédio que o
  alocador do Windows dá sem querer: alocações de 512+ bytes ganham folga de
  cauda (arredondamento 64 B + registro de 16 B {magic, n pedido, magic,
  magic} no fim). Um overrun pequeno cai na folga e **não destrói mais o
  header vizinho**; `gsize`/`HeapSize` continuam reportando o tamanho pedido;
  no `gfree` o registro é verificado e o estouro é reportado com a identidade
  do bloco (`heap: OVERRUN block ... req=...`). Registro com magic destruído =
  bloco vazado (extensão desconhecida) + relatório; o alocador segue são.
* **Diagnóstico do bad call (runtime/dispatch.c)**: quando o alvo é minúsculo
  (<0x10000), despeja registradores + a janela da tabela ao redor de
  `[ecx + esi*4 + 0x18c]` (limitado a 8 despejos) — a próxima ocorrência mostra
  o estado do registro do Havok inteiro no log.
* **Rastro de stdio (runtime/msvcrt.c)**: fopen/_wfopen/fread com caminho,
  modo e resultado (limitado) — a leitura dos .hke era invisível no log (só o
  rastro Win32 aparecia).
* **Rastro de textura (d3d9_bridge/bcemu)**: CreateTexture com formato,
  tamanho, uso e pool (64 primeiras + todas as falhas) e LockRect de texturas
  pequenas (48 primeiras) — revela como as páginas de fonte e o gradiente
  destacado são carregados/atualizados.

### Validação

* `sh tools/test_heap.sh`: 2 cenários novos — `test_overrun_containment`
  (reproduz o overrun de 8328 B + 24 B de floats: contido, sem corrupção,
  reportado, heap vivo) e `test_overrun_huge` (registro destruído: bloco
  vazado, alocador são). **ALL PASS**, incl. stress multi-thread.
* `sh scripts/test_hud.sh`: hud_jni ok, hud_state ok, 17 blocos behav ok.
* Sintaxe: gcc/g++ `-fsyntax-only` limpos em heap.c, dispatch.c, msvcrt.c,
  d3d9_bcemu.cpp, d3d9_bridge.cpp.

### O que esperar no aparelho

* **O modo Carreira não deve mais fechar ao carregar** — o overrun cai na
  folga; se o jogo depender dos bytes que estouraram (não deve), o comportamento
  será visível de outra forma e o log dirá exatamente que bloco estourou.
* No log: `heap: OVERRUN ...` identifica o estourador pela combinação
  endereço+tamanho (o ring mostra a vizinhança); `fopen/fread` mostram os
  .hke lidos; `d3d9: CreateTexture/lock` mostram as páginas de fonte.

## 2026-11-01 — Ícones do HUD (swat4-hud-icons.zip do usuário): 42 PNGs no lugar dos rótulos

### O que chegou

* O usuário gerou o pacote com `docs/PROMPT_ICONES_HUD.md` e enviou
  `swat4-hud-icons.zip` (42 PNGs + `manifest.md`; a tabela do prompt especifica
  42 arquivos, não 36 — o manifesto registra a diferença).
* Todos os 42 foram validados por script local (`scripts/check_icons.py`,
  decodificador PNG próprio): dimensão certa (`fogo.png` 192×192, resto
  128×128), RGBA 8-bit, bordas 100% transparentes, conteúdo não vazio e
  **só as duas cores do estilo** (`#F2F4F6` traço / `#E8A33D` acento,
  tolerância de anti-aliasing ≤1%). `42 ok, 0 fail`. Conferência visual em
  folha de contato sobre fundo escuro: pares `incl`/`incr` espelhados de
  verdade, `zoom` ≠ `fogo`, numerais dos `slot_*` e `fx_*` legíveis.

### Integração (android/app/src/main/java/.../HudOverlay.java)

* PNGs empacotados no APK em `assets/hudicons/` (212 KB, sem subpastas) e
  `manifest.md` virou `docs/MANIFEST_ICONES_HUD.md`.
* Os bitmaps são decodificados **uma única vez no construtor** da View:
  primeiro `<pasta do jogo>/hudicons/*.png` (o usuário troca o set no
  aparelho sem recompilar), depois os assets do APK; PNG ausente/corrompido
  mantém o rótulo de texto daquele botão — degradação por botão, nunca total.
  `decodeOne()` nunca lança (IOException/RuntimeException → null).
* Desenho: `drawBtn()` recebe o bitmap e o desenha com `drawBitmap(null-src,
  dst pré-alocado, Paint FILTER_BITMAP_FLAG)` a 82% do botão (os PNGs já
  carregam a área segura de 96/128 do estilo); alpha acompanha a opacidade do
  HUD. Os quatro `dpad_*.png` substituem os chevrons desenhados (que continuam
  como fallback se o PNG faltar). A grade usa `slot_1..slot_0` na página
  numérica, `fx_1..fx_10` na página F e `toggle_fx`/`toggle_num` como chips de
  página. Zero alocação por frame preservado (só `setAlpha` + dst reutilizado).
* `labels=0` continua desenhando as caixas vazias (comportamento anterior
  mantido); `labels=1` agora mostra ícone, caindo para texto quando o PNG
  não existe. README.md atualizado.

### Validação

* `ecj` 3.36.0 `-source 17` + stubs Android (`scripts/stubs/`, recriados):
  `HudOverlay.java` + `HudBridge.java` compilam com **0 erros** (2 warnings
  pré-existentes: `DIK_0` e `Btn.id` não usados — documentação de tabela).
* `bash scripts/test_hud.sh`: hud_jni ok, hud_state ok, 17 blocos behav ok
  (rc=0).
* Sem aparelho Android: o desenho real dos bitmaps no Canvas só é exercitado
  no CI/aparelho.

### O que esperar no aparelho

* Todos os botões (e a grade aberta) mostram os ícones táticos; ESC/TROCA no
  topo, FOGO maior no canto, PORTA com acento azul perto de porta (o acento é
  no contorno, não no PNG), D-pad com chevrons duplos âmbar.
* Para trocar um ícone sem recompilar: coloque o PNG em
  `<pasta do jogo>/hudicons/<nome>.png` (nomes em `docs/MANIFEST_ICONES_HUD.md`).
* Esta build também carrega a correção da Carreira (heap com folga de cauda)
  e os diagnósticos do "meio branco" — o próximo log fecha essa causa.

## 2026-11-01 — shhxpott: a Carreira ainda fecha (ou fica carregando infinito) — o escritor Fantasma vai ser nomeado

### Análise do log (112 MB, 3,0 M linhas) + vídeo

* Build testada: a dos ícones (a faixa de horário do aparelho, UTC-3, bate:
  log 13:05 = 16:05 UTC, zip subido 16:12). O crash é o MESMO do njdjport:
  `LoadMap: SP-FoodWall` (Browse, linha 3028101) → mundo físico do Havok →
  morte no dispatch do importador (`f_173B590+0x13c`, SIGSEGV code 2 = ACCERR
  em MEM+0x14 — leitura através de vtbl lixo do objeto 0x21369260).
* **Novo retrato da corrupção** (o heap endurecido flagrou 2 vezes e seguiu):
  - CORRUPTION #1: os LINKS de um bloco LIVRE de 2,2 MB (0x210a4488,
    size 0x236420) viraram lixo; a quarentena isolou [212da8a8..21366de8) —
    575 KB — e o dump mostra dados de alta entropia (conteúdo .hke) por cima
    de headers vivos.
  - CORRUPTION #2: header em 0x210af508 virou o padrão repetido `01ff0901`;
    segunda quarentena. O ring mostra que o bloco 8328 (payload 210a4490 —
    os FVectors do parse, iguais ao njdjport) foi alocado DENTRO do envelope
    que o escritor continua preenchendo.
  - O escritor joga ~2,9 MB de dados sequenciais ATRAVESSANDO headers de
    blocos (212da8a8 ERA um header) — não é overrun pequeno (a folga de cauda
    não pega; nenhuma linha `heap: OVERRUN` no log inteiro), não é LockRect
    (as 8 violações de canário d3d9 são todas de UMA textura tex-rect de
    2,4 MB em região VA, inofensivas ao heap) e não é fread/ReadFile (shims
    copiam exatamente n bytes; 32 freads de 256 B no log).
* Conclusão: contenção NÃO resolve esta classe (os vizinhos vivos morrem do
  mesmo jeito) e o log atual não nomeia o escritor. Também corrigidos de
  passagem dois desvios reais encontrados na auditoria do caminho.

### Correções

* **Writer trap (runtime/heap.c)** — depois da PRIMEIRA corrupção o heap
  entra em modo paranoico: a quarentena `[poison_min, poison_max)` e toda a
  fronteira de bump `[g_top, 0x40000000)` viram `PROT_READ` (mprotect). O
  próximo write falso do convidado é um SIGSEGV ACCERR cujo dump (já
  existente) imprime RIP + registradores + backtrace — **o próximo log nomeia
  o culpado de verdade**. Páginas entregues pelo bump voltam a ser RW
  (um mprotect por alocação, só no modo paranoico — o boot nunca paga nada:
  ele não corrompe). Frees de bloco do topo devolvem a faixa para RO.
* **Header invalidado no tail-return (runtime/heap.c)** — o free do bloco do
  topo zerava g_top sem invalidar o header: um free atrasado do MESMO ponteiro
  (janela pré-bump) passava no `block_sane` e relinkava o que viesse depois.
  Agora o header é zerado e o free atrasado é reportado (`bad free pointer`).
  (Depois que o bump reusa o MESMO header, free atrasado e free legítimo são
  indistinguíveis por endereço — o heap do Windows tem o mesmo buraco; quem
  pega isso é o writer trap.)
* **GMalloc guard: Realloc de ponteiro estranho COPIA o conteúdo**
  (runtime/swat4_shims.c) — o atalho "os chamadores sobrescrevem tudo" vale
  para FString, mas um buffer de stream que continua anexando no offset antigo
  (o leitor .hke) perdia tudo o que já tinha lido a cada Realloc estranho.
  Agora copia min(n, gsize(p)) para ponteiros do heap do runtime e min(n, 64 KB)
  para os demais (imagens/stacks/VA: tudo mapeado).

### Validação

* `bash tools/test_heap.sh`: **ALL PASS**, com 2 cenários novos —
  `test_stale_free_tail_return` (free atrasado na janela pré-bump é
  reportado; a faixa devolvida é entregue de novo RW) e `test_paranoid_trap`
  (fork: write em página em quarentena morre com SIGSEGV; alocação pós-rebuild
  continua funcionando e gravável).
* `gcc -fsyntax-only` em runtime/*.c com -I/tmp/sdlinc: rc=0.
* `bash scripts/test_hud.sh`: rc=0 (intacto).

### O que esperar no aparelho

* Se a corrupção acontecer de novo (provável), o jogo MORRE MAIS CEDO — na
  primeira escrita fantasma — e o log traz `CRASH: signal 11 code 2 at
  address …` com o RIP dentro de libmain.so (`f_XXXXXXX+N`) apontando o
  escritor. Esse é o objetivo desta rodada: trocar a morte confusa do dispatch
  pela IDENTIDADE do culpado; a rodada seguinte corrige o caminho nomeado.
* Se o Realloc-estrangeiro do leitor .hke era o gatilho, a cópia nova pode já
  mudar o comportamento (o parse passa a ter os dados que o jogo espera).
* O "carregando infinito" e o "fecha" são o mesmo escritor: com o trap, os
  dois casos terminam no mesmo dump nomeado.

## 2026-11-01 — Ícones novos (2swat4-hud-icons.zip) + layout mais confortável + botão de ocultar

Pacote do usuário com os 42 PNGs regenerados (mesmos nomes, arte nova):
validados com `scripts/check_icons2.py` (IHDR/alpha/borda transparente/
conteúdo; 42 ok), normalização determinística de borda (alpha=0 nos 2 px
externos + feather no anel 3 — 9 PNGs tinham artefatos de borda da geração)
e contact sheet inspecionado. `assets/hudicons/` atualizado; o fallback
`<game>/hudicons/*.png` e a degradação por botão continuam.

Layout redesenhado (tudo em unidades u, como antes):

* Agrupamento direito em quatro colunas alinhadas que leem como um bloco:
  FOGO 150 / CMD 100 / SLOTS 64 (alinhados à direita), USAR 90 com PORTA 72
  centrado em cima, e as colunas REC/ZOOM/MODO e AGACH/CORRER (72 cada).
* Barra de topo à direita com os quatro botões "meta" (ESP/TAB + TROCA/ESC,
  64 cada) — linha única que o polegar nunca descansa em cima; o canto
  superior esquerdo fica livre.
* SLOTS/TAB/ESP/D-pad sobem de 56u para 64u (alvos maiores); zona do
  analógico de 0,38h para 0,34h (D-pad e INCL/INCR mais baixos, ao alcance
  real do polegar).
* Feedback de pressionado agora na cor âmbar do conjunto de ícones
  (#E8A33D); PORTA mantém o acento azul perto de porta.

Botão de ocultar (pedido do usuário): chip de olho no topo CENTRO, fora do
espelhamento de canhoto e fora do arco de descanso dos polegares. Um toque
colapsa TODO o HUD (analógico, botões, grade) e deixa só um olho cortado;
outro toque traz de volta. Toggle só de sessão (nunca persistido: o HUD
nunca volta oculto após reabrir). Oculto, cada dedo livre se comporta como
se o HUD não existisse. `releaseAll()` antes de ocultar solta qualquer
botão pressionado; o dedo do chip é reivindicado como papel consumido para
o UP não vazar. Zero alocação por frame mantido (glifo do olho é
reconstruído só no relayout).

### Crash da Carreira (shhxpott) — contenção alargada

O log do usuário mostrou duas corrupções com a mesma assinatura: os buffers
de parse do Havok de 19952 bytes escrevem ~90 bytes além do fim e o
arredondamento de 64 bytes deixava só 8 bytes de folga quando (HDR+n) já era
alinhado — o header vizinho era esmagado duas vezes e o load morreu no
vtable NULL do objeto ferido. O arredondamento agora GARANTE slack mínimo:
192 bytes + cauda de 16 para n>=4096 (absorve ~200 bytes, 2x o overrun
observado), 48+cauda para a classe 512..4095 (o bug da fórmula
`round_up(...)+TAIL` — slack colapsar a zero quando o tamanho já é múltiplo
— foi pego pelo cenário novo antes de virar build).

### Validação

* `sh tools/test_heap.sh`: ALL PASS (1039 ok, 0 falhas).
* `tools/heap_overrun_scenario.c` (novo): overrun de 90B e de 150B num bloco
  de 19952 absorvidos, vizinho intacto, churn 100x limpo.
* `sh scripts/test_hud.sh`: rc=0 (vinput/dinput/hud intatos).
* ecj 3.33 + android-all API 33 (Robolectric, java/javax removidos): árvore
  Java completa (io + org.libsdl) compila com 0 erros.

## 2026-11-01 (2) — hhkport: a causa raiz da Carreira, agora com nome

### Cadeia de evidência (log hhkport.zip do usuário)

* O writer trap do commit anterior armou no 1º evento ("PARANOID MODE on:
  quarantine and bump frontier [2137e598..40000000) are read-only") e a
  escrita bogus seguinte morreu com o autor: SIGSEGV code 1 em guest
  0x21381604 (top+0x306C), backtrace f_18143C0+0x4f4 ← f_1816380+0xdc ←
  f_1750430+0x244 ← f_176EB70+0xd0 ← f_1747040+0x5c8 ← f_14046CE+0xfa0,
  registrados eax=213805c0 esi=42bda000.
* f_18143C0 (Engine.dll, engine_84.c liftado) é o sub-alocador do sistema
  de memória do Havok: bins 0x40/0x200/0x2000, mágica 0x3425234 nos nós
  livres, chunks via M32(0x19EABB4) — que o log revela ser
  `MSVCR71!_aligned_malloc` (ret=0181444a/018145aa, args 0x2040/0x40).
* Os 3 últimos eventos do heap runtime antes da corrupção são exatamente
  os chunks do pool: alloc 20860ba0/20862d70/20864f40 size=8328
  (0x2040+0x40+8).
* Última linha do jogo antes da corrupção: "(Havok): Physical world for
  ULevel being created." — o load do SP-FoodWall morre na criação do mundo
  físico, os 3 logs seguidos (njdjport/shhxpott/hhkport) no mesmo ponto.

### Causa raiz

`shim__aligned_malloc` lia os args TROCADOS: `a=ARG(0), n=ARG(1)`. A
assinatura MSVC é `_aligned_malloc(size, alignment)`; o Havok pede
(0x2040, 0x40) e recebia alinhamento 0x2040 — `q` caía até 0x203F bytes
depois de p+8 e a janela usável `[q, q+0x2040)` estourava o bloco em até
~8 KB pastando nos vizinhos. Com blocos de 0x21D0 empacotados, ~metade dos
chunks estoura — a "corrupção em streaming" de megabytes dos 3 logs.
`scripts/test_aligned_malloc.c` prova: com os endereços reais do log, 8/16
chunks estouram (piores 8184 B) no shim antigo, 0/16 no corrigido; o 2º
chunk real (20862d70) estourava 8136 B direto sobre o 3º e mais 3 blocos.

### Correção

`runtime/swat4_shims.c`: `n=ARG(0), a=ARG(1)` — a mesma janela 0x2088 do
gmalloc agora sempre contém `[q, q+size)` e o alinhamento 0x40 pedido pelo
guest é honrado. Contenção (tail slack) e writer trap permanecem como
defesa para qualquer outro escritor divergente.

### Validação

* `gcc -O1 scripts/test_aligned_malloc.c && a.out`: antigo 8/16 estourando,
  corrigido 0/16, alinhamento ok (rc=0).
* `gcc -fsyntax-only -Iruntime runtime/{swat4_shims,heap,kernel32,msvcrt,misc,shims,dispatch,modules}.c`: rc=0.
* `sh tools/test_heap.sh`: ALL PASS; `sh scripts/test_hud.sh`: rc=0.

## 2026-11-01 (3) — WA0233: câmera volta a "um local fixo" a cada novo arrasto + o analógico não anda o personagem

### Sintomas (vídeo VID-20261101-WA0233.mp4, APK 409ebfa)

* Arrastar o dedo move a câmera, mas ao SOLTAR e arrastar de novo a câmera
  pula como se a ponteira do mouse voltasse a um lugar fixo a cada gesto.
* Mover o analógico (stick) não anda o personagem.

### Uma causa só (log hhkport.zip, linhas `hud:` da mesma sessão do vídeo)

* `hud scan: bail 2 cls 010a5320 num 0 data 00000000` — o slot
  `ASwatPlayer::PrivateStaticClass` (SwatGame+0xab848) leu **0x010a5320**,
  um ponteiro da imagem da Core.dll, e o scan antigo ABORTAVA aí (bail 2)
  antes de andar no array. `ctx` ficou **0 (UNKNOWN) a sessão inteira**
  (todas as linhas periódicas: `ctx 0`; `ctx -1 -> 0`, gen 8, pawn 0).
* Com ctx != MISSION o HudOverlay: (1) dedo livre vira mouse de menu →
  `menu_steer()` empurra `alvo - modelo` no canal de look a CADA novo
  toque — o "pulo para um local fixo" (o modelo do cursor fica onde o
  gesto anterior terminou e o dedo volta ao mesmo lugar de repouso);
  46 steers na sessão. (2) o stick manda SETAS (navegação de menu) em vez
  de WASD.
* DefUser.ini (cópia GOG): `W/A/S/D=MoveForward/Backward/StrafeLeft/Right`;
  `Up/Down/Left/Right` = vazio ou `ScrollChatUp/Down` — seta NÃO anda.
* O RVA 0xab848 está CERTO (export `?PrivateStaticClass@ASwatPlayer@@`,
  seção .data, SHA 27fc10be… conferido no PE; o log confirma
  `loaded SwatGame.dll at 03200000`). O slot é BSS (o .data bruto do PE só
  tem 0x1000 bytes; 0xab848 está na cauda virtual) e alguém escreveu um
  ponteiro estranho lá — origem ainda desconhecida (o gen referencia
  0x32AB848 só por ponteiro; nenhuma escrita direta). Um slot de .data não
  é a verdade do engine: o array de objetos é.

### Correção (runtime/hud_state.cpp)

* O scan agora resolve o array PRIMEIRO (códigos 3/4/5) e trata as classes
  como cache validável: slot de módulo quando contiver um UClass real
  (validação por auto-cadeia da metaclasse: `Class->Class == Class`), e
  **descoberta por nome** (UClass "SwatPlayer"/"SwatDoor" no próprio array,
  via tabela de Names com ANSI/UTF-16 e os dois layouts de `Names` — o
  ponteiro indireto do engine e o direto já pinado) quando o slot falhar.
  Classes descobertas ficam cacheadas (o fallback roda uma vez por
  processo); falha = UNKNOWN com diag 7, nunca palpite.
* `in_entry_package` também aceita os dois layouts de Names (no engine real
  `Names` é `FNameEntry**`).
* Diagnóstico novo: código 6 = fallback rodou e achou (out[1] = o valor
  estranho do slot, evidência permanente no log); 7 = fallback não achou.
  O código 2 deixou de ser terminal.

### Validação

* Selftest novo (S6/S6b/S7): slot com 0x010a5320 + array vivo → descoberta
  → MISSION, diag 6 com o valor do slot; cache segura o scan seguinte
  (diag 0); cadeia da classe quebrada → UNKNOWN, diag 7. Classes do teste
  agora na forma real do engine (META aponta para si, classes apontam para
  META) — uma instância de pawn nunca passa no filtro de classe.
* `sh scripts/test_hud.sh`: rc=0 (compilação C/C++ inteira + ring selftest
  + hud_state selftest + behav vinput/dinput: all ok).
* O próximo log do aparelho deve mostrar `hud scan: bail 6 cls 010a5320 …`
  uma vez e depois `hud: ctx 0 -> 2 (gen N pawn …)` ao entrar na missão —
  com ctx 2 o dedo livre é look relativo puro (sem steer) e o stick manda
  WASD (pol state DI). Se ainda vier `bail 7`, o diag traz num/data do
  array para a próxima etapa.
