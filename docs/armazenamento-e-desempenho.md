# SD, memória e desempenho

## O que existe nesta placa

A bancada confirmou flash de 4 MiB e nenhuma PSRAM detectada. Os 4 MiB são
armazenamento de programa/dados; não representam RAM disponível para desenhar.
A tabela de partições preservada reserva dois slots de aplicação de 1.966.080
bytes para atualizações. Não houve reparticionamento ou overclock nesta etapa.

O cartão preparado reporta 30.255 MiB e FAT32. Ele não foi reformatado para a
revisão da interface.

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

Para uma imagem RGB565 de 320×240 são 153.600 bytes. Só transportar esses
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

- Instrumentos e barra persistem na página; textos e segmentos mudam somente
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
a cadência do produtor. Nos modos normais, produtor 17/33/50ms e polling LVGL
5/11/16ms; o timer pausa sem regiões sujas. O teste termina e restaura o menu. Não escreve estatística por
quadro nem registra amostras de sensores.

Conta uma apresentação somente quando termina o último DMA daquela atualização,
não cada faixa do buffer. p95 parcial usa os últimos 256 tempos; máximo e
contagem abrangem toda a fase parcial. O maior intervalo do touch inclui ambas
as fases. Heap mínimo corresponde ao boot inteiro. Nenhuma dessas medidas é
latência óptica ou ensaio do motor/veículo.

Resultados próprios e limites de validação ficam em [validacao.md](validacao.md).
