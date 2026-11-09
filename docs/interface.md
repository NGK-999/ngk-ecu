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

## Cockpit atual — 0.3.0

O responsável considerou a composição 0.2.0 insuficiente e deu liberdade para
pesquisar outra solução. Confirmou que o touch estava funcionando. A revisão
adota uma composição contínua e assimétrica inspirada na hierarquia dos
instrumentos [FuelTech](https://www.fueltech.com.br/products/ft450-injecao-eletronica),
nos layouts [AiM MXm/MXq](https://aim-sportline.com/download/doc/eng/mxm-mxq/MXm-MXq_user_guide_eng.pdf)
e no formato320 × 240 do [MoTeC D153](https://www.motec.com.au/products/D153).
As medidas e decisões visuais são próprias; nenhum ativo dessas marcas foi copiado.

| Região | Tratamento |
| --- | --- |
| RPM superior | Número condensado, barra crescente e cinco marcas de escala |
| Velocidade inferior direita | Número maior, unidade separada e alinhamento estável |
| Bloco e óleo à esquerda | Temperatura legível e estado binário, sem pressão inventada |
| Menu | Painel em destaque; Sensores, Ajustes e Sistema como ações secundárias |
| Ajustes | Seis categorias diretas; abas Unidades/RPM dentro de Instrumentos |

Fundo `#06090D`, superfícies `#101820`, botões `#131E28`, texto branco,
secundário `#8F9DAA`, ciano `#35D9D0`, âmbar `#FFC857`, vermelho `#FF4D5A`
e divisórias `#283642`. Sem imagem de fundo, transparência animada ou sombra.
Cores de faixaRPM são referências visuais, não corte/proteção do motor.

Barlow Condensed Bold foi convertida em NGK Cockpit64/72, com apenas 12 glifos:
`-.0123456789`. As alturas de linha efetivas são 48/53 px; em 72, três dígitos
ocupam até 105 px; em 64, cinco dígitos ocupam até 155 px. Fontes residentes na flash,
4bpp sem compressão, sem carregamento do SD durante desenho. Licença e reprodução
em [third_party/licenses](../third_party/licenses/README.md).
Legendas continuam Montserrat12/14/16/28. Áreas numéricas usam largura do
conteúdo, preservando alinhamento e invalidando regiões antigas/novas.

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
