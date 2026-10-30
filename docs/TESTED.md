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
