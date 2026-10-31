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
