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
| `AActor::Location` (FVector) | offset `+0x174` | — | desasm de `AActor::GetAimLocation` e `ASwatDoor::GetBreachAimLocation` (ambos usam `+0x174`) |
| `ASwatPlayer::PrivateStaticClass` (UClass*) | SwatGame.dll `0xab848` | `0x032AB848` | export `?PrivateStaticClass@ASwatPlayer@@0VUClass@@A` |
| `ASwatDoor::PrivateStaticClass` | SwatGame.dll `0xa6408` | `0x032A6408` | export `?PrivateStaticClass@ASwatDoor@@0VUClass@@A` |
| `AUseInterface::PrivateStaticClass` | SwatGame.dll `0x9ca40` | `0x0329CA40` | export `?PrivateStaticClass@AUseInterface@@0VUClass@@A` |
| `ASwatPlayerController::PrivateStaticClass` | SwatGame.dll `0xac370` | `0x032AC370` | export idem |

Consequência direta: **achar objetos reais por classe é um loop O(N) sobre
`GObjObjects` comparando `obj->Class == *(UClass**)PRIVATE_STATIC_CLASS`** —
sem heurística, sem nome, sem suposição de layout.

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
| 2 | Missão carregada vs menu/loading | **Feito** (etapa 2) | procurar `ASwatPlayer` em `GObjObjects` (classe exata) | confirmação em aparelho (próximo log) |
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
* Menu/loading ⇒ HUD invisível **e** transparente ao toque (o menu do jogo
  recebe os toques — corrige o problema real de botões sintetizados no menu).
* Toda nova constante de layout/estado citada no código aponta para a
  evidência em §1 (endereço/offset/desasm), nunca "achou que".
