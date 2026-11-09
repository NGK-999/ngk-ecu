# Inventário inicial de hardware

Data: 2026-11-09. Evidência: inspeção de foto fornecida pelo responsável e
enumeração USB/discos do computador. Não houve comunicação com o firmware.
A foto original não é publicada por conter elementos externos ao projeto.

## Observado

| Item | Evidência | Limite |
| --- | --- | --- |
| Placa | Serigrafia ESP32-2432S028 na foto | Sufixo/revisão não confirmados |
| Módulo | Blindagem marcada ESP-32 | Variante e revisão do chip não consultadas |
| USB | Micro-USB e USB-C visíveis | Função elétrica de cada conector não confirmada |
| Ponte USB–serial | CH340, VID:PID `1a86:7523` | Identifica a ponte, não a pinagem da placa |
| Porta no diagnóstico | `/dev/ttyUSB0` | Nome pode mudar ao reconectar |
| Acesso serial | Processo sem leitura/escrita na porta | Nenhuma abertura/reset/leitura de flash |
| Cartão | Inserido no slot TF na foto | 32 GB informados; capacidade não verificada |
| Disco removível | Nenhum cartão acessível no inventário do host | Não prova falha ou ausência do cartão |

## Pinagem

Nenhum GPIO atribuído a sensores. A pinagem deve partir do esquema da revisão
correta e reservar os pinos usados por tela, touch, SD, flash, boot e demais
periféricos. Não copiar configuração de outra ESP32-2432S028 somente pela aparência.

Referência candidata do fabricante Guition, **ainda não validada nesta unidade**:

| Função | GPIOs da definição candidata |
| --- | --- |
| LCD SPI | MOSI 13, MISO 12, SCLK 14, CS 15, DC 2, backlight 21 |
| Touch resistivo XPT2046 | MOSI 32, MISO 39, SCLK 25, CS 33 |
| SD SPI | MOSI 23, MISO 19, SCLK 18, CS 5 |

Fonte fixada no commit `cc16562bc184676edaa2a61f5263dff854dec7a3`:
[periféricos](https://github.com/Guition-JC/Guition/blob/cc16562bc184676edaa2a61f5263dff854dec7a3/components/board/guition_boards/esp32_2432s028/board_peripherals.yaml),
[dispositivos](https://github.com/Guition-JC/Guition/blob/cc16562bc184676edaa2a61f5263dff854dec7a3/components/board/guition_boards/esp32_2432s028/board_devices.yaml).
O próprio [registro da placa](https://github.com/Guition-JC/Guition/blob/cc16562bc184676edaa2a61f5263dff854dec7a3/components/board/guition_boards/esp32_2432s028/board_info.yaml)
avisa `hardware unverified`. Embora os YAMLs repitam `spi_port: 2`, o pacote
completo usa [SPI em software para o touch](https://github.com/Guition-JC/Guition/blob/cc16562bc184676edaa2a61f5263dff854dec7a3/components/board/guition_boards/esp32_2432s028/guition_touch.c),
com [implementação GPIO própria](https://github.com/Guition-JC/Guition/blob/cc16562bc184676edaa2a61f5263dff854dec7a3/components/board/guition_boards/esp32_2432s028/guition_soft_spi.c).
Copiar somente os YAMLs omite essa adaptação. Tela, touch e SD simultâneos ainda
precisam ser validados na unidade real.

Há divergência entre fontes do fabricante: o
[manual ESP32-2432S028R](https://www.guition.com/icms/upload/fb081940d6fc11f09850077a33e1404f/FTPData/UEditor/file/2026121/1768961091633/ESP32-2432S028%20Specifications-EN.pdf)
descreve ILI9341, enquanto a definição acima usa ST7789. Nenhum dos dois
controladores está confirmado na placa fotografada. Não inferir a revisão
"Rv3" apenas por haver dois conectores USB.

## Identificação e substituição autorizada do firmware

Com acesso à serial e placa isolada de qualquer atuador, usar esptool para
consultar o chip e a flash. A comunicação pode reiniciar o dispositivo. Não
gravar firmware para descobrir que placa é. Se for necessário analisar o
firmware existente, preservar uma cópia local autorizada antes de qualquer escrita.
Essa cópia pode conter credenciais; nunca publicá-la no repositório ou memória.
O responsável autorizou a substituição do firmware para o novo projeto e para
manutenção do SD. Preservar o firmware atual não é requisito para manter a placa
intocada; o backup local permite recuperação quando a leitura for possível.

Consultas ao chip não identificam necessariamente o controlador do LCD/touch ou
o roteamento dos GPIOs. Usar esquema e drivers da revisão, com verificação física.

## Cartão microSD

A ponte CH340 não expõe o slot como armazenamento USB. Opções de acesso:

1. Leitor de cartões conectado ao computador, identificando o dispositivo
   por reconexão, modelo, capacidade e dependências/montagens antes de formatar.
2. Firmware conhecido na própria placa, após confirmar sua pinagem e capacidade
   de identificar explicitamente o cartão e limitar a formatação ao SD.

**Fluxo escolhido pelo responsável:** manutenção pela própria placa, pois não
pode usar leitor externo. A referência local karl-panel possui implementação
dedicada de FAT32 por comando explícito. Conferir o perfil da placa e o código
antes de reutilizar; o histórico de sucesso em outra tarefa não identifica
automaticamente o cartão conectado agora.
FAT32 é candidato para 32 GB, condicionado à confirmação do cartão e do driver.
Não ativar formatação automática quando a montagem falhar. Ausência, corrupção
ou mau contato devem produzir diagnóstico, não apagamento.

## Integração com o veículo

Definir condicionamento conforme os sensores reais antes de escolher GPIOs.
Sinal de ignição e alimentação automotiva exigem proteção própria; um painel
alimentado por USB não foi validado contra ruído, transientes ou inversão.

Fontes primárias:

- [WCH: CH340](https://www.wch.cn/products/CH340.html)
- [Espressif: consulta da flash](https://docs.espressif.com/projects/esptool/en/latest/esp32/esptool/basic-commands.html#read-spi-flash-id-flash-id)
- [Espressif: especificação elétrica do ESP32](https://documentation.espressif.com/esp32_datasheet_en.html)
- [Espressif: USB MSC](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_device.html#usb-mass-storage-device-msc)
  (exemplo de recurso que exige chip e firmware compatíveis, não confirmação nesta placa)
- [Texas Instruments: referência de proteção automotiva](https://www.ti.com/tool/TIDA-01167)
