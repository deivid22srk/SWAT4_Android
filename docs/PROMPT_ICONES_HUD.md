# PROMPT PARA IA DE GERAÇÃO DE IMAGENS — ÍCONES DO HUD CONTEXTUAL (SWAT 4 Android)

> **Como usar este arquivo:** cole o bloco `PROMPT` abaixo inteiro em uma IA
> geradora de imagens (ideal: uma que gere vários arquivos e saiba zipar —
> ex. agente com ferramentas de imagem + shell). O resultado esperado é um
> único `swat4-hud-icons.zip` contendo todos os PNGs + `manifest.md`, pronto
> para ser adicionado ao repositório `deivid22srk/SWAT4_Android`.

---

## PROMPT

Você vai gerar **todos os ícones do HUD de toque do SWAT 4 para Android**.
Hoje os botões do HUD mostram só o nome da ação em texto (FOGO, USAR, CORRER…).
Os ícones gerados vão **substituir esses rótulos** num overlay Java (Canvas/
PNG desenhado por cima do jogo), então precisam ficar legíveis **em fundo
escuro variável, em tamanhos pequenos (48–150 px) e em telas de celular**.

### Regras de estilo (valem para TODOS os ícones)

1. **Estilo**: linha militar tática ("tactical line icon"), traço uniforme,
   cantos levemente arredondados, sem gradiente, sem sombra, sem texto solto.
2. **Cores**: exatamente duas —
   - traço principal: branco off-white `#F2F4F6`;
   - acento tático âmbar `#E8A33D` (a mesma cor do destaque do menu do jogo),
     usada em UM elemento por ícone (ponto, seta ou preenchimento pequeno).
   Nada de azul/verde/roxo.
3. **Fundo**: 100% transparente (PNG-32 com alpha). Nada de quadrado de fundo.
4. **Grade**: canvas 128×128 px, ícone desenhado dentro de uma área segura de
   96×96 px centralizada; espessura do traço 8 px (no canvas 128). O ícone
   "FOGO" é o principal: gere em **192×192 px** com traço proporcional.
5. **Legibilidade**: o ícone deve ser reconhecível em 48 px de altura;
   formas fechadas > detalhes finos; nenhum elemento menor que 6 px de traço.
6. **Sem borda/contêiner**: nenhum ícone deve ter círculo/quadrado de moldura
   ao redor — o botão do HUD já desenha o próprio contêiner.
7. **Exporte 1 arquivo PNG por ícone**, nome exato como especificado abaixo,
   tudo em minúsculas, sem subpastas.

### Ícones e significados (o que cada botão faz no jogo)

Os significados abaixo vêm do mapeamento real do jogo (`User.ini` do
SWAT 4) — desenhe a metáfora correta para cada ação:

| Arquivo | Botão atual | Ação real no jogo | Como desenhar o ícone |
|---|---|---|---|
| `fogo.png` | FOGO | Atirar (mouse esquerdo) | **Mira/retículo**: círculo central + 4 traços curtos cardeais apontando para dentro + ponto central. Ponto central em âmbar. O ícone mais importante — forte e simples. |
| `cmd.png` | CMD | Abrir a interface gráfica de ordens (mouse direito) | **Megafone** inclinado 30° para cima à direita, com 3 arcos de som saindo da boca; o arco do meio em âmbar. |
| `usar.png` | USAR | Interação/compliance (mouse do meio: "IssueComplianceOrInteract") | **Mão indicando/toque**: mão de perfil com o dedo indicador estendido tocando um losango pequeno; o losango em âmbar. |
| `porta.png` | PORTA | Interagir com porta (F, só aparece perto de porta) | **Porta em perspectiva aberta** (retângulo vertical com batente + maçaneta) com uma **seta curta** entrando pela folha; seta em âmbar. |
| `rec.png` | REC | Recarregar (R) | **Pente/carregador** em diagonal com uma **seta circular** (rotação anti-horária) contornando-o; a ponta da seta em âmbar. |
| `zoom.png` | ZOOM | Alternar mira/zoom (Z) | **Luneta**: círculo grande com cruz fina interna e uma aba/almofada lateral de luneta; cruz horizontal em âmbar. Diferente do `fogo.png`: tem a aba lateral e sem ponto central. |
| `modo.png` | MODO | Próximo modo de tiro (X) | **Três balas** verticais lado a lado dentro de um arco de seletor (como um câmbio); a bala ativa (a da esquerda) em âmbar. |
| `agach.png` | AGACH | Agachar (C) | **Silhueta de pessoa agachada** de perfil (joelhos dobrados, 90°) com um chevron para baixo ao lado; chevron em âmbar. Silhueta em traço, não sólida. |
| `correr.png` | CORRER | Correr (Shift, segurar) | **Pessoa correndo** de perfil (pernas abertas em passada) com 3 linhas de velocidade atrás; as linhas em âmbar. |
| `incl.png` | INC< | Inclinar à esquerda (Q, segurar) | **Pessoa de perfil inclinando o tronco para a esquerda** + chevron curto para a esquerda; chevron em âmbar. |
| `incr.png` | INC> | Inclinar à direita (E, segurar) | Espelho exato de `incl.png` (inclinando à direita, chevron à direita). |
| `tab.png` | TAB | Próximo grupo de ordens (Tab) | **Três cartões empilhados** em leque com um chevron para a direita à frente; chevron em âmbar. |
| `esp.png` | ESP | Dar ordem padrão (Espaço) | **Balão de fala** com uma **estrela de 4 pontas** dentro (ordem rápida); estrela em âmbar. |
| `esc.png` | ESC | Menu/pausa (Esc) | **Duas barras de pausa** verticais + um chevron de retorno ("voltar") à esquerda das barras; chevron em âmbar. |
| `troca.png` | TROCA | Alternar agente (Delete = CycleOfficer) | **Duas silhuetas de policiais** (uma atrás da outra) com **setas de troca** em arco entre elas; seta de cima em âmbar. |
| `slots.png` | SLOTS | Abrir a grade rápida de itens/ordens | **Bandeja 2×2** com um dos quadrados "levantado" (como um slot aberto); o quadrado levantado em âmbar. |
| `dpad_up.png` | seta ↑ | Seta para cima (navegação) | **Chevron duplo** (dois V empilhados) apontando para cima, âmbar no de cima. |
| `dpad_down.png` | seta ↓ | Seta para baixo | Espelho vertical de `dpad_up.png`. |
| `dpad_left.png` | seta ← | Seta para a esquerda | Chevron duplo para a esquerda. |
| `dpad_right.png` | seta → | Seta para a direita | Chevron duplo para a direita. |
| `slot_1.png` … `slot_9.png`, `slot_0.png` | 1…9, 0 | Equipar arma/item da fileira numérica (`CommandOrEquip Row_NumberKeys`) | **Medalha circular** com o número grande no centro (tipografia condensada militar, ex. stencil); o ponto das 12h da medalha em âmbar. Numeral branco, traço fino âmbar no aro externo. Gere 10 arquivos: `slot_1.png`…`slot_9.png` e `slot_0.png`. |
| `fx_1.png` … `fx_10.png` | F1…F10 | Ordem da fileira de teclas de função (`CommandOrEquip Row_FunctionKeys`) | **Escudo quadrado de cantos chanfrados** com "F1"… "F10" centralizado; traço do chanfro em âmbar. 10 arquivos. |
| `toggle_num.png` | "NUM" | Alternar a grade para a fileira numérica | Chip/cápsula com "**123**" e um chevron pequeno; "123" em âmbar. |
| `toggle_fx.png` | "FX" | Alternar a grade para a fileira F | Chip/cápsula com "**F1–F10**" abreviado como "F" e um chevron; "F" em âmbar. |

Total: **36 arquivos PNG** (16 principais em 128 px + `fogo.png` em 192 px +
4 setas + 10 numéricos + 10 de função + 2 toggles).

### Controles de qualidade (verifique antes de empacotar)

- Abra todos os PNGs e confira: fundo transparente (alpha 0 nas bordas),
  contorno suave (sem serrilhado grosso), as DUAS cores exatas
  (`#F2F4F6` e `#E8A33D`), nenhum texto além dos numerais e "F1…F10"/"123"/"F".
- Os pares espelhados (`incl`/`incr`, `dpad_left`/`dpad_right`) devem ser
  espelhos REAIS (mesmo traço, mesma espessura).
- `zoom.png` e `fogo.png` devem ser claramente diferentes entre si.

### Empacotamento (obrigatório)

1. Gere os 36 PNGs com os nomes exatos da tabela.
2. Crie um `manifest.md` listando: nome do arquivo → botão do HUD → ação real
   no jogo (copie da tabela acima) → tamanho em px do arquivo.
3. Compacte tudo (36 PNGs + `manifest.md`) em **um único arquivo
   `swat4-hud-icons.zip`** (sem pastas internas — os PNGs na raiz do zip).
4. Entregue o `swat4-hud-icons.zip` + este `PROMPT_ICONES_HUD.md` para irem
   juntos ao repositório `deivid22srk/SWAT4_Android` (sugestão de destino no
   repo: `android/hud-icons/`).

**Não invente botões extras, não gere variações alternativas e não use
fotos/realismo — são ícones vetoriais planos de linha.**

---

## Integração (para quem aplicar o resultado)

* Os PNGs substituem os rótulos desenhados em
  `android/app/src/main/java/io/github/deivid22srk/swat4/HudOverlay.java`
  (o campo `label` de cada `Btn`); os arquivos podem ser carregados de
  `hudicons/` ao lado do jogo (mesmo padrão do `hud.ini`) com fallback para o
  rótulo de texto quando o PNG não existir.
* Estados: o HUD já desenha "pressionado" (alpha/realce) sobre o botão — não
  é necessário gerar variante pressionada por enquanto.
* `porta.png` só aparece no HUD quando o contexto é "perto de porta"
  (door-gated) — nenhuma variante extra é necessária.
