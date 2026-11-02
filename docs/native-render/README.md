# native-render — backend Vulkan próprio do port SWAT4 Android

Backend gráfico **escrito do zero** para substituir (opcionalmente) o
dxvk-native neste port. Implementa a superfície D3D9 que o jogo consome
(15 interfaces COM, 297 métodos mapeados pela bridge do runtime) direto sobre
Vulkan 1.1, sem camada de tradução de D3D em runtime além da necessária
(shader bytecode → SPIR-V, feito aqui, em tempo de criação de shader).

**Status e limites (honestidade primeiro):** o que foi verificado neste
ambiente e o que ainda exige teste em aparelho estão listados no fim deste
arquivo e em `ANALISE.md` §5.4. O dxvk **continua sendo o backend default**
até o native-render ser validado em campo.

---

## 1. Como trocar de renderizador (sem recompilar)

A escolha acontece em runtime, em `runtime/renderer_host.cpp`, na ordem:

1. **Variável de ambiente** `LB_RENDERER=dxvk|native`
2. **Arquivo** `<pasta do jogo>/port/renderer.txt` — primeira linha, conteúdo
   `dxvk` ou `native`
3. **Default: `dxvk`** (fallback preservado; regra do projeto)

Para usar o native-render no aparelho:

```bash
adb shell "echo native > /sdcard/Android/data/<pacote>/files/game/port/renderer.txt"
# ou, para voltar ao dxvk:
adb shell "echo dxvk > .../port/renderer.txt"
```

A troca é por processo (vale a partir do próximo boot do jogo), igual a um
driver real. Nada é removido do dxvk: os dois backends coexistem no mesmo
binário (`libdxvk_d3d9` e `libnative_render` linkados no `libmain.so`; o
símbolo de entrada do native-render é `nvr_Direct3DCreate9`, de propósito com
outro nome).

## 2. Layout do módulo

```
android/app/jni/native-render/
  include/nvr_api.h        → nvr_Direct3DCreate9 / nvr_set_cache_dir / nvr_version_string
  src/
    nvr_common.*           → log (logcat/stderr), config INI, utilidades
    nvr_format.*           → D3DFORMAT ↔ VkFormat, consulta de suporte real
    vulkan/
      nvr_vk.*             → VkCore (instance/device/queues), Swapchain
                             (pre-rotation, SUBOPTIMAL/OUT_OF_DATE, recreate),
                             FrameCtx (2-3 frames in flight, staging ring
                             persistente por frame), VMA
      nvr_gfx.*            → caches: render passes (LOAD/STORE ops escolhidos
                             por estado real do frame), samplers, pipelines
                             (VkPipelineCache + cache em disco), descriptor
                             sets (cache por frame), mipgen, upload/readback
      nvr_fb.cpp           → cache de framebuffers por (view, render pass)
    shader/
      nvr_spirv.*          → emissor SPIR-V 1.0 binário (sem toolchain externa)
      nvr_dx9bc.*          → parser do bytecode D3D9 SM2/SM3 (tokens, srcmods,
                             writemasks, endereçamento relativo aL)
      nvr_compile.*        → tradutor bytecode → SPIR-V (VS/PS; registradores
                             = variáveis Private; subrotinas via OpFunctionCall;
                             alpha test injetado; texkill estruturado)
      nvr_fff.*            → pipeline fixed-function (T&L completo, 8 luzes,
                             fog vertex/table, TCI, texture transform, cadeia
                             de TextureStageState com todos os ops relevantes)
    d3d9/
      nvr_d3d9.cpp         → IDirect3D9: enumeração, Check*, GetDeviceCaps
      nvr_device*.cpp      → IDirect3DDevice9: ciclo de vida, Reset, Clear,
                             present (blit escala + gamma ramp + pre-rotation),
                             locks de buffers, state blocks (gravação real),
                             queries (occlusion/event/timestamp)
      nvr_draw*.cpp        → draws (incl. UP e TRIANGLE_FAN via índices
                             gerados), construção de pipelines (estado D3D9 →
                             VkGraphicsPipeline), empacotamento de constantes
      nvr_res.cpp          → texturas 2D/cube/volume, superfícies, Locks com
                             shadow, blits (StretchRect/UpdateSurface/
                             GetRenderTargetData/GetFrontBufferData)
      nvr_pack.cpp         → empacotamento dos UBOs fixed-function
  thirdparty/
    vma/                   → Vulkan Memory Allocator 3.x (Apache-2.0)
    spirv_inc/             → headers SPIR-V (Khronos)
  tests/
    nvr_test_shaders.cpp   → harness desktop: monta bytecode D3D9 real e
                             valida o tradutor (15 casos)
    run_shaders.sh         → roda o harness + spirv-val em cada módulo gerado
```

## 3. Como o módulo é construído

- **Android/CI**: o `android/app/jni/CMakeLists.txt` faz
  `add_subdirectory(native-render/src)` e o `libmain.so` linka
  `native_render` (além do `dxvk_d3d9`). O Gradle empacota as duas `.so`
  via `externalNativeBuild` — nenhuma mudança de recipe foi necessária.
- **Desktop (verificação)**: `tests/run_shaders.sh` compila o harness da
  camada de shader com g++ e valida cada módulo SPIR-V emitido com
  `spirv-val --target-env vulkan1.1` (sem GPU). O CI roda isso no job
  `shader-tests` do workflow `build.yml`.
- Sem `glslc`/`glslang` no build: o SPIR-V é emitido por código próprio
  (`nvr_spirv.cpp`) e validado offline. Vantagens: determinismo, cache por
  hash trivial e zero dependência nova no NDK.

## 4. Decisões técnicas (resumo; detalhes em ANALISE.md)

| Tema | Decisão |
|---|---|
| Surface | `SDL_Vulkan_CreateSurface` sobre a mesma janela da bridge (igual ao dxvk) |
| Pre-rotation | `preTransform = currentTransform`; rotações de 90/180/270° viram rotação do blit do Present |
| Resolução | backbuffer do jogo (0.2–1.0× da janela) é escalado no Present (blit + gamma ramp) |
| Shaders | bytecode SM2/SM3 do hóspede → SPIR-V próprio; `s[aL]` dinâmico → erro nomeado no log |
| FFF | VS/PS gerados por assinatura de estado (hash); mesmos bindings dos shaders SM3 |
| Tile-based | 1 render pass por troca de RT; `LOAD_OP_DONT_CARE` no backbuffer no 1º uso do frame; clear pendente vira `LOAD_OP_CLEAR`; 2 frames in flight; staging ring persistente |
| Depth | D24S8 → D24S8/D32S8/D16S8 consultado no device real (stencil preservado para shadow volumes) |
| BC1/2/3 | nativo quando o device tem `textureCompressionBC`; senão a bridge do runtime já entrega RGBA decodificado (caminho `bcemu` existente) |
| Caches | `VkPipelineCache` + blob em `<jogo>/port/native-render/pipeline_cache.bin` (salvo a cada 1024 frames e no shutdown) |
| SWVP | o vertex shader do hóspede roda na GPU (por definição de SWVP o resultado é o mesmo) |
| `ProcessVertices` | **não implementado**: retorna `D3DERR_INVALIDCALL` com log nomeado. Sem evidência de uso pelo jogo (SM3/T&L por hardware) e a conversão para o FVF de destino é maquinaria grande; erro explícito, não sucesso falso |
| `CreateAdditionalSwapChain` | `D3DERR_INVALIDCALL` (o jogo usa a swapchain implícita; o Bink do jogo copia por locks) |
| Observabilidade | log de estatísticas a cada 64 frames (draws, pipelines criados/acionados) + logs nomeados em cada falha de tradução |

## 5. O que foi VERIFICADO neste ambiente

- **CI verde na branch** (`build.yml`, commits a partir de
  `feature/native-vulkan-renderer`):
  - **job `shader-tests`**: monta bytecode D3D9 real (10 casos de VS/PS: m4x4,
    DEF/DEFI/DEFB, loop/aL, break dentro de if, rep, mova, setp/breakp,
    call/label, sincos, pow, texld/texldl/texldb/texldd/proj/texkill, dp2add,
    dsx/dsy, cmp/lrp/cnd, srcmods, writemasks parciais, alpha test) + 5
    assinaturas fixed-function (point/spot/directional, fog linear/exp/exp2
    vertex e table, TCI camera-space, projected, texture matrix, MULTIPLYADD/
    LERP/DOTPRODUCT3, alpha test) e valida **20/20 módulos SPIR-V** com
    `spirv-val --target-env vulkan1.1`.
  - **job `apk`**: build Android completa no NDK 27.2 (arm64-v8a) — o módulo
    compila com clang, `libnative_render.so` linka com `--no-undefined`
    (771+ símbolos exportados, nenhum indefinido) e o Gradle produz o APK
    app-only com dxvk e native-render dentro do `libmain.so`.
- Host: os 18 objetos compilam com g++ `-Wall -Wextra`; link estrito
  (`-Wl,--no-undefined`) com SDL2 real 2.30.11 e loader Vulkan do sistema.
  `runtime/renderer_host.cpp` compila isoladamente.
- O histórico de correções que o CI/estrito expôs (caminhos relativos do
  CMake, aritmetica em `void*`, narrowing de flags, 7 métodos declarados sem
  definição, `ROOT` do harness) está nos commits da branch — cada um validado
  no CI antes do seguinte.

## 6. O que NÃO foi verificado aqui (exigiria aparelho)

1. **Execução real em GPU** (Adreno/Mali/PowerVR): criação de device,
   swapchain, present, timings. O ambiente desta sessão não tem Android nem
   ICD Vulkan instalável (sem root); a camada abaixo do harness não roda aqui.
2. **O jogo em si** (boot, menu, missões): depende do APK completo com o
   conteúdo do jogo (o CI público só monta o app-only).
3. **Performance** (frame time, temperatura): precisa de medição de campo
   (`port/log.txt` já tem o canal; o módulo emite as estatísticas).
4. **Caminhos raros de D3D9** exercitados pelo jogo apenas sob condições
   específicas (GetFrontBufferData, VolumeTexture com mipgen, two-sided
   stencil com o estado exato do jogo).

O plano de validação em campo está no fim de `ANALISE.md`.
