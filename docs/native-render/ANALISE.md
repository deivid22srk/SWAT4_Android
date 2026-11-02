<<<<<<< HEAD
# Análise técnica — renderer Vulkan nativo do SWAT 4

**Branch:** `feature/native-vulkan-renderer`  
**Base analisada:** `main`  
**Escopo desta etapa:** inspeção documental e de integração existente. Este documento não afirma que um renderer foi implementado.

## 1. Estado real do projeto

O projeto é um port Android arm64 de SWAT 4 (2005), Unreal Engine 2, por recompilação estática de módulos PE32/i386 para C e compilação com Android NDK. O código derivado do jogo fica em um repositório privado separado; este repositório não contém executáveis, assets nem o diretório `gen/`.

O README registra explicitamente que o jogo ainda **não é jogável** e não chega ao menu principal. O boot desktop observado inicializa D3D9/DXVK e encerra com `exit(0)`; isso não demonstra que o caminho de renderização durante gameplay esteja exercitado.

## 2. API gráfica confirmada

A análise PE documentada em `docs/RECON.md` e `docs/IMPORTS.md` identifica:

- **Direct3D 9**, não D3D8.
- `D3DDrv.dll` importa `Direct3DCreate9` de `d3d9.dll`.
- `RTCShader.dll` também importa `d3d9.dll`.
- Há evidência de **Shader Model 3.0** (`vs_3_0`, `ps_3_0`) e caminhos software (`vs_3_sw`).
- A import table não enumera os métodos do device: chamadas posteriores são feitas por **COM vtables**. Portanto, a lista de imports, isoladamente, não revela os métodos `IDirect3DDevice9` chamados.

### Inventário ainda necessário

Não foi encontrado, nos documentos lidos, um inventário dinâmico completo de métodos D3D9, estados, formatos, combinações de texture stages, render targets, depth/stencil, blending, fog, alpha test, projective textures ou shadow volumes realmente exercitados pelo jogo.

Esse levantamento exige instrumentar a interface D3D9/COM ou executar o jogo com um backend de rastreamento e capturar chamadas em cenas representativas. O projeto ainda não alcança o menu principal, portanto não há uma captura de gameplay confiável disponível no estado documentado.

**Conclusão:** não é tecnicamente honesto declarar agora que é possível implementar “somente tudo o que o jogo usa” sem primeiro obter essa telemetria.

## 3. Integração gráfica atual

Evidências no repositório:

- `android/app/jni/CMakeLists.txt` define C/C++17, adiciona SDL2, `dxvk` e `game`.
- `docs/RECON.md` descreve o caminho atual como D3D9 → Vulkan 1.1 via dxvk-native.
- O workflow usa NDK **27.2.12479018**, CMake **3.22.1**, JDK 17, compile SDK 34, minSdk/API 28 e ABI **arm64-v8a**.
- `android/app/build.gradle` fixa `abiFilters 'arm64-v8a'`.
- O README exige Android 9+ e Vulkan 1.1 para a instalação atual.
- A build completa depende de `gen/` privado; a build pública app-only usa `ci/gen_stub/` e não executa o jogo.

Ainda falta rastrear, em código-fonte, o caminho exato de criação do `ANativeWindow`/surface, instanciação Vulkan, ownership da swapchain e ciclo de vida pause/resume, assim como a configuração de shaders e a seleção de backend. A existência de `add_subdirectory(dxvk)` não basta para provar todos esses detalhes.

## 4. Requisitos e riscos para o backend novo

Um backend D3D9→Vulkan próprio não pode ser tratado como uma simples camada de apresentação. Ele precisa implementar a semântica D3D9 observada pelo jogo, incluindo:

1. objetos e lifetime COM (interfaces, referências e vtables);
2. criação de device, apresentação e reset/perda de device;
3. buffers, vertex declarations, índices, texturas, surfaces e render targets;
4. estados de renderização, sampler, texture stage e transforms;
5. fixed-function, se efetivamente usado, e tradução de shaders D3D9 SM3 para SPIR-V;
6. formatos de textura e conversão de formatos não suportados pela GPU;
7. operações de depth/stencil, blending, clipping e multisampling que forem observadas;
8. sincronização Vulkan, swapchain Android, rotação e recriação de surface;
9. cache persistente e observabilidade, sem bloquear o caminho quente.

Vulkan 1.1 deve ser o baseline real. Dynamic rendering é uma opção de otimização, não uma dependência obrigatória, pois não faz parte do baseline Vulkan 1.1. Recursos opcionais devem ser detectados e possuir fallback funcional.

Adreno, Mali e PowerVR/Immortalis não podem ser declaradas compatíveis por inspeção mental. É necessário compilar e validar em dispositivos/drivers reais, com logs e testes de cenas.

## 5. Plano técnico seguro

### Fase A — instrumentar antes de substituir

- Instrumentar a fronteira D3D9/COM atual para registrar criação de objetos, métodos chamados, parâmetros, formatos, estados e sequências por frame.
- Capturar boot, menus, mapas, gameplay, efeitos, vídeos e mudanças de resolução.
- Criar um corpus reproduzível de traces, sem incluir arquivos proprietários do jogo no repositório.
- Manter os traces e resultados de análise separados de assets e binários.

### Fase B — especificação observada

- Converter traces em uma matriz de cobertura: interface/método, parâmetros, estado, formato, frequência e evidência.
- Identificar shaders presentes e seus perfis reais; determinar se há fixed-function ou se o caminho é inteiramente shader-based.
- Definir testes de conformidade para cada comportamento observado antes de escrever a tradução.

### Fase C — backend paralelo

- Criar módulo isolado `native-render/` com interface de backend e seleção explícita por configuração.
- Preservar DXVK como caminho padrão/fallback até equivalência funcional demonstrada.
- Implementar objetos D3D9 e recursos em incrementos testáveis, sem retornar sucesso falso para operações não suportadas.
- Adicionar cache de pipeline, alocação, sincronização e métricas após o caminho funcional mínimo estar correto.

### Fase D — validação

- Testes unitários e de replay dos traces.
- Vulkan validation layers em debug quando disponíveis.
- CI para arm64-v8a e análise estática.
- Testes em aparelhos Adreno, Mali e PowerVR/Immortalis disponíveis; registrar modelo, driver, Vulkan API, traces e resultados.
- Só selecionar o backend novo como opção utilizável quando cobrir a matriz de chamadas observadas.

## 6. Critérios de bloqueio atuais

O desenvolvimento de uma implementação completa não pode ser certificado neste momento porque:

- o jogo não chega ao menu principal no estado descrito pelo README;
- não há captura completa das chamadas COM D3D9 reais durante gameplay;
- não há inventário de estados/formatos/shaders exercitados;
- a leitura documental realizada nesta etapa não equivale a auditar todos os arquivos do repositório;
- não foi executada build local nem teste em aparelho nesta etapa.

A cópia de jogo não foi baixada nesta etapa: os documentos existentes já contêm hashes, identificação PE e inventário básico. Ela só será necessária se a instrumentação/extração de shaders exigir acesso aos binários locais. Nenhum asset ou binário proprietário deve ser adicionado ao repositório.

## 7. Decisão

**Não substituir DXVK ainda.** A próxima entrega técnica correta é obter telemetria D3D9 real e levar o runtime existente a uma execução que exercite o renderer. Implementar uma API D3D9 parcial antes disso violaria o requisito de não usar stubs e criaria uma falsa impressão de compatibilidade.

## Referências internas consultadas

- `README.md`
- `docs/RECON.md`
- `docs/IMPORTS.md`
- `docs/REFERENCE.md`
- `.github/workflows/build.yml`
- `tools/swat4_modules.py`
- `android/app/jni/CMakeLists.txt`
- `android/app/build.gradle`

## Implementação incremental: probe Vulkan nativo

Foi adicionada a biblioteca `nativevk_probe`, separada do DXVK, com uma chamada JNI executada no início de `GameActivity`. O relatório é gravado em `<pasta-do-jogo>/port/native_vulkan_probe.txt`.

O probe consulta:
- versão anunciada pelo loader Vulkan (com resolução dinâmica da função de consulta para não exigir o símbolo Vulkan 1.1 em loaders antigos);
- quantidade e identificação das GPUs físicas;
- versão Vulkan e versão do driver por dispositivo;
- filas e presença de fila gráfica;
- heaps de memória local do dispositivo;
- recursos básicos relevantes para caracterizar a GPU.

**Limites explícitos:** não cria um `VkDevice`, não cria surface/swapchain, não submete comandos, não compila shaders e não renderiza. Portanto, não é ainda o renderizador nativo D3D9→Vulkan e não substitui DXVK. O backend DXVK permanece carregado e ativo.

A biblioteca foi ligada ao build Android e o relatório é opcional: falhas no probe são capturadas no Java e não devem impedir a inicialização do jogo. A compilação e o comportamento em dispositivo ainda precisam ser confirmados pelo CI e por teste real.
=======
# Análise do projeto e plano técnico — `native-render`

> Complemento da análise de viabilidade da sondagem Vulkan
> (`ANALISE-sondagem.md`, do trabalho anterior de probe nesta branch). Este
> documento responde às perguntas que a sondagem deixou abertas — em
> particular o "inventário de métodos D3D9": o conjunto exato é fechado e
> conhecido (as 297 shims COM da bridge, §1 abaixo).

Documento da **Etapa 1** (análise antes de escrever código). Tudo aqui foi
medido no código deste repositório; referências de arquivo:linha acompanham
cada afirmação.

---

## 1. O que este projeto é (e por que isso muda o desenho do renderizador)

O port **não** é uma recompilação de código-fonte da engine: é uma
**recompilação estática** do binário x86-32 do jogo para C, que depois é
compilado para ARM64 (`README.md`, seção "Como funciona"):

1. **Build-time**: `tools/discover.py` + `tools/lift.py` convertem as funções
   x86-32 dos DLLs do jogo em C (`gen/*.c`, repositório privado
   `SWAT4_codegen`). O `gen/` nunca está no git.
2. **Runtime** (`runtime/`): reloca os PEs na memória, resolve imports para
   *shims* Win32 ou para exports recompilados, e fornece D3D9, áudio, input e
   arquivos ao código do jogo.
3. **App**: launcher Java + SDL2 (`libmain.so`).

Consequência central para o renderizador: **a "API gráfica que o jogo consome"
não é um código-fonte que possamos ler — é o conjunto exato de métodos COM que
a bridge do runtime já mapeia.** Esse conjunto é conhecido e fechado: a bridge
gerada (`runtime/d3d9_bridge_gen.cpp`, produzida por `tools/gen_d3d9_bridge.py`)
contém **297 shims** cobrindo as vtables completas de **15 interfaces**
(`runtime/d3d9_ifaces.h`): `IDirect3D9`, `IDirect3DDevice9`, `SwapChain9`,
`Texture9`, `CubeTexture9`, `VolumeTexture9`, `Volume9`, `Surface9`,
`VertexBuffer9`, `IndexBuffer9`, `VertexDeclaration9`, `VertexShader9`,
`PixelShader9`, `StateBlock9`, `Query9`. Um substituto do dxvk-native precisa
implementar essas vtables com a ABI exata dos headers `d3d9.h` vendidos
(`android/app/jni/dxvk/include/native/directx/d3d9.h`, estilo Wine, com
`__uuidof` provido por `windows_base.h`).

## 2. Qual API gráfica o jogo usa — levantamento real

| Pergunta | Resposta | Evidência |
|---|---|---|
| API | **Direct3D 9** (COM por vtable) | `docs/IMPORTS.md`: `D3D9.DLL (1 functions)` → só `Direct3DCreate9`; `docs/RECON.md` §5 |
| Shader model | **SM 3.0** (`vs_3_0`, `ps_3_0`, `vs_3_sw`) | strings dentro de `D3DDrv.dll`, citadas em `docs/RECON.md` §5 |
| Onde os shaders nascem | `RTCShader.dll` (fábrica `ShaderCore\ShaderFactory`) + `D3DCompiler_43.dll` **executando dentro do hóspede** (o DLL do jogo é recompilado junto) | `docs/RECON.md` §5; `runtime/kernel32.c:1097` (heap dedicado ao `D3DCompiler_43`); `runtime/shader_status.c` (progresso de compilação em runtime) |
| Consequência | `CreateVertexShader`/`CreatePixelShader` recebem **bytecode D3D9 SM3 real** (saída do D3DCompiler) | fluxo: HLSL → D3DCompiler (hóspede) → bytecode → COM → host |
| Quantidade | ~1.700 shaders pré-construídos no caminho NVIDIA/Intel/AMD; no caminho "vendor desconhecido" (Qualcomm) compila em runtime | `runtime/d3d9_bridge.cpp:186-191` (comentário medido em campo, `sjx.log`) |
| Texturas comprimidas | DXT1/3/5 (BC1/2/3). `D3DFMT_D24S8` é pedido para o autodepthstencil | `runtime/d3d9_bcemu.cpp`; `runtime/d3d9_bridge.cpp:313-319` |
| BC já tem emulação | **Sim, na bridge** (`d3d9_bcemu.cpp`): quando o host recusa `D3DFMT_DXT1`, as texturas nascem `A8R8G8B8` no host e a bridge decodifica no `UnlockRect` | `runtime/d3d9_bcemu.cpp:99-109` (`bc_emulating` decide por `CheckDeviceFormat`) |
| Fog, luzes, transform | `SetTransform`, `SetLight`, `LightEnable`, `SetMaterial`, `SetRenderState`, `SetTextureStageState`, `SetSamplerState` estão na vtable mapeada; D3DDrv é centrado em shaders, mas os caminhos FFF precisam funcionar | `tools/gen_d3d9_bridge.py:47-101` |
| Draw calls | `DrawPrimitive`, `DrawIndexedPrimitive`, `DrawPrimitiveUP`, `DrawIndexedPrimitiveUP`, streams + FVF/`VertexDeclaration` | idem |
| Present | `Present` na device e na swapchain adicional; vsync é forçado no Android pela bridge | `runtime/d3d9_bridge.cpp:453-479`, `167-172` |
| Queries | `CreateQuery` mapeado (occlusion/event usados pelo UE2 para frame-sync de leituras) | idem |
| Vídeo Bink | `BinkDX9SurfaceType` — cópia frame→surface D3D9 por lock | `docs/IMPORTS.md` (BINKW32) |

**Pontos de atenção herdados da bridge (comportamento que o host recebe):**

- **Um único device por processo**: a bridge cria o device host uma vez e
  `CreateDevice` subsequente vira `Reset` do mesmo objeto
  (`runtime/d3d9_bridge.cpp:277-344`). Motivo: o Android concede **uma**
  `VkSurfaceKHR` por `ANativeWindow`, e o jogo recria o device em cada troca de
  resolução (splash 1142×540 → filmes 914×432 → menu 1142×540).
- **Remapeamento D24S8→D24X8** no `CreateDevice`/`Reset` (bug de driver Android
  documentado no comentário; aplica-se a qualquer host, incluindo o novo).
- **Locks** são servidos por sombras na memória do hóspede e copiados ao host
  no `Unlock` (`runtime/d3d9_bridge.cpp:535-673`); texturas SYSTEMMEM ficam
  "persistentemente sujas" e são empurradas antes de `UpdateTexture`
  (`:519-525`).
- **Resolução**: `EnumAdapterModes` expõe 9 modos = janela × 0.2..1.0
  ("Resolution Scale"; `d3d9_bridge.cpp:229-251`). O backbuffer do jogo é
  menor que a janela; **o host é responsável por escalar no Present**.

## 3. Como o dxvk-native está integrado hoje

- **Vendored** em `android/app/jni/dxvk` (source list de `d3d9/*` +
  `dxso/*` + `dxvk/*`, `CMakeLists.txt`), construído como `libdxvk_d3d9.so`
  e **linkado dentro do `libmain.so`**
  (`android/app/jni/game/CMakeLists.txt:41`).
- `runtime/d3d9_bridge.cpp:915` chama `Direct3DCreate9(D3D_SDK_VERSION)`
  (símbolo do dxvk, resolvido no link). Todo o resto da "integração" é a
  bridge acima.
- **WSI**: dxvk usa o backend SDL2 (`dxvk/src/wsi/sdl2`); a janela vem de
  `platform_window()` = `SDL_Window*` (`runtime/platform_sdl.c:182`) criado
  com `SDL_WINDOW_VULKAN` e `SDL_Vulkan_LoadLibrary` antes do primeiro
  `CreateDevice` (`platform_sdl.c:147-163`). O *splash* do launcher é
  destruído e a janela recriada antes do dxvk pegar a surface
  (`platform_sdl.c:112-141`, chamado de `B_IDirect3D9_CreateDevice`).
- **Licença/limites**: dxvk-native 1.9.2b "patched for mobile GPUs"
  (`dxvk/CMakeLists.txt:1-2`). O RECON marca o caminho gráfico como
  **"Vendorizado, não validado"** (`docs/RECON.md` §5) e o `docs/TESTED.md`
  só registra boot desktop com llvmpipe e saída limpa (`exit(0)`).

## 4. ABIs, toolchain e CI

| Item | Valor |
|---|---|
| ABI | **arm64-v8a** (única; `android/app/build.gradle` `abiFilters`) |
| minSdk / target | 28 (Android 9, Vulkan 1.1 baseline) / 29 |
| NDK / CMake | 27.2.12479018 / 3.22.1 (`build.yml` `env`) |
| STL | `c++_shared`; `externalNativeBuild` = Gradle→CMake (`jni/CMakeLists.txt`) |
| Windows do CI | `workflow_dispatch` (qualquer ref) + `push` em `main` com filtro de paths (`android/**`, `runtime/**`, `tools/**`, `ci/**`, `build.yml`) |
| Modos | app-only (público, `ci/gen_stub`) e completo (gen privado via `GEN_REPO`/`GEN_TOKEN`) |
| Verificação local possível (sem NDK) | `g++/clang` + SDL2 + **lavapipe** (Vulkan por software) + `VK_LAYER_KHRONOS_validation` + `spirv-val` — o projeto já usa llvmpipe nos testes desktop (`lb_d3d9.log`) |

## 5. Plano técnico do `native-render`

### 5.1 Princípios

1. **Backend substituível em runtime**: o dxvk continua sendo o *default* e o
   fallback; a escolha é por configuração (`port/renderer.txt` ou env
   `LB_RENDERER=dxvk|native`), com opção de build para excluir um backend.
   `shim_Direct3DCreate9` passa a chamar um despachante
   (`runtime/renderer_host.cpp`) em vez do símbolo do dxvk diretamente.
2. **Implementar o que a bridge mapeia — todo — e nada "de mentira"**: cada
   método das 15 vtables tem semântica D3D9 real. Métodos que a D3D9 define
   como no-op trivial (ex.: `SetNPatchMode` que só guarda um float) são
   no-op **por definição**, não por preguiça, e estão anotados.
3. **SPIR-V emitido diretamente**, sem toolchain de shader no build: o mesmo
   gerador de SPIR-V (escrito aqui, `src/shader/nvr_spirv.cpp`) alimenta
   (a) o tradutor de bytecode SM2/SM3 → SPIR-V e (b) os shaders de
   fixed-function. Vantagens sobre `glslc`: determinismo, zero dependência
   nova no CI, cache trivial por hash, e validação local com `spirv-val`.
4. **GPUs tile-based primeiro**: render passes com `LOAD_OP_DONT_CARE` /
   `STORE_OP_DONT_CARE` onde a semântica permite, attachments transitórios,
   2 frames in flight, upload por *staging ring* persistente, e um único
   `vkCmdBeginRenderPass` por troca de render target (sem passes vazios).
5. **Compatibilidade**: Vulkan 1.1 baseline; `VK_KHR_get_physical_device_properties2`
   e formatos de depth/stencil resolvidos por consulta real
   (D24S8 → D24S8 | D32S8 | D32/D24X8), BC nativo quando o driver aceita
   (`VK_EXT_texture_compression_bc`) e fallback RGBA (decodificação própria
   no upload) quando não; MSAA só se o device suportar de verdade.

### 5.2 Módulo

```
android/app/jni/native-render/
  CMakeLists.txt            → libnative_render.so (arm64 no CI; x86-64 nos testes desktop)
  include/nvr_api.h         → nvr_Direct3DCreate9() (único símbolo exportado)
  src/
    nvr_common.*            → log (logcat/stderr), config, util
    nvr_format.*            → D3DFORMAT↔VkFormat, tamanhos de bloco, suporte
    vulkan/                 → loader (SDL2), instance/device, swapchain (pre-rotation,
                              SUBOPTIMAL/recriação), frames-in-flight, staging ring,
                              pipeline cache (Vk + disco + worker assíncrono), VMA
    shader/                 → emissor SPIR-V; parser de bytecode D3D9 SM2/SM3;
                              tradutores VS/PS; pipeline fixed-function
    d3d9/                   → COM: IDirect3D9/Device9/recursos; estados; draws;
                              present (escala+gamma ramp); blits; queries; stateblocks
  thirdparty/vma/           → VulkanMemoryAllocator 3.5.0 (Apache-2.0, vendored)
  tests/                    → harness desktop headless (lavapipe) — não entra no APK
```

### 5.3 Decisões registradas

| Tema | Decisão | Justificativa |
|---|---|---|
| Surface | `SDL_Vulkan_CreateSurface` sobre o `SDL_Window*` que a bridge já entrega como `HWND` | mesmo caminho do dxvk; zero código Android-specific; funciona no desktop |
| Pre-rotation | `preTransform = currentTransform`; 90°/270° giram o blit final do Present; 0°/180° direto | robusto em qualquer painel; a activity é `sensorLandscape` (0°/180°) |
| Escala de resolução | Present faz blit do backbuffer do jogo → imagem da swapchain (com gamma ramp aplicada) | a bridge entrega backbuffers 0.2–1.0× da janela por desenho |
| SM3 | Tradutor bytecode→SPIR-V próprio (parser completo SM2.0/3.0: ALU, modificadores, flow control, `texld*`, `texkill`, `m*xy`, `sincos`, endereçamento relativo `aL`) | o jogo gera bytecode em runtime no hóspede; não existe HLSL no host |
| FFF | Pipeline gerado por estado (VS+PS via o mesmo emissor SPIR-V), cobrindo transform/luzes/material/TSS(4)/fog/alpha-test | `SetTransform/SetLight/...` estão na vtable mapeada; precisa funcionar mesmo se pouco usado |
| SWVP (`vs_3_sw`, `SOFTWARE_VERTEXPROCESSING`) | executa o VS do hóspede na GPU (resultado idêntico por definição de SWVP) | evita emulação CPU; documentado |
| `ProcessVertices` | via VS+SSBO (`vertexPipelineStoresAndAtomics`); se o device não tiver o feature → `D3DERR_INVALIDCALL` **com log** | sem feature não há como ser real; não há stub |
| Dynamic sampler index (SM3 `s[aL]`) | não suportado → erro explícito na compilação com nome da instrução | legal em SM3, mas incompatível com layouts estáticos de descriptor sem bindless; se o jogo usasse, o log nomeia o shader |
| Queries | Occlusion→`VK_QUERY_TYPE_OCCLUSION`; Event→fence/timestamp | usados por UE2 |
| Threads | submits na thread do jogo (como hoje); worker só para compilar pipelines | a bridge é single-thread por contrato |
| Caches | `VkPipelineCache` + SPIR-V por hash em disco (`<jogo>/port/native-render/`) | reinício do jogo carrega instantâneo |

### 5.4 Riscos conhecidos (honestidade)

1. **Nenhum teste em aparelho é possível neste ambiente** (sem Android; o
   projeto inteiro está nesse estado — ver `docs/TESTED.md`). O que for
   verificado aqui será com **lavapipe + validation layers + leitura de
   pixels** em um harness que exercita a API D3D9 do renderizador
   diretamente. O comportamento em Adreno/Mali/PowerVR precisa de validação
   em campo (o projeto já tem canal de logs em campo via `port/log.txt`).
2. A bridge obriga **1 device + Reset** para toda a vida do processo; o
   renderizador novo é desenhado para isso (swapchain própria sobrevive a
   `Reset` quando o formato/tamanho só mudam).
3. O caminho dxvk continua sendo o default até o native-render ser validado
   em campo (regra 3 do enunciado).

---

## 6. Desvios registrados durante a implementação (atualização pós-código)

Esta seção documenta onde a implementação final divergiu do plano da §5 e por
quê — para que nada fique implícito:

1. **`ProcessVertices` não foi implementado.** A §5.3 prometia VS+SSBO. Na
   implementação, além da execução do VS, seria necessária a conversão da
   saída para o FVF/declaração de destino (D3DDECLTYPE_* de 1-4 floats e
   formats normalizados) — maquinaria grande para um caminho sem evidência de
   uso pelo jogo (D3DDrv com SM3 usa vertex processing por hardware). Decisão:
   `D3DERR_INVALIDCALL` com log nomeado (`ProcessVertices nao suportado`) em
   vez de sucesso falso. O `feature vertexPipelineStoresAndAtomics` continua
   sendo consultado e ativado no device caso se decida implementar depois.
2. **O worker assíncrono de pipelines foi removido.** A §5.2 pedia
   "pipeline cache (Vk + disco + worker assíncrono)". O worker executaria
   `build_pipeline` numa thread secundária sem lock no estado do device
   (m_st, caches de pass/module) — corrida real. O aquecimento continua
   existindo pelo `VkPipelineCache` em disco (blob carregado no boot, salvo
   a cada 1024 frames e no shutdown), e a criação de pipeline é bloqueante
   na thread do jogo, como num driver real.
3. **Descriptor sets por frame.** O cache de descriptor sets passou a ser
   por frame (limpo quando o pool do frame é destruído): os UBOs apontam
   para o staging ring do frame corrente, e o offset real vai como dynamic
   offset. O desenho original ("limpo por frame pelo owner") não tinha o
   caminho de limpeza — corrigido.
4. **`CreateAdditionalSwapChain`** retorna `D3DERR_INVALIDCALL` (o jogo usa a
   swapchain implícita; o Bink copia frames por locks, caminho que não passa
   por swapchain adicional).
5. **Tradução de bytecode — correções de spec encontradas pelos testes**
   (ver §5.4 e tests/): bits de D3DSPSM (24-27, não 20-23), tipo de
   registrador em 5 bits (SHIFT2 bits 11-12), D3DSPC no token de instrução
   (SETP/IFC/BREAKC), D3DSP_DCL_USAGE nos bits 0-3, DCL do sampler com os
   valores D3DSTT (2=2D, 3=CUBE, 4=VOLUME), operando aL contíguo ao
   operando relativo, `loop` com dest (aL) + src, `rep` sem dest, tamanho do
   shader em DWORDs (incluindo END), comparadores do alpha test como código
   de instrução. Nada disso era visível sem um assembler de bytecode — o
   harness cobriu.
6. **Estrutura SPIR-V** corrigida contra `spirv-val --target-env vulkan1.1`:
   literal strings NUL-terminadas, OpEntryPoint com model+id+nome+interface
   nessa ordem, OpType*/OpLabel sem slot de result type, OpFunctionCall com
   result id, ArrayStride nos arrays de UBO (std140), `OpDot` escalar,
   `Grad` exige ExplicitLod, selection merge do `while` separado do merge do
   loop, blocks com terminador explícito (branch pós-STORE no fim de blocos),
   branch falso definido mesmo sem ELSE.
7. **BREAK dentro de IF** (comum em loops de iluminação): pulo direto ao
   merge do loop seria SPIR-V inválido dentro de selection; implementado o
   padrão de flag por loop (`brk_flag`) com cascata de verificação no merge
   de cada selection — mesmo comportamento observável do D3D9.
>>>>>>> 70f6572 (docs(native-render): analise do projeto e plano tecnico do backend Vulkan)
