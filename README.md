# SWAT 4 (PC, 2005) para Android

Port **nativo** (arm64) não oficial do *SWAT 4* de PC por **recompilação
estática**: o código x86 do jogo é convertido para C em tempo de build e
compilado para ARM64 com o Android NDK. Não há Wine, Box64, FEX nem emulador
x86 em tempo de execução.

Este repositório **não contém código nem dados do jogo**, e nunca vai conter.
Você precisa da sua própria cópia do PC; o C gerado a partir dela (`gen/`) fica
em um repositório **privado** separado, `deivid22srk/SWAT4_codegen`.

> ## Status: início, e ainda não jogável
>
> O reconhecimento, o mapa de módulos, o pipeline de discovery/lifting e o app
> Android existem. O APK **app-only** foi gerado no CI e contém o launcher que
> valida a pasta do jogo; a **instalação em aparelho não foi testada** (não há
> Android neste ambiente). O jogo **não** foi levado ao menu principal. Veja a
> tabela abaixo e `docs/TESTED.md` para o que foi realmente executado.

## Status

| Área | Estado | Evidência |
|---|---|---|
| Identificação (engine, versão, hashes, imports) | **Feito** | `docs/RECON.md`, `docs/IMPORTS.md` — medido nos binários fornecidos |
| DRM / packing | **Feito** | GOG DRM-free; sem SafeDisc/SecuROM/packer (`docs/RECON.md` §2) |
| `docs/REFERENCE.md` (Fase 0, OpenSA2) | **Feito** | o OpenSA2 real usa Remill/LLVM; o enunciado supunha outro pipeline |
| Discovery (`tools/discover.py`) | **Feito no conjunto base** | números em `docs/RECON.md` §12 |
| Lifting (`tools/lift.py`) | **Executado no conjunto base** | ver `docs/TESTED.md` |
| Runtime Win32 (kernel32/user32/advapi32/msvcrt/ole32/…) | **Parcial, ampliado** | wide-char/stdio/`std::string`/arquivos/eventos; import sem shim aborta com o nome |
| Boot desktop (Linux x86-64) | **Avançado** | 33 módulos, `appInit`, **D3D9/DXVK v1.9.2 inicializa**; sai com `exit(0)` (ver `docs/TESTED.md`) |
| Gráficos D3D9 → Vulkan (dxvk-native) | **Vendorizado, não validado** | `D3DDrv.dll` importa `Direct3DCreate9`; `vs_3_0`/`ps_3_0` |
| Áudio OpenAL 1.x + EAX + Vorbis | **Inicializa no desktop** | alias `OpenAL32`→`DefOpenAL32`; device ALAudio sobe; EAX/voz não validados |
| Vídeo Bink | **Módulo no mapa, não validado** | `binkw32.dll`, `FBinkMovie` |
| Launcher (pasta, tamanho + SHA-1, lista do que falta) | **Implementado, não testado em aparelho** | `LauncherActivity.java`; classes presentes no APK |
| Overlay de toque | **Etapa 1 implementada** (base jogável) | `runtime/touch_swat4.cpp` + `runtime/vinput.c`; selftest `scripts/test_touch.sh` no CI |
| CI (`build.yml`) | **Verde nos dois modos** | app-only público (APK 2 MB); completo privado (APK 24 MB, `libmain.so` 56 MB, 80.848 funções) |
| **Jogo chega ao menu principal** | **Não** | não há otimismo aqui |

## Como funciona

1. **Build-time:** `tools/discover.py` encontra funções por descida recursiva
   (capstone) e `tools/lift.py` traduz cada função x86-32 para C. O resultado é
   `gen/<módulo>_*.c` + tabelas de endereços.
2. **Runtime:** `runtime/` mantém os endereços guest, carrega os PEs do jogo na
   memória do processo, resolve imports (`dll!nome`) para shims do host ou para
   exports recompilados, e fornece D3D9 (dxvk-native), áudio, input e arquivos.
3. **App:** launcher Java escolhe a pasta, valida tamanho/SHA-1 e inicia
   `libmain.so` via SDL2.

O mapa de módulos e os endereços de carga verificados estão em
`docs/RECON.md` §3 e em `tools/swat4_modules.py` (fonte única).

## Arquivos necessários

Só a build **GOG Gold base** (`Swat4.exe` com o SHA-1 abaixo) é suportada. A
expansão *The Stetchkov Syndicate* ainda não é suportada (os módulos dela têm
outro conjunto e `ALAudio.dll` diferente).

| Arquivo | Tamanho | SHA-1 | Para quê |
|---|---|---|---|
| `Content/System/Swat4.exe` | 180.224 | `94d1a40f8f60b6664f539cab4c38a444b7c4a1e7` | entrada (validado pelo launcher) |
| `Content/System/Core.dll` | 1.093.632 | `1cefb028ec7f67dc7cdf0e1456744e4dd0579819` | engine core + VM UnrealScript |
| `Content/System/Engine.dll` | 6.615.040 | `6751da119558c747ce51d45fab668a13c929322a` | engine |
| `Content/System/Window.dll`, `WinDrv.dll` | — | ver `docs/RECON.md` | janela/input |
| `Content/System/D3DDrv.dll`, `RTCShader.dll` | — | ver `docs/RECON.md` | renderer D3D9 |
| `Content/System/ALAudio.dll`, `DefOpenAL32.dll` | — | ver `docs/RECON.md` | áudio |
| `Content/System/binkw32.dll` | 338.944 | `c8fbcdb9feced291bed8b91bc4a2bf40a7dcc954` | vídeo Bink |
| `Content/**` (`.u`, `.utx`, `.ukx`, `.usx`, `.uax`, `.ogg`, `.bik`) | 1,7 GB | — | dados do jogo |
| `ContentExpansion/**` | 766 MB | — | expansão (opcional; ainda não suportada) |

Instalação total: **2,5 GB** (medido). Use a pasta que contém `Content/` e
`ContentExpansion/` — a raiz da instalação GOG.

## 1. Gerar o `gen/` (na sua máquina, com a sua cópia)

```sh
python3 -m pip install capstone==5.0.9 pefile==2024.8.26
tools/regen_gen.sh "/caminho/para/SWAT 4"
```

Isso roda discovery + lifting para os módulos de `tools/swat4_modules.py` e
escreve `gen/` e `analysis/` (ambos ignorados pelo git). Tempo/RAM medidos estão
em `docs/RECON.md` §12.

## 2. Enviar o `gen/` para o repositório privado

```sh
gh repo create deivid22srk/SWAT4_codegen --private   # uma vez
GEN_REPO=deivid22srk/SWAT4_codegen GEN_TAG=gen-v1 tools/push_gen.sh
```

O script **recusa** o envio se `gh repo view --json isPrivate` não disser
`true`. Ele publica `gen.tar.zst` como asset de uma release privada.

## 3. Build do APK

O APK é compilado **somente** no GitHub Actions (`.github/workflows/build.yml`).

* **App-only** (`GEN_REPO` não definido): usa `ci/gen_stub/`, publica um
  artefato público `SWAT4-…-apponly-….apk`. Instala e valida a pasta, mas não
  roda o jogo.
* **Completo** (`GEN_REPO` + `GEN_TOKEN` definidos): baixa `gen.tar.zst` da
  release privada, extrai em `gen/`, compila e publica o APK **somente** em uma
  release privada do `GEN_REPO`.

Configuração no repositório público:

```sh
gh variable set GEN_REPO --body "deivid22srk/SWAT4_codegen" --repo deivid22srk/SWAT4_Android
gh variable set GEN_TAG  --body "gen-v1" --repo deivid22srk/SWAT4_Android
gh secret set GEN_TOKEN --repo deivid22srk/SWAT4_Android   # token só de leitura do privado
gh workflow run build.yml --repo deivid22srk/SWAT4_Android
```

Assinatura: o build usa o **keystore de debug** (o APK não é para distribuição).
Se quiser um keystore próprio, adicione os secrets e um `signingConfig` no
`android/app/build.gradle`.

## Instalação

Requisitos: arm64, Android 9+ (API 28), Vulkan 1.1, ~2,5 GB livres para o jogo.
**RAM mínima ainda não foi medida** — não confie em número nenhum aqui até
`docs/TESTED.md` registrar uma medição.

1. Instale o APK.
2. Copie a instalação do PC para uma pasta normal (ex.:
   `Armazenamento interno/Games/SWAT4`). **Não** para `Android/data`.
3. Abra o app e escolha a pasta que contém `Content/`. O launcher verifica
   tamanho + SHA-1 do `Swat4.exe` e lista DLLs faltando.
4. Toque em Play.

Logs e saves ficam em `<pasta do jogo>/port/`: `log.txt` (reescrito a cada
execução), `last_exit.txt` (por que a execução anterior terminou) e `save/`.

## Controles

Bindings **reais** do PC, lidos de `Content/System/DefUser.ini`:

| Ação | Tecla PC |
|---|---|
| Atirar | botão esquerdo |
| Menu gráfico de comandos do esquadrão | botão direito |
| Compliance / interagir | botão do meio |
| Comando padrão | Espaço |
| Próximo grupo do esquadrão | Tab |
| Voltar no menu de comandos | Backspace |
| Inclinar esquerda/direita | Q / E |
| Agachar | C |
| Correr | Shift |
| Recarregar | R |
| Próximo modo de tiro | X |
| Zoom | Z |
| Equipar/comandar | 1–0, F1–F10 |
| Trocar oficial | Delete |
| Mirar (eixo) | mouse |

Mapeamento Android implementado (overlay de toque, etapa 1): stick virtual
flutuante no canto inferior esquerdo = movimento (setores digitais WASD, com
deadzone e histerese — o jogo PC só aceita movimento digital); arrastar em
qualquer outra área = olhar/mirar (deltas de mouse, sensibilidade e inversão
configuráveis). Botões: FOGO = atirar (segurar); CMD = menu de comandos do
esquadrão, equivalente ao botão direito (segurar e clicar no menu do jogo);
USAR = compliance/interagir (botão do meio); REC = recarregar; AGACH = agachar;
CORRER = correr (segurar); ZOOM; MODO = modo de tiro; INC< / INC> = inclinar
(segurar); ESC = pausa/menu; ESP = comando padrão; TAB = próximo grupo;
TROCA = trocar oficial; SLOTS = grade de equipamento (1–0 e F1–F10, duas
páginas). Controle físico e teclado/mouse continuam suportados via SDL2.

### Ajustes do overlay (`<pasta do jogo>/port/hud.ini`)

Criado com os defaults na primeira execução; edite e reinicie o jogo.
`scale` (0.5–2.0) tamanho dos botões; `opacity` (0.15–1.0); `look_sens`
(0.2–3.0) sensibilidade do olhar; `look_invert_y`; `handed` (1 = canhoto);
`labels` (0/1 textos nos botões); `stick_radius` (0.7–1.6); `hold_crouch` e
`hold_zoom` (AGACH/ZOOM enviam toque de 120 ms por padrão, o modo toggle do
jogo; mude para 1 se o seu User.ini usar agachar/zoom por pressionar).

## Troubleshooting

* `port/log.txt` é reescrito a cada execução; `port/last_exit.txt` sobrevive.
* "unsupported build": o `Swat4.exe` não é o SHA-1 suportado.
* "import X has no shim yet": falta um shim Win32 no runtime; o log nomeia a
  API. É bug do port, não algo que se resolva no celular.
* "missing from …": a pasta escolhida não é a raiz da instalação (tem que
  conter `Content/System/Swat4.exe`).

## Problemas conhecidos

* O jogo **não chega ao menu principal**.
* O runtime cobre só parte das 1.201 APIs de SO importadas; as demais abortam
  com o nome.
* D3D9/dxvk, OpenAL, Bink e input não foram validados em aparelho.
* O overlay de toque (etapa 1) está sempre visível em jogo: não há detecção de
  menu/estado do jogo ainda (etapa 3); botões como FOGO enviam cliques que
  também interagem com menus do jogo. Sem menu de comandos do esquadrão
  desenhado pelo overlay (o menu do jogo é usado via CMD).
* Expansão não suportada.
* GameSpy/multiplayer e o cliente de anúncios Massive estão fora do escopo
  (stubs).
* SEH/x87 e chamadas indiretas da VM UnrealScript ainda não foram validados.

## Legal

*SWAT 4* e todos os nomes e assets relacionados pertencem aos seus respectivos
titulares (Sierra / Activision / Irrational Games). Este projeto não é afiliado
nem endossado por eles. Não redistribua nada produzido a partir do jogo nem os
arquivos do jogo. Código de terceiros mantém suas licenças: SDL2 (zlib) em
`android/app/jni/SDL2`, dxvk-native (zlib) em `android/app/jni/dxvk`.