# SD, memória e desempenho

## O que existe nesta placa

A bancada confirmou flash de 4 MiB e nenhuma PSRAM detectada. Os 4 MiB são
armazenamento de programa/dados; não representam RAM disponível para desenhar.
A tabela de partições preservada reserva dois slots de aplicação de 1.966.080
bytes para atualizações. Não houve reparticionamento ou overclock nesta etapa.

O cartão preparado reporta 30.255 MiB e FAT32. Ele não foi reformatado para a
revisão da interface. Na 0.4.2, a montagem assíncrona voltou a confirmar
30.255 MiB; exportação e importação do perfil Suave concluíram pela placa.
A consulta inicial antes da carga dos serviços pode indicar ausência; aguardar
o estado final antes de diagnosticar falha ou solicitar uma ação.

## Usos do cartão

| Uso | Estado / efeito |
| --- | --- |
| Perfil visual exportável | Implementado em `/ngk-ecu/ui-profile.cfg`; backup `.bak` e temporário `.new` |
| Logs e diagnóstico | Evolução prevista; nenhum registro de sensores implementado nesta etapa |
| Imagens, manuais, temas e fontes opcionais | Podem retirar recursos não essenciais da flash em evolução futura, com cache e leitura fora do desenho |
| Pacotes de atualização | Possível evolução; requer verificação de imagem e fluxo próprio, ainda não implementado |
| Mapas de combustível | Etapa futura de injeção monoponto; não há execução de mapas ou atuação nesta versão |

O perfil atual contém somente brilho, cadência visual, unidades, escala e
faixas visuais de RPM e preferência de movimento. Não contém senha, rede,
bonds Bluetooth ou calibração do touch. Exportar grava o último ajuste aplicado,
não um rascunho ainda não aplicado. Importar exige confirmação e validação do
formato, campos, limites e checksum; falha preserva os ajustes atuais.

Gravação usa arquivo temporário, leitura de conferência e backup do perfil
anterior. Isso melhora recuperação; não constitui transação garantida contra
qualquer perda de energia em FAT32. Erro de leitura do perfil existente aborta
a exportação sem removê-lo. Importação tenta o backup quando o principal não
é válido. `.new` nunca é importado automaticamente.

## Por que SD não aumenta a RAM

O SD é acessado como dispositivo de arquivos. Não substitui RAM interna,
PSRAM ou buffers DMA. A memória externa integrada ao endereço do ESP32 é
[PSRAM com suporte específico](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-guides/external-ram.html),
ausente na unidade identificada. Firmware continua executando da flash.

Para uma imagem RGB565 de 320 × 240 são 153.600 bytes. Só transportar esses
bytes pelo SPI do SD a 8 MHz exige teoricamente 153,6 ms, antes de protocolo,
decodificação e envio ao LCD. Ler a imagem a cada quadro prejudicaria a fluidez.
A 1 MHz, o limite teórico de leitura passa a 1,2288 s. Esses são cálculos de
banda, não ensaios de velocidade do cartão.

No LCD a 40 MHz, o transporte bruto dessa tela exige ao menos 30,72 ms.
Redesenhar só regiões alteradas permite mais apresentações por segundo que
redesenhar sempre a tela inteira. O
[driver LVGL 8.4](https://lvgl.io/docs/open/8.4/porting/display) suporta buffers
parciais e dois buffers para sobrepor desenho e transferência DMA.

## Otimizações desta revisão

- Instrumentos e barra persistem na página; textos e preenchimento mudam somente
  quando o conteúdo muda. Rótulos dos números têm largura do conteúdo e
  permanecem centralizados ao mudar o número de dígitos. Não reconstruir a
  página a cada atualização.
- Dois buffers RGB565 de 320×16 pixels, DMA e confirmação somente após terminar
  a transferência. Um único dono do LVGL.
- Fonte grande reduzida para `-.0123456789`, preservando pixels e métricas:
  96.713 para 4.629 bytes constantes no objeto, economia de **92.084 bytes**.
- SD e rádios em workers; pedidos e snapshots limitados. Cartão saudável não
  é desmontado/remontado periodicamente no firmware normal.
- Wi-Fi/BLE desligados ao iniciar, ativados por pedido explícito; recusam início
  quando os limites mínimos de memória não estão disponíveis. A bancada
  recusou ativar ambos: usar um rádio por vez nesta unidade.

Reduzir a fonte libera flash; isso não prova, por si só, aumento de FPS.
Os dígitos principais continuam na flash e não dependem do cartão para desenhar.
O pool LVGL continua 32 KiB; a fonte inteira de 48 px possuía bitmap de 91.912
bytes, portanto carregar essa fonte inteira no pool não seria uma solução.

## Método de medição

O comando local `bench` executa uma cena **DEMO TESTE**, sem entradas do motor:
20 redesenhos completos, depois 10 segundos de alterações parciais solicitadas
a cada 17 ms. A base usava timer LVGL de 16ms; a revisão usa 5ms, mantendo
a cadência do produtor. Na 0.4.2 normal, FrameClock limita a barra a 60 Hz racionais/33/50 ms;
os números em Suave têm cadência de cerca de 30 Hz. O refresher LVGL usa
1/5/10 ms e pausa sem regiões sujas. O benchmark mantém seu timer de 5 ms e
produtor de 17 ms para preservar a comparação. O teste termina e restaura o menu. Não escreve estatística por
quadro nem registra amostras de sensores.

Conta uma apresentação somente quando termina o último DMA daquela atualização,
não cada faixa do buffer. p95 parcial usa os últimos 256 tempos; máximo e
contagem abrangem toda a fase parcial. O maior intervalo do touch inclui ambas
as fases. Heap mínimo corresponde ao boot inteiro. Nenhuma dessas medidas é
latência óptica ou ensaio do motor/veículo.

Resultados próprios e limites de validação ficam em [validacao.md](validacao.md).

## Fontes do cockpit

A composição introduzida na 0.3.0 usa NGK Cockpit64/72 derivadas de Barlow Condensed Bold,
com 12 glifos por fonte e alturas efetivas 48/53 px. Os dois objetos somam14.045 B
constantes, mais 8 B de cache por fonte. São fontes maiores que a antiga
NGK Digits48; o objetivo é a legibilidade mantendo desenho parcial eficiente.
A imagem normal 0.3.0 tinha 1.404.240 B. A 0.4.2 adiciona NGK Gear48 com
somente `-NR01234`, sem habilitar Montserrat48 completa. Tamanho e hash atuais
estão em [validação](validacao.md); o slot continua 1.966.080 B.
A fonte anterior e seu gerador continuam no histórico/repositório, mas não
são usados no desenho do cockpit atual. Licença/geração estão documentadas.

## Movimento na 0.4.2

Barra única com desenho somente nas colunas sujas; sem canvas ou framebuffer
completo extra. Dois buffers fixos de texto evitam realocação dos números
principais. Cache de View evita conversões repetidas quando a fonte não muda;
TTL e alarmes continuam observados em cada tick. Não há leitura SD por quadro.
`motion-bench` mede separadamente o caminho de interpolação com fonte USB de
20 Hz. Tempo decorrido de `ui::tick` inclui espera/preempção e não permite
declarar percentual de CPU livre ou orçamento garantido para aquisição real.

## Medição de I/O contínuo executada

O diagnóstico separado `ecu-sd-benchmark` compara escrita 512 B, escrita 4 KiB,
sincronização por bloco ou por lote de 64 KiB e leitura 4 KiB. A fase SD 15 s
envolve integralmente a janela UI 10 s, comprovada pelos relógios do dispositivo.
O arquivo artificial exclusivo é limitado a 1 MiB, verificado e removido pela
própria execução. Não há esse comando ou seus buffers no build normal.

Em 2026-11-11, bancada concluída em SPI de 8 MHz: escrita 4 KiB com sync por
64 KiB teve 424,19 KiB/s, contra 281,08 KiB/s com sync por bloco (+50,9%).
São taxas efetivas com pausas cooperativas, sem medir velocidade bruta ou CPU
livre. Painel manteve 59,7–60 apresentações parciais/s, com máximos da rampa
até 34,685 ms. As operações SD chegaram a 117,161 ms, reforçando o uso do
worker e de fila limitada. A sincronização por volume deve ter também um
prazo temporal conforme a perda aceitável de dados; 64 KiB não exige buffer
RAM desse tamanho. Todos os arquivos próprios foram verificados/removidos e
normal original restaurado. Método e limites em [ensaio-sd.md](ensaio-sd.md).
