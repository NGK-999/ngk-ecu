# Referência de interface e orçamento da tela

## Origem

Orientação fornecida pelo responsável e inspeção da captura local
`firmware/karl-panel/artifacts/connectivity/ui/overview.png`, dos componentes
`firmware/karl-panel/src/main.cpp`, do layout `include/ui_layout.hpp` e do
`platformio.ini` da referência karl-panel. Esses arquivos estão numa árvore
local de outro projeto, não nesta base publicada. Não foram copiados para cá.

Os [Apple Design Resources](https://developer.apple.com/design/resources/),
[Human Interface Guidelines](https://developer.apple.com/design/human-interface-guidelines/)
e a [sessão WWDC25 fornecida](https://developer.apple.com/br/videos/play/wwdc2025/356/)
são referências de composição/hierarquia. O catálogo oficial aponta para o kit
iOS/iPadOS 27 indicado pelo responsável; o arquivo Figma não pôde ser aberto
pelo navegador automatizado nesta sessão. Não tratá-lo como arquivo inspecionado.
Na ESP, manter Montserrat e ícones apropriados do LVGL; não incorporar fontes
ou componentes de plataforma Apple ao firmware.

## Cockpit atual — 0.4.6

O responsável considerou a composição 0.2.0 insuficiente e deu liberdade para
pesquisar outra solução. Confirmou que o touch estava funcionando. A revisão
adota uma composição contínua e assimétrica inspirada na hierarquia dos
instrumentos [FuelTech](https://www.fueltech.com.br/products/ft450-injecao-eletronica),
nos layouts [AiM MXm/MXq](https://aim-sportline.com/download/doc/eng/mxm-mxq/MXm-MXq_user_guide_eng.pdf)
e no formato320 × 240 do [MoTeC D153](https://www.motec.com.au/products/D153).
As medidas e decisões visuais são próprias; nenhum ativo dessas marcas foi copiado.

| Região | Tratamento |
| --- | --- |
| RPM superior | Número condensado e barra sólida em rampa, com graduação de cor |
| Velocidade e marcha no centro | Número maior à esquerda; marcha grande à direita somente em SIM; unidades separadas |
| Bloco e óleo na base | Temperatura legível e estado binário alinhados, sem pressão inventada |
| Menu | Quatro cartões 2×2: Painel, Sensores, Ajustes e Sistema |
| Ajustes | Seis categorias diretas; abas Unidades/RPM dentro de Instrumentos |

Na revisão 0.4.2, solicitada somente para ESP32: fundo `#000000`, superfícies
e botões `#101010`, texto `#F5F5F5`, secundário `#A0A0A0`, laranja `#FF6A00`,
atenção `#FF9B32`, crítico `#FF3B30` e divisórias `#212121`. Tema centralizado
em `ui_theme.hpp`; neutros conferidos após quantização RGB565.
Sem imagem de fundo, transparência animada ou sombra.
Cores de faixaRPM são referências visuais, não corte/proteção do motor.

Barlow Condensed Bold foi convertida em NGK Cockpit48/64/72, com apenas 12 glifos:
`-.0123456789`. As alturas de linha efetivas são 34/48/53 px. O painel atual usa 48 para RPM
e 72 para velocidade; cinco dígitos da 48 cabem em 115 px, três da 72 em 105 px.
A fonte 64 permanece para reprodução/histórico e não é ligada ao normal atual. Fontes residentes na flash,
4bpp sem compressão, sem carregamento do SD durante desenho. Licença e reprodução
em [third_party/licenses](../third_party/licenses/README.md).
Legendas continuam Montserrat12/14/16/28. Áreas numéricas usam largura do
conteúdo, preservando alinhamento e invalidando regiões antigas/novas.
Marcha usa NGK Gear64, oito glifos `-NR01234`, altura efetiva 48 px e baseline 1; não foi
habilitada uma fonte Montserrat48 completa. Proveniência e reprodução no mesmo
documento de licenças. A identidade do simulador Linux foi preservada.

Referências conferidas nesta revisão: [manual oficial FuelTech, página 20](https://files.fueltech.com.br/manual/Portugues/FT450_FT550_FT600.pdf#page=20),
[Apple HIG Layout](https://developer.apple.com/design/human-interface-guidelines/layout),
[Color](https://developer.apple.com/design/human-interface-guidelines/color) e
[Gauges](https://developer.apple.com/design/human-interface-guidelines/gauges).
FuelTech orienta a hierarquia de instrumentos; Apple, agrupamento/alinhamento
e semântica de cor. A composição e o código são próprios.

## Acabamento dos instrumentos — 0.4.3

Pedido aprovado após a pesquisa: padronizar ícones, fixar as referências dos
números/unidades e destacar alertas. O menu usa velocímetro, atividade,
deslizadores e processador, com os mesmos tamanhos/traços. São somente cinco
máscaras alpha4 derivadas do Lucide, incluindo triângulo de alerta; SVGs,
licença integral, commit, hashes e gerador em
[third_party/icons/lucide](../third_party/icons/lucide/README.md).

Os ícones são recursos constantes na flash, 2.308 B no objeto Xtensa, sem
renderizador SVG ou fonte adicional na placa. A cor vem do tema LVGL e os
filhos não capturam clique; os cartões mantêm alvos e a animação local anterior.

RPM e velocidade continuam com largura do conteúdo e buffers estáticos, mas
agora têm borda direita fixa. Isso mantém o alinhamento ao trocar dígitos sem
invalidar uma caixa numérica inteira por quadro. Unidades permanecem em labels
separados; temperatura exibe °C/°F. Não foram mudados valores ou conversões.

Faixa de alerta em y218..239, com ícone20 px e texto Montserrat14. Fundo
`#5C3200` para rotação alta e `#8F1D1D` para os demais alertas, com texto claro.
Ocupa o lugar da dica/TPS, preservando todos os instrumentos. Fonte SIM,
prioridades, histerese e validade continuam iguais; perda de link também
retira o contêiner inteiro. A faixa é estática enquanto o alerta não muda.

## RPM compacto e rampa maior — 0.4.5

Pedido baseado na nova imagem FuelTech: apertar o valor de RPM no topo e ampliar
o gráfico. O número agora usa Barlow48 (34 px de linha, antes 48 px), em uma faixa
120×34 no alto. RPM, estado da fonte e escala ficam compactos ao lado. A marca
continua no menu; a faixa do painel é dedicada aos instrumentos.

A rampa passou de 304×24 para 304×56 px, começando em y44 e terminando em y99.
Isso aumenta a altura em 133%, preservando velocidade/marcha, bloco/óleo e alerta
nas posições anteriores. Desenho por colunas sujas, borda Q8, fontes residentes
na flash e buffers DMA continuam iguais. A referência numérica permanece fixa.

## Aproveitamento do espaço — 0.4.6

A análise da captura 0.4.5 identificou vazios à esquerda da velocidade, acima da
marcha e entre a leitura de RPM e a curva. A região da velocidade agora tem 184 px;
sua borda numérica termina em x127, alinhada à do RPM, com unidade em x136.
A coluna da marcha tem 108 px e usa Gear64/linha 48, preenchendo a altura até y175.

Bloco/óleo usam y180..215, reduzindo o vão de 8 para 4 px após os instrumentos.
Temperatura usa a Barlow48 já residente (linha 34); estado do óleo, Montserrat14.
Continuam acomodados -40°C/F,392°F e estados de ausência, sem pressão inventada.

O TPS existente da fonte SIM foi levado ao espaço acima da curva à esquerda,
com texto de 14 px e barra horizontal de 120×4 px. Não aparece em BANCADA; continua visível
sob alertas sem esconder dados críticos. Inválido/stale mostra TPS --/barra vazia;
zero válido é TPS 0%. DEMO interno indica SIMULADO sem inventar TPS. A escala máxima do gráfico
é sempre visível e acompanha mudanças dos ajustes, inclusive com SIM ativo.

O recorte incremental da rampa agora limita também a altura, usando a coluna
mais alta entre as alteradas. Isso evita redesenhar a região do TPS em pequenas
variações de giro. A fórmula de altura é compartilhada com o desenho; host
comparou frames incrementais com redesenho completo para descidas/subidas,
frações e extremos. Sem imagem de tela inteira, fonte completa ou SD por quadro.

## Fluxos

Menu → Painel ocupa toda a tela, sem barra fixa de navegação. Duplo toque
retorna ao menu: dois contatos completos em até 400ms, hold até 350ms, arraste
até 18px e distância entre toques até 48px. Mudança de página somente fora do
callback LVGL. A calibração e os GPIOs do touch não foram modificados.

Ajustes abre Tela, Instrumentos, Toque/DEMO, PerfisSD, Wi-Fi e Bluetooth.
Salvar aparece apenas nos parâmetros visuais; rádios e SD exibem seus próprios
estados e ações assíncronas. Abas Unidades/RPM destacam a seleção. Voltar
preserva o rascunho; confirmação de gravação não apaga edições posteriores.
DEMO abre diretamente o painel, fica identificado e não persiste após boot.

Alvos de pelo menos 44×44px, cliques e páginas, sem arraste obrigatório.
Teclado local paginado com senha mascarada. Pareamento recebido durante digitação
apaga a entrada e libera o teclado antes de abrir o modal. Não há marcha estimada,
pressão numérica de óleo ou sinal real sem sensor. Leitura sob sol/vibração,
distância de condução e latência óptica continuam sem avaliação no veículo.

Alertas precisam de texto/símbolo, não só cor.
Valores desconhecidos ou antigos aparecem explicitamente indisponíveis;
demonstração deve continuar identificada como simulação.

Somente uma tarefa acessa LVGL. Aquisição e armazenamento passam snapshots ou
filas limitadas. Atualizar texto apenas quando mudar e limitar animações a
pequenas regiões. Animação de valor é apresentação; alarmes e dados registrados
usam o valor adquirido e sua validade, sem atraso introduzido pelo efeito visual.

## Movimento e orçamento de processamento — 0.4.2

`ui_motion.hpp` mantém duas curvas escalares de apresentação e um relógio
racional próximo de 60 Hz. RPM usa ease-out de 100 ms e velocidade, 120 ms;
novo alvo parte do valor interpolado atual, sem fila ou salto. Primeiro valor,
nova sessão, urgência e redução de movimento fazem snap; invalidez limpa a
curva. Não há suavização do protocolo, da validade ou do cálculo de alarmes.

O snapshot observa TTL em cada tick. Conversões do View são refeitas somente
quando mudam sequência, sessão, validade ou unidades; formatação e desenho
comuns seguem a cadência escolhida. Mudanças de falha/urgência passam antes do
limitador, incluindo superaquecimento quando outro alerta domina o rodapé.
Calibração/modal opaco suspendem o desenho oculto e retomam com o dado atual.

O menu anima somente seus quatro ícones por 4 px em 120 ms, mantendo os alvos
fixos. A barra RPM é uma única rampa curva de 304×56 px na 0.4.5: callback C++ visita
somente as colunas sujas e agrupa trechos iguais em retângulos sólidos. A cor
RGB565 é calculada localmente, sem canvas, imagem ou buffer de gradiente.
Escala/limiares/validade invalidam a rampa quando necessário; o avanço comum
invalida só a faixa entre a posição anterior e a nova. A borda usa cobertura
fracionária Q8, misturada em RGB565, para reduzir saltos visuais de um pixel.
A barra evolui com cadência alvo de 60 Hz no modo Suave; números, cerca de
30 Hz, e detalhes acompanham o produtor USB de 20 Hz. Isso reduz redesenho de
glifos sem atrasar falhas/validade. Os números principais
usam dois buffers fixos de 16 bytes com `lv_label_set_text_static`, evitando
realocações de texto por quadro. Não há framebuffer completo extra.
Modos 33/50 ms e Reduzir movimento continuam disponíveis. Quadros atrasados
são descartados; não há recuperação acumulada que bloqueie o trabalho atual.
O modo Suave foi ativado e salvo após a bancada identificar modo Econômico
persistido. `ui-smooth` solicita essa mudança pelo serviço normal de ajustes,
preservando brilho, unidades e referências RPM; `info` informa os modos ativos.
O refresher LVGL em Suave acorda em 1 ms quando há região inválida, enquanto o
FrameClock limita a evolução dos instrumentos a 60 Hz; isso não gera 1.000
quadros/s. O LVGL pausa seu timer de refresh quando não há novo desenho.

O SPI de 40 MHz exige teoricamente 30,72 ms para os 153.600 bytes de uma tela
inteira RGB565, antes dos demais custos. A meta próxima de 60 Hz depende de
regiões parciais pequenas; não significa 60 telas completas/s. Métricas da
bancada e distinção entre apresentação, tempo decorrido da tarefa e FPS óptico
constam em [validação](validacao.md).

## Base técnica de referência, sujeita a validação no novo firmware

C++17, Arduino-ESP32 2.0.17, LVGL 8.4.0 e LovyanGFX 1.2.7 aparecem nos arquivos
locais. O perfil usa 320 × 240 em paisagem, RGB565, LCD compatível com ST7789,
SPI de 40 MHz, dois buffers DMA de 320×16 pixels (20 KiB no total), LVGL de
32 KiB e transições locais de 160 ms com desaceleração. Esses valores não são
resultados medidos pelo projeto NGK ECU e não fixam a revisão da placa atual.

Manter a aquisição independente da GUI/SD. O responsável solicitou explicitamente
Wi-Fi e BLE nesta revisão; a implementação própria começa com ambos desligados.
O firmware de manutenção do SD deve ser separado do firmware de operação.

## Métricas: referência versus validação própria

O relato e a documentação local registram aproximadamente 59 apresentações
**parciais**/s, p95 de 12,5 ms numa animação curta, e 30,8 ms para transportar
tela inteira. Não são 59 telas completas/s, medição óptica ou garantia de
desempenho no veículo. As medições próprias da bancada e seus limites estão em
[validacao.md](validacao.md); não confundir esses resultados com a referência.

SHA-256 dos arquivos locais inspecionados, para distinguir de um commit que
não inclui esses arquivos ainda não rastreados na origem:

| Arquivo | SHA-256 |
| --- | --- |
| `src/main.cpp` | `94d6ba1a2767ff85f7223a3f31f8ac51352eb7c36feae7c58370cf907424637c` |
| `include/ui_layout.hpp` | `d589107e6ba7fe21b11361d6d2d5b27ddb08efc108aff8d3fe26616418150bfc` |
| `platformio.ini` | `daa71a182ba410e46045ef66009600fcb190a2adbb34aba3f39be979a508c70d` |
| `artifacts/connectivity/ui/overview.png` | `cb23160e0f3ec4085f9550398244c61fb801e059d262c1edc663f934662427df` |
