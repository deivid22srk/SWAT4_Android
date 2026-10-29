# RECON — SWAT 4 (PC, 2005) para Android arm64

Tudo abaixo foi **medido** na cópia do jogo fornecida pelo usuário, que fica fora
deste repositório. Nenhum binário, asset ou código gerado é versionado aqui.
Ferramentas: `pefile 2024.8.26`, `capstone 5.0.7`, `unicorn 2.1.4`, `sha1sum`,
`strings`, `unrar 7.00`.

> Onde a resposta exigiria rodar o jogo no Windows/num aparelho, o texto diz
> explicitamente que **não foi verificado**. Não há otimismo neste documento.

---

## 1. Qual cópia é esta

Instalação **GOG Gold** (jogo base + expansão *The Stetchkov Syndicate*), pasta
`SWAT 4/`, 2,5 GB. Confirmado pelos arquivos `goggame-1409964317.info`,
`goggame-1409964317.hashdb`, `gog.ico`, `goglog.ini` e pela ausência de
`steam_api*.dll`. Existe também um `sw4.rar` (1,27 GB) que é uma distribuição
"STEAMRIP" do mesmo jogo; ela **não** foi usada (a GOG é DRM-free e é a cópia
documentada).

### Arquitetura e build

Todos os módulos são **PE32 / i386 (x86-32)**, `Machine = 0x014C`, subsistema
Windows GUI. A engine é **Unreal Engine 2** (strings `c:\perforce\branches\swat4\
staging\source\unreal\core\inc\...` em `Core.dll`/`Swat4.exe`, `UnEngineWin.h`,
`FBinkMovie`). `Content/System/Version.ini` registra
`BuildTime="Wed Jun 08 12:13 Eastern Standard Time 2005"` e
`ChangeTime="12:24:56"`.

### Hashes dos binários relevantes (base)

| Módulo | Tamanho | SHA-1 | Base pref. | SizeOfImage | Imports | Exports |
|---|---|---|---|---|---|---|
| `Swat4.exe` | 180.224 | `94d1a40f8f60b6664f539cab4c38a444b7c4a1e7` | 0x10900000 | 0x4feb0 | 520 | 1 |
| `Core.dll` | 1.093.632 | `1cefb028ec7f67dc7cdf0e1456744e4dd0579819` | 0x10100000 | 0x2b3000 | 131 | 2675 |
| `Engine.dll` | 6.615.040 | `6751da119558c747ce51d45fab668a13c929322a` | 0x10300000 | 0x829000 | 992 | 7497 |
| `Window.dll` | 475.136 | `1aeaabfb4c7a2d3d64b1df0a4d558484d8635428` | 0x11000000 | 0x76000 | 381 | 1317 |
| `WinDrv.dll` | 409.600 | `9613b77e29b358eb59bbe9d55db49937e76a8362` | 0x11100000 | 0x65000 | 354 | 85 |
| `D3DDrv.dll` | 552.960 | `9364c4f054e1286a7fdd3354eb744c4205949cd7` | 0x10000000 | 0x8a000 | 352 | 61 |
| `ALAudio.dll` | 135.168 | `827352b1b7ed36c51e679149afdf10fdc8531a27` | 0x10000000 | 0x21000 | 207 | 73 |
| `DefOpenAL32.dll` | 159.744 | `62cdf9b2694fd3f3140a11005199c3e24b0d9020` | 0x10000000 | 0xe3000 | 69 | 72 |
| `GUI.dll` | 294.912 | `1b58b462ef71ddacd0fdbe8a2d7d288c014eca34` | 0x10000000 | 0x59000 | 222 | 1001 |
| `IpDrv.dll` | 196.608 | `a379ea0a84c3d9c99e1facefd6adabaf1d3eb24b` | 0x10700000 | 0x34000 | 468 | 367 |
| `RTCShader.dll` | 258.048 | `35f0d5b36399f6946aa14ed71c748220cbd40370` | 0x10000000 | 0x43000 | 89 | 22 |
| `RenderUtilities.dll` | 172.032 | `a0827f975f576257ef550ee328f6642f48206510` | 0x10000000 | 0x30000 | 66 | 1 |
| `binkw32.dll` | 338.944 | `c8fbcdb9feced291bed8b91bc4a2bf40a7dcc954` | 0x18000000 | 0x68000 | 119 | 84 |
| `m4d.dll` | 274.432 | `5c0c4b5938886e747c5cace48fa1bda583592600` | 0x10000000 | 0x48000 | 87 | 25 |
| `AICommon.dll` | 49.152 | `186f51dcb1387559993dfd959188524e01053469` | 0x10000000 | 0xe000 | 187 | 100 |
| `SwatAIAwareness.dll` | 81.920 | `5d74155633123ef45bfa16361b435349a37304ae` | 0x10000000 | 0x14000 | 102 | 72 |
| `SwatAICommon.dll` | 278.528 | `570cdcbc644f9d378b058b28a6f8da240652bb1c` | 0x10000000 | 0x52000 | 342 | 989 |
| `Tyrion.dll` | 73.728 | `026854107ff49577aa5543500f3d266ea49e5c35` | 0x10000000 | 0x16000 | 239 | 209 |
| `RWOSupport.dll` | 110.592 | `799b51735abd21346e1b4ac3c10383ce535873db` | 0x10000000 | 0x1e000 | 204 | 111 |
| `SwatGame.dll` | 684.032 | `27fc10be8e74f44ed557533604b28378928df135` | 0x10000000 | 0xc3000 | 724 | 1505 |
| `SwatGui.dll` | 30.208 | `4bbab6ae0f4da45878969630e29203676566bc50` | 0x10000000 | 0xa000 | 52 | 13 |
| `IFC23.dll` | 237.568 | `bca60e404d042698b7562572906927cb8156b926` | 0x10000000 | 0x3c000 | 94 | 719 |
| `IrrSupport.dll` | 77.824 | `00eb180e96cd49fed3c6534817be887509c41891` | 0x10000000 | 0x15000 | 76 | 3 |
| `msvcr71.dll` | 348.160 | `d5502a1d00787d68f548ddeebbde1eca5e2b38ca` | 0x7c340000 | 0x56000 | 154 | 837 |
| `msvcp71.dll` | 499.712 | `c8ccb04eedac821a13fae314a2435192860c72b8` | 0x7c3a0000 | 0x7b000 | 109 | 2903 |

Expansão (`ContentExpansion/System`):

| Módulo | Tamanho | SHA-1 |
|---|---|---|
| `Swat4X.exe` | 196.608 | `e71a8f0ed71cb9937972965df5840799caa35341` |
| `SwatEquipment.dll` | 16.384 | `a871c4a4d1476aca5144d11cdbe87e2bc614f318` |
| `MassiveAdClient.dll` | 233.472 | `16d5fc58c6388b08c398f63de2144fc85280b8ab` |
| `ALAudio.dll` (variação da expansão, usa `DSOUND.dll`) | 282.624 | `0c0aa149f61b419cac667ab207102d8274d92df5` |

> **Build suportado:** base GOG `Swat4.exe` SHA-1
> `94d1a40f8f60b6664f539cab4c38a444b7c4a1e7`. Outro build tem código em outros
> endereços: discovery/lifting continuam funcionando, mas todo endereço fixo em
> `runtime/` e o launcher precisam ser refeitos.

## 2. DRM / proteção

* Nenhuma string `SafeDisc`, `SecuROM`, `Themida`, `ASPack`, `UPX`, `StarForce`,
  `Denuvo` nos executáveis/DLLs principais.
* Nenhuma seção com nome típico de packer. `Swat4.exe` tem só
  `.text/.rdata/.data/.rsrc` e **overlay 0**; `Core/Engine/D3DDrv` também têm
  overlay 0 e seções normais.
* `Swat4.exe` tem **tabela de relocação ausente** (`IMAGE_FILE_RELOCS_STRIPPED`),
  por isso é fixo em `0x10900000`; os DLLs têm relocações e podem ser rebaseados.
* `Engine.dll` contém funções de **CD key** (`GetCDKeyHash`,
  `ServerAuthenticateCDKey`) — isso é autenticação de multiplayer GameSpy, **não**
  DRM de disco.
* Conclusão: a cópia é DRM-free e o código pode ser levantado estaticamente
  como está. **Não verificado:** se um disco retail exigiria checagem de CD.

## 3. Módulos e mapa de endereços

O `Swat4.exe` é um stub fino (1 export, 520 imports) que carrega a engine nos
DLLs. O grafo de dependências (imports entre DLLs do jogo) exige uma ordem de
carga topológica; `Core.dll` e `Engine.dll` são a base.

Os endereços preferidos **colidem** (quase tudo em `0x10000000`, e `Core`/
`Engine`/`Swat4.exe` se sobrepõem). Como os DLLs têm relocação, este port os
**rebaseia** para uma faixa livre abaixo de `0x10000000`; o `Swat4.exe`, por não
ter relocação, **fica em `0x10900000`**. Por isso a pilha guest foi movida para
`0x1A000000` (o layout antigo em `0x10000000` colidiria com a imagem do exe).

| Módulo | Base neste port |
|---|---|
| `Swat4.exe` | `0x10900000` (fixa) |
| `Core.dll` | `0x01000000` |
| `Engine.dll` | `0x01400000` |
| `Window.dll` | `0x02000000` |
| `WinDrv.dll` | `0x02100000` |
| `D3DDrv.dll` | `0x02200000` |
| `ALAudio.dll` | `0x02300000` |
| `DefOpenAL32.dll` | `0x02400000` |
| `GUI.dll` | `0x02500000` |
| `IpDrv.dll` | `0x02600000` |
| `RTCShader.dll` | `0x02700000` |
| `RenderUtilities.dll` | `0x02800000` |
| `binkw32.dll` | `0x02900000` |
| `m4d.dll` | `0x02A00000` |
| `IFC23.dll` | `0x02B00000` |
| `IrrSupport.dll` | `0x02C00000` |
| `AICommon.dll` | `0x02D00000` |
| `SwatAIAwareness.dll` | `0x02E00000` |
| `SwatAICommon.dll` | `0x02F00000` |
| `Tyrion.dll` | `0x03000000` |
| `RWOSupport.dll` | `0x03100000` |
| `SwatGame.dll` | `0x03200000` |
| `SwatGui.dll` | `0x03300000` |
| `IGEffectsSystem.dll` | `0x03400000` |
| `IGSoundEffectsSubsystem.dll` | `0x03500000` |
| `IGVisualEffectsSubsystem.dll` | `0x03600000` |
| `Scripting.dll` | `0x03700000` |
| `ScriptCompiler.dll` | `0x03800000` |
| `dinterface.dll` | `0x03900000` |
| `ogg.dll` | `0x03A00000` |
| `vorbis.dll` | `0x03B00000` |
| `vorbisfile.dll` | `0x03C00000` |
| `Editor.dll` | `0x03D00000` |
| `UnrealEdPerforce.dll` | `0x03E00000` |

Faixas do runtime: pilha `0x1A000000` (30 slots de 4 MiB), TEB `0x1B000000`,
heap `0x20000000–0x40000000`, `VirtualAlloc` `0x40000000–0x60000000`, thunks de
import `0xFF000000`. **Nenhuma dessas faixas foi validada em execução.**

## 4. Importações (o que o runtime precisa fornecer)

Tabela completa por módulo em `docs/IMPORTS.md`. União (base+expansão):
**1.201 APIs distintas**, dominadas por `USER32` (308), `KERNEL32` (280),
`MSVCR71` (255), `GDI32` (124). O CRT VC7.1 (`msvcr71/msvcp71`) precisa de
shims (ou recompilação). O runtime atual já cobre uma parte de
kernel32/user32/msvcrt; o resto falha de forma explícita e logada
(`import X has no shim yet`), nunca em silêncio.

Padrão COM confirmado: `D3D9.DLL` importa **só** `Direct3DCreate9`, `DSOUND.DLL`
só uma entrada, `DINPUT8.DLL` só `DirectInput8Create`, `DDRAW.DLL` só
`DirectDrawCreate`; o resto é chamada por vtable.

**Lacuna concreta já identificada:** `Core.dll`, `Engine.dll` e `Window.dll`
importam 4 funções de `MSVCP71.dll` (`std::string`: construtor por `char*`,
construtor de cópia, destrutor e `c_str`). Elas **não** têm shim ainda; quando
chamadas, abortam com o nome. Como o CRT C++ do MSVC 7.1 tem layout próprio,
implementá-las exige casar o layout de `basic_string` com o alocador do jogo —
é um dos primeiros bloqueios a resolver para o boot.

## 5. Gráficos

* **Direct3D 9**, confirmado por `D3DDrv.dll` importar `d3d9.dll` e pelas
  strings `RenDev->Direct3DDevice9`, `Direct3DVertexBuffer9`, `vs_3_0`,
  `ps_3_0`, `vs_3_sw`, `ShaderCore\ShaderFactory`. É shader model 3.0.
* `RTCShader.dll` também importa `d3d9.dll` (compilador/otimizador de shader).
* Caminho do port: **dxvk-native** (D3D9 → Vulkan 1.1), o mesmo layer já
  vendorizado no scaffold. O `D3DDrv.dll` é recompilado; a criação do device e
  as vtables D3D9 são atendidas pela camada do host.
* **Não verificado**: se o dxvk-native cobre todos os estados/shader usados pelo
  SWAT 4 em GPU mobile. É o maior risco gráfico do projeto.

## 6. Áudio

* `ALAudio.dll` (subsistema de áudio da engine) + **OpenAL 1.x com EAX**
  (`DefOpenAL32.dll` exporta `al*`, `EAXGet/EAXSet`, `alEAXAC3GetFunctionTable`)
  + **Vorbis/OGG** (`vorbisfile.dll`, `vorbis.dll`, `ogg.dll`; `ALAudio` importa
  `vorbisfile.dll`). A expansão troca o backend para `DSOUND.dll`.
* Formato de som: **OGG Vorbis** (9.110 arquivos `.ogg` no total) e `.uax`.
* Plano: recompilar `DefOpenAL32`/`vorbis*`/`ogg` (já no mapa) e ligar a saída
  OpenAL ao dispositivo SDL2 do port. **Não implementado/validado.**

## 7. Vídeo / intros

* **Bink** (`binkw32.dll`, exports `_BinkOpen@8`, `_BinkDoFrame@4`,
  `_BinkDX9SurfaceType@4`, superfícies `BINK16/32/...`; classe `FBinkMovie` na
  engine). 6 arquivos `.bik` em `Content/Movies`.
* Plano: recompilar `binkw32.dll` (já no mapa) e pular intros com opção
  explícita se o decoder falhar. **Não validado.**

## 8. Assets e formatos

| Pasta | Tamanho |
|---|---|
| `Content/Textures` (`.utx`) | 615 MB |
| `Content/Maps` | 415 MB |
| `Content/StaticMeshes` (`.usx`) | 284 MB |
| `Content/Sounds` (`.ogg`, `.uax`) | 251 MB |
| `Content/Animations` (`.ukx`) | 83 MB |
| `Content/System` (DLLs, `.u`, `.ini`) | 48 MB |
| `Content/Movies` (`.bik`) | 35 MB |
| `Content/HavokData` (`.mopp`) | 4,7 MB |
| **Base total** | **1,7 GB** |
| **Expansão total** | **766 MB** |

Contagem de arquivos: 9.110 `.ogg`, 93 `.utx`, 77 `.dll`, 69 `.u`, 68 `.ukx`,
51 `.s4m`, 50 `.usx`, 14 `.exe`, 11 `.uax`, 6 `.bik`.

Carregamento: a engine lê pacotes Unreal (`.u/.utx/.ukx/.usx/.uax`) por
`FFileManagerWindows`/`CreateFile`/`ReadFile`; não há memory-mapped nem
streaming fora do padrão da engine. O runtime só precisa de I/O de arquivo
comum — o que o scaffold já implementa.

## 9. Scripts / bytecode

O jogo usa **UnrealScript** compilado em `.u` (bytecode), interpretado pela VM
nativa em `Core.dll` (`GNatives`, `ProcessEvent`, `UObject`). Isso **não** quebra
a recompilação estática: a VM é código nativo recompilado e o bytecode é dado
lido em tempo de execução. Mas significa que o comportamento de gameplay não
está no código levantado — está nos `.u`. Nada disso foi executado ainda.

## 10. Rede e extras

* Multiplayer usa **GameSpy** (`IpDrv.dll`, `WSOCK32/WS2_32`, `GetCDKeyHash`,
  `ServerAuthenticateCDKey`, `swat4_with_gamespy.url`).
* Expansão inclui `MassiveAdClient.dll` (propaganda in-game da Massive Inc.,
  serviço morto).
* Decisão de escopo: **todo o online fica de fora** (stubs), incluindo GameSpy
  e Massive. O single-player é o alvo.

## 11. Sinais que quebram recompilação estática

* **SEH**: `msvcr71` exporta `_except_handler3`, `__CxxFrameHandler`,
  `_local_unwind2`, `_setjmp3`, `_resetstkoflw`. O lifter precisa de suporte a
  SEH; o scaffold tem `runtime/setjmp.c` e `fpu.c`, mas a cobertura de SEH do
  UE2 **não foi validada**.
* **x87**: a engine é de 2005 e usa x87 intensivamente (o lifter tem suporte
  x87 explícito); precisão/arredondamento é um risco de divergência.
* **Chamadas indiretas**: vtables de `UObject`/`ProcessEvent` e tabelas de
  mensagens. O discovery resolve muitas, mas `Core.dll` teve **18 indirect jumps
  não resolvidos** e 160 gaps (906 bytes) — precisam de tratamento.
* **Código auto-modificável / geração em runtime**: não há sinal nos binários,
  mas a engine compila shaders em runtime (D3D9), o que é dado, não código x86.
* **TLS**: `Core.dll`/`Engine.dll` têm diretório TLS; o runtime avisa que não
  roda callbacks TLS.

## 12. Medições de escala (reais)

| Etapa | Módulo | Tempo | RAM máx. | Resultado |
|---|---|---|---|---|
| discovery | `Core.dll` | 12,5 s | 931 MB | 6.893 funções; 478.725/663.025 bytes de `.text` cobertos (72%); 161 gaps, 18 indirect jumps |
| lifting | `Core.dll` | 13,6 s | 123 MB | 6.886/6.893 funções; 7 rejeições (`daa`, `int1`, `aas`, `das`, `loopne`, regs de segmento) |
| discovery | `Engine.dll` | **246,8 s** | **2.422 MB** (pico) | **39.432 funções**; 3.643.841/4.870.371 bytes (75%); 1.127 gaps, 199 indirect jumps |
| lifting | `Engine.dll` | **100,0 s** | — | **39.396/39.432**; 36 rejeições |
| discovery | `SwatGui.dll` | 0,35 s | — | 87 funções |
| difftest (lifted × unicorn) | `Core.dll` | — | — | **216 amostras, 214 liftadas; 19 divergências em 16 de 79 mnemônicos** (63/79 sem divergência). Falhas concentradas em x87 (`fcompp`, `fdivp`, `fdivrp`, `fmulp`, `fstp`, `fsubp`, `fucompp`, `fyl2x`, `fpatan`, `fnstsw`, `fistp`) e em `imul`, `mul`, `push`, `xchg`, `emms`. |

Conjunto completo (base, **33 módulos**: `Swat4.exe` + 32 DLLs):

* **80.848 funções** em **434 arquivos `.c` / 182 MB** de C.
* Soma dos tempos: **347 s de discovery + 184 s de lifting** (≈ 9 min no total).
  `Engine.dll` sozinho responde por 247 s + 100 s.
* Pico de RAM observado no discovery: **2,4 GB** (`Engine.dll`).
* `m4d.dll` derrubou o discovery por uma jump table fora da imagem; o `u32` do
  discovery foi corrigido e o módulo foi reprocessado (1.323 funções). Sem a
  correção, um único DLL inviabilizava o lote inteiro.
* `dinterface.dll` foi **excluído**: existe em `Content/System`, mas nenhum
  módulo o importa (verificado com `pefile`).
* A compilação ARM64 de 182 MB de C é o gargalo de CI (ver §13).

## 13. Plano de risco

| Risco | Gravidade | Mitigação / estado |
|---|---|---|
| Volume de código (UE2: ~33 módulos + VM) | Alta | gen/ em repositório privado; `LB_GEN_JOBS` limitado; cache de objetos no CI |
| Runtime incompleto (1.201 APIs de SO) | Alta | falha explícita por import; priorizar boot path (kernel32/user32/advapi32/msvcrt/ole32) |
| D3D9 SM3 em GPU mobile via dxvk | Alta | testar shaders cedo; fallback de qualidade baixa; **não testado** |
| OpenAL/EAX | Média | recompilar DefOpenAL32 e mapear para SDL2; EAX pode virar no-op |
| Bink | Média | recompilar binkw32; opção de pular intros |
| SEH/x87 | Alta | difftest contra unicorn; traps explícitos |
| Chamadas indiretas não resolvidas | Média | dispatch por tabela + tratamento de gaps |
| Tempo de CI (6 h) | Média | cache de `android/app/.cxx`; runner self-hosted se necessário |
| Memória no aparelho | Média | medir; requisito só será publicado após medição |
| DRM | Baixa | cópia GOG DRM-free; sem unpacking necessário |
| Distribuição do código gerado | Legal | gen/ só no repo privado; APK só em release privada |

## 14. O que NÃO foi verificado

* Se o jogo chega ao menu principal (não chega; ver `docs/TESTED.md`).
* Se os shims de SO cobrem o boot da engine.
* D3D9/dxvk, áudio, Bink, input e touch em aparelho real.
* Desempenho, uso de RAM e tempo de carregamento em hardware.