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

## Tema proposto para a ECU

| Elemento | Cor |
| --- | --- |
| Fundo | `#0B111C` |
| Cartões | `#20242D` |
| Botões | `#282D37` |
| Texto principal | `#F5F5F7` |
| Texto secundário | `#B4BAC6` |
| Destaque | `#70ADFF` |
| Bordas | `#424752` |

Centralizar o tema num único módulo. Cartões opacos, bordas de 1 pixel, raios
de referência de 14 nos botões, 16 nos cartões e 22 no dock. Fontes de referência:
12 px para legendas, 14–16 px para textos e 20–28 px para valores, ajustando o
instrumento principal à legibilidade real. Validar leitura sob sol, vibração
e distância de condução; a captura de software não faz essa validação.

Interação por clique, alvos de pelo menos 44 pixels como ponto de partida,
paginação e teclados por páginas; sem depender de arraste, slider ou swipe.
Esse número é um requisito inicial desta referência, não equivalência física
com pontos de interfaces Apple. Calibração de cinco pontos, filtro de ruído e
estados de pressionar/soltar devem ser conferidos na unidade real.

## Organização proposta

Tela inicial com RPM em destaque, velocidade e temperatura legíveis e faixa
persistente de estados/alertas. Páginas de sensores, configuração e sistema
sem sobrecarregar a tela principal. Alertas precisam de texto/símbolo, não só cor.
Valores desconhecidos ou antigos aparecem explicitamente indisponíveis;
demonstração deve continuar identificada como simulação.

Somente uma tarefa acessa LVGL. Aquisição e armazenamento passam snapshots ou
filas limitadas. Atualizar texto apenas quando mudar e limitar animações a
pequenas regiões. Animação de valor é apresentação; alarmes e dados registrados
usam o valor adquirido e sua validade, sem atraso introduzido pelo efeito visual.

## Base técnica de referência, sujeita a validação no novo firmware

C++17, Arduino-ESP32 2.0.17, LVGL 8.4.0 e LovyanGFX 1.2.7 aparecem nos arquivos
locais. O perfil usa 320×240 em paisagem, RGB565, LCD compatível com ST7789,
SPI de 40 MHz, dois buffers DMA de 320×16 pixels (20 KiB no total), LVGL de
32 KiB e transições locais de 160 ms com desaceleração. Esses valores não são
resultados medidos pelo projeto NGK ECU e não fixam a revisão da placa atual.

Manter a aquisição independente da GUI/SD. Wi-Fi e BLE não foram solicitados
para esta V1; não importar automaticamente os serviços de rede da referência.
O firmware de manutenção do SD deve ser separado do firmware de operação.

## Métricas: referência versus validação própria

O relato e a documentação local registram aproximadamente 59 apresentações
**parciais**/s, p95 de 12,5 ms numa animação curta, e 30,8 ms para transportar
tela inteira. Não são 59 telas completas/s, medição óptica ou garantia de
desempenho no veículo. Repetir as medições no novo firmware, com sensores,
touch e SD em uso, antes de publicar números de desempenho próprios.

SHA-256 dos arquivos locais inspecionados, para distinguir de um commit que
não inclui esses arquivos ainda não rastreados na origem:

| Arquivo | SHA-256 |
| --- | --- |
| `src/main.cpp` | `94d6ba1a2767ff85f7223a3f31f8ac51352eb7c36feae7c58370cf907424637c` |
| `include/ui_layout.hpp` | `d589107e6ba7fe21b11361d6d2d5b27ddb08efc108aff8d3fe26616418150bfc` |
| `platformio.ini` | `daa71a182ba410e46045ef66009600fcb190a2adbb34aba3f39be979a508c70d` |
| `artifacts/connectivity/ui/overview.png` | `cb23160e0f3ec4085f9550398244c61fb801e059d262c1edc663f934662427df` |
