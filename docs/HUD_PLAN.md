# HUD Contextual — mapa de viabilidade e plano de etapas

Cada item abaixo foi verificado contra **evidência real** (binários do jogo da
cópia GOG SHA-1 `94d1a40f…`, `PlayerInterface_*.ini`/`DefUser.ini` da mesma
cópia, e o runtime do port). Nada aqui é suposição sem fonte.

## 1. O que o port já permite (fundação, medido no código)

* Toda a memória do jogo é um flat 4 GiB em `MEM` (`runtime/x86.h`): o runtime
  **lê qualquer dado do jogo** com `M32(guest_addr)`.
* O runtime pode **chamar qualquer função recompilada** via `rt_invoke`
  (`runtime/dispatch.c`) — inclusive os exports do jogo, porque os RVAs das
  tabelas de export são preservados nas tabelas `gen/*_table.c`.
* O overlay já desenha no `Present` e injeta input real por `vinput`
  (teclas DIK + botões de mouse que o shim de DirectInput entrega ao jogo).

### Símbolos do jogo pinados por desassembly (evidência)

| Símbolo | RVA (PE original) | Endereço guest no port | Como foi verificado |
|---|---|---|---|
| `UObject::GObjObjects` (TArray de UObject*) | Core.dll `0x294354` | `0x01294354` | export `?GObjObjects@UObject@@0V?$TArray@PAVUObject@@@@A` |
| `FNameEntry** Names` | Core.dll `0x28c2c0` | `0x0128C2C0` | desasm de `GetFullName` (usa `Names[idx]`) |
| `UObject::Class` | offset `+0x24` | — | desasm de `IsA` (`mov ecx,[ecx+0x24]`) e `GetFullName` |
| `UObject::Name.Index` | offset `+0x20` | — | desasm de `HashObject` (`[ecx+0x20] & 0xfff`) e `GetFullName` |
| `UObject::Outer` | offset `+0x18` | — | desasm de `GetPathName` (Core.dll RVA `0x2d47`, corpo em `0x1683e0`): `mov 0x18(%edi),%ecx` e recursão; a mesma função confirma Name em `+0x20` e a string da FNameEntry em `+0xC` |
| `AActor::Location` (FVector) | offset `+0x174` | — | desasm de `AActor::GetAimLocation` e `ASwatDoor::GetBreachAimLocation` (ambos usam `+0x174`) |
| `ASwatPlayer::PrivateStaticClass` (UClass*) | SwatGame.dll `0xab848` | `0x032AB848` | export `?PrivateStaticClass@ASwatPlayer@@0VUClass@@A` |
| `ASwatDoor::PrivateStaticClass` | SwatGame.dll `0xa6408` | `0x032A6408` | export `?PrivateStaticClass@ASwatDoor@@0VUClass@@A` |
| `AUseInterface::PrivateStaticClass` | SwatGame.dll `0x9ca40` | `0x0329CA40` | export `?PrivateStaticClass@AUseInterface@@0VUClass@@A` |
| `ASwatPlayerController::PrivateStaticClass` | SwatGame.dll `0xac370` | `0x032AC370` | export idem |

Consequência direta: **achar objetos reais por classe é um loop O(N) sobre
`GObjObjects` comparando `obj->Class == *(UClass**)PRIVATE_STATIC_CLASS`** —
sem heurística, sem nome, sem suposição de layout.

*Atualização de campo (hhklog, 2026-11-01): o slot de
`ASwatPlayer::PrivateStaticClass` (endereço e RVA conferidos no PE — export
real, seção .data, SHA do DLL igual ao da cópia GOG) leu **0x010a5320** —
um ponteiro da imagem da Core.dll — durante uma sessão inteira no aparelho,
abortando todo scan. O slot agora é só caminho rápido validável; o que não
vier dele é **descoberto por nome** no próprio `GObjObjects` (UClass
"SwatPlayer"/"SwatDoor" via auto-cadeia da metaclasse + tabela de Names) —
o array do engine é a fonte da verdade, não um dword de .data.*

### Binds reais do jogo (DefUser.ini, cópia GOG)

| Tecla | Comando real | Uso no HUD |
|---|---|---|
| `F` / `MiddleMouse` | `IssueComplianceOrInteract` | botão USAR — **o próprio jogo decide** o que fazer (porta/cumprir lei/interagir) |
| `G` | `Grab` | prender/pegar (evidência, imóvel, algemado) |
| `Botão direito` | `OpenGraphicCommandInterface` | botão CMD |
| `1..0` / `F1..F10` | `CommandOrEquip Row_… N` | grade SLOTS — comando OU equipamento conforme o estado do jogo |
| `Espaço` | `GiveDefaultCommand` | botão ESP |
| `X` | `NextFireMode` | botão MODO |
| `Z` | `ToggleZoom` | botão ZOOM |
| `R` | `Reload` | botão REC |
| `C`/`Q`/`E`/`Shift` | agachar/inclinar/correr | já mapeados |
| `Delete` | `CycleOfficer` | botão TROCA |
| `V` | `ToggleFlashlight` | candidata a botão novo |
| `\` | `ChangeHud` | alterna tamanho do HUD nativo |

### O jogo já é contextual por dentro (dados, não código novo)

`PlayerInterface_Use.ini` / `PlayerInterface_Command_SP.ini` definem o motor de
contexto do jogo: um *trace de foco* (Range=2000; portas com Range=128) testado
contra contextos em ordem (`LockedWedgedDoor`, `LockedDoor`, `WedgedDoor`,
`ClosedDoor`, `OpenDoor`, `Evidence`, `RestrainedEnemy`, `CanBeArrested`,
`MirrorPoint`, `DeployedWedge`, `ActiveBomb`, …) com feedback textual ("Locked
Door.", "Open Door"). **O resultado desse cálculo é estado do jogo**; o HUD
pode lê-lo em vez de reimplementar as regras.

## 2. Mapa de viabilidade por contexto (o PROMPT pediu honestidade)

| # | Contexto | Viabilidade | Base técnica real | Falta |
|---|---|---|---|---|
| 1 | Andando/explorando (HUD mínimo) | **Feito** (etapa 1) | overlay com stick/look/16 botões reais (15 fixos + 1 contextual) | — |
| 2 | Missão carregada vs menu/loading | **Feito** (etapa 2 + rodada de campo 2026-10-31) | procurar `ASwatPlayer` em `GObjObjects` (classe exata) + **discriminador do nível Entry**: pawn cujo `Outer→Outer` é o pacote "Entry" (nome via `Names[]`, ANSI/UTF-16) é o fundo do menu, não missão — log do aparelho: `LoadMap: Entry`, `Level is Level Entry.myLevel` | confirmação em aparelho (próximo log: linha `overlay: ctx …` por virada de contexto) |
| 3 | Perto de porta | **Feito** (etapa 3) | `ASwatDoor` instances + `Location@+0x174` + distância ao pawn, Range=128 do próprio jogo | confirmação em aparelho |
| 4 | Estado da porta (fechada/trancada/cedida) | **Parcial** | campos de estado do `ASwatDoor` precisam ser pinados (RE dirigido nos accessors exportados, ex. `?IsOpen`/beliefs) | RE + validação em aparelho |
| 5 | Menu de porta (ações por tecla) | **Parcial** | abrir o menu é real (`F`/`Interact`); o mapeamento das opções do menu por tecla precisa de confirmação (RE no `UseInterface`/script) | RE |
| 6 | Suspeito/civil focado | **Parcial→Viável** | instâncias de pawn inimigo/civil por `PrivateStaticClass` (exports existem para `ASwatEnemy`/`ASwatHostage`); ações reais = `F` (compliance/interact) + `G` (grab) | pinar `IsArrested`/estado (export `?IsArrested@ASwatPawn@@UAEHXZ` chamável via `rt_invoke`) |
| 7 | Evidência | **Parcial** | mesma técnica (classe do pickup de evidência); ação real = `G` | achar a classe certa |
| 8 | Arma/munição/modo de tiro (exibir) | **Parcial** | teclas de ação reais (`X`/`R`/`Z`); **exibir** munição exige pinar item ativo + contagem (RE dirigido) | RE |
| 9 | Comando de esquadrão | **Feito** (etapa 1) | binds reais: CMD abre a interface gráfica; 1-0/F1-F10 comandam; ESP = comando padrão | — |
| 10 | Vida ferida | **Parcial** | o jogo tem regiões esqueléticas (`SkeletalRegionInformation.ini`); campos de vida do player precisam de RE | RE |
| 11 | Fim de missão/pause | **Viável** (derivado) | sem `ASwatPlayer` = tela de menu/debriefing → HUD some | idem etapa 2 |

Bloqueio declarado (honesto): os itens "Parcial" dependem de **RE dirigido por
exports** (cada campo ~1 desassembly) e de **confirmação em aparelho**; nenhum
deles é impossível, mas nenhum entra no HUD antes de ter evidência.

## 3. Ordem das etapas (do mais simples/valioso ao mais complexo)

| Etapa | Entrega | Critério de aceitação |
|---|---|---|
| 1 ✅ | Fundação: stick/look/botões/multitouch/hud.ini | selftest 201 asserts; sem alocação por frame |
| 2 | **Consciência de missão**: HUD some no menu/loading (e para de capturar toques lá), volta na missão | selftest com imagem guest sintética; leitura a cada ≥1 s; cache validado por O(1) |
| 3 | **Contexto de porta**: linha de ação de porta aparece só perto de porta (distância real via `Location`) | selftest com portas sintéticas; botões reais (F + menu quando confirmado) |
| 4 | **Ações de porta** com estado (trancada/cedida) quando os campos forem pinados | evidência de desasm no docs |
| 5 | **Suspeito/civil/evidência** (foco/proximidade) | idem |
| 6 | **Exibição de munição/modo** | idem |
| 7 | **Vida/ferramentas** | idem |

## 4. Regras permanentes do HUD (do PROMPT, reafirmadas)

* Zero botão decorativo: todo botão dispara bind real do jogo (tabela §1).
* Zero alocação por frame; leitura de estado throttled (≥1 s) e cacheada.
* Menu/loading ⇒ HUD invisível **e** cada toque vira mouse real do jogo
  (WM_MOUSEMOVE + WM_LBUTTONDOWN/UP com coordenadas de cliente, cursor
  virtual no `GetCursorPos`, botão espelhado no DirectInput) — o menu do
  jogo passa a receber cliques de verdade (rodada de campo 2026-10-31).
* Toda nova constante de layout/estado citada no código aponta para a
  evidência em §1 (endereço/offset/desasm), nunca "achou que".

## 5. Rodada de campo 2026-10-31 (build #38, log ebcclog) — causas e correções

Sintomas do usuário: menu (Career/Training) não clica, vídeos não pulam,
analógico "enorme" e invisível até arrastar, botões irreconhecíveis.

Evidência no log + vídeo:
* `overlay: init ok` aparece **uma única vez** (75 quads) e nenhuma linha
  `hud hidden/shown` no log inteiro → o overlay congelou após o primeiro
  frame.
* O DXVK registra **dois** Presenters; o segundo (FIFO) surge logo após o
  primeiro Present **sem Reset** no meio → `CreateAdditionalSwapChain` do
  jogo: os frames seguintes (filmes, menu) saem pela swapchain adicional,
  onde o overlay não desenhava — mas o pump de eventos continuava
  consumindo os toques com o contexto do primeiro frame.
* `GetCursorPos` shim sempre devolvia (0,0) e nenhum WM de mouse era
  postado → o cursor da GUI fica parado no canto (visível no vídeo) e
  nenhum clique chega ao jogo.
* Analógico: raio `0.16*h` = 173 px no aparelho (346 px de diâmetro).
* `labels=0` por padrão → caixas escuras sem texto.

Correções (todas com selftest):
1. `touch_set_device` capturado em `CreateAdditionalSwapChain`;
   `touch_draw_frame()` chamado também em `IDirect3DSwapChain9::Present`
   (dedupe por ms para não desenhar duas vezes no mesmo frame).
2. Papel `R_MMENU`: com o HUD oculto, cada toque vira mouse real
   (`vinput_menu_move/button` → WM_MOUSEMOVE/WM_LBUTTONDOWN/UP via
   `user32_mouse_msg`, cursor virtual lido pelo shim `GetCursorPos`,
   botão espelhado no DI para os caminhos que pollam o device — pular
   vídeo incluso). Imprensas soltas: flip de contexto, resize e perda de
   foco sempre soltam o clique (nunca trava pressionado).
3. Discriminador Entry (§2 item 2) + log de diagnóstico por virada de
   contexto (`overlay: ctx … pawn=… doors=… [Entry pawn ignored]`).
4. Analógico: raio base `0.105*h` (238 px @1080p) + **fantasma de repouso**
   desenhado na zona (a bolha flutuante só existia sob o dedo).
5. `labels=1` por padrão; `hud.ini` agora carimba `version=2` e arquivos
   pre-versão são substituídos uma vez pelos novos padrões.
6. `SDL_WINDOWEVENT_LEAVE` não solta mais toques (Android dispara
   espuriamente — log: `window event 11`); FOCUS_LOST/MINIMIZED cobrem os
   casos reais.
7. `user32_set_screen` re-sincronizado com o tamanho REAL da janela após o
   recreate pós-splash (display 2400x1080 vs janela 2285x1080 no aparelho),
   para as coordenadas do mouse de menu baterem 1:1.
