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
