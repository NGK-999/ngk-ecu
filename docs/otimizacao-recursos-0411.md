# Menos trabalho repetido — 0.4.11

A versão normal 0.4.11-resource foi instalada e medida na ESP32 em 2026-11-11.
O objetivo é reduzir contas, cópias e consultas repetidas antes de ampliar a
aquisição física. A alternância visual da 0.4.10 foi preservada.

## Mudanças

| Caminho | Alteração |
| --- | --- |
| Relógio de quadros | Comparação/subtração no caso comum; módulo de 64 bits fica para saltos grandes. |
| Barra RPM | Fator da escala calculado somente quando a escala muda. |
| RPM animado | Uma amostra de apresentação por quadro, compartilhada entre barra e número. |
| Rádios desligados | Worker espera na própria fila, sem retornar periodicamente para publicar o mesmo estado. Com rádio ativo, a espera na fila continua limitada a 20 ms. |
| UART e SD | TX vazio retorna antes de consultar o driver UART. O mailbox SD só copia publicações novas; o último estado continua disponível. |

As alterações reduzem operações repetidas e mantêm responsabilidades explícitas.
O código matemático ficou explícito e testável. O TX continua limitado; a fila
SD de um elemento continua representando o estado mais recente, com consumidor único.
Nenhuma fila de sensores ou infraestrutura sem consumidor foi adicionada.

O getter BLE por referência foi investigado e descartado: `getAttVal()` é
`protected` na NimBLE 2.5.1 instalada. O RX continua usando `getValue()` por cópia;
**nenhuma alocação por fragmento BLE foi eliminada nesta entrega**. SDK intacto.

## Verificação matemática e de software

O oráculo conserva a fórmula original e compara 1.127.278 consultas do relógio:
tempos repetidos, passos de 1 ms, mudanças de modo, reset, rollover e deltas
até UINT32_MAX. O acumulador permanece em 64 bits; só é estreitado após provar
que cabe no ramo rápido. Não há recuperação de quadros em rajada.

O fator RPM foi comparado nas 23 escalas permitidas, em RPM inteiro, extremos
e vizinhanças dos limiares de arredondamento Q8. A ordem de divisão e
multiplicação foi mantida. A escala deve ser positiva e validada pelos ajustes
antes de `progress()`; a trajetória de apresentação não aceita NaN.
Urgência, validade e valor terminal também foram comparados ao cálculo repetido.
Duas mutações incorretas foram detectadas pelos testes.

Passaram **20 executáveis nativos com ASan/UBSan, 87 testes do simulador,
quatro builds e auditoria de 14 sinais sem erros**. Os três avisos conhecidos
de strapping permanecem. Revisão independente conferiu código e resultados.

## Bancada e limites

[Agregados dos nove casos](measurements/resource-budget-2026-11-11.json).
Fonte sintética USB de 20 Hz; cada janela da UI tem aproximadamente 10 s.
As duas versões são binários diferentes, e as tentativas antes de iniciar
mudaram a fase da onda: uma tentativa nas bases, cinco nos dois casos novos
sem carga. Essa comparação verifica continuidade; **não isola percentual de CPU**.

| Sem carga, dois casos por versão | 0.4.10 | 0.4.11 |
| --- | ---: | ---: |
| Apresentações parciais por caso | 600 | 600 |
| p95 entre apresentações | 24,22–24,36 ms | 24,49–24,60 ms |
| Máximo do tick da UI | 14,50 ms | 14,29–14,30 ms |
| Maior área por apresentação | 11.779 pixels | 11.779 pixels |

O p95 usa os últimos 256 intervalos; máximos abrangem a janela inteira.
O tempo acumulado da UI caiu cerca de 2,56%, acompanhado de 1,91% menos pixels
transferidos e fase diferente. Não atribuir essa diferença somente às otimizações,
nem convertê-la em CPU livre. A fluidez anterior foi mantida, sem um novo ganho
global de FPS demonstrado.

Com os rádios desligados, o contador permaneceu **2 → 2** durante os casos
sem carga, repouso e refresh SD. Os dois comandos anteriores já tinham sido
processados. Com BLE anunciando: **112 → 594**; com Wi-Fi buscando: **108 → 590**.
Os estados ativos foram observados, a busca Wi-Fi terminou e os rádios foram
desligados no encerramento. Não foram solicitados conexão, associação ou pareamento com terceiros.
O anúncio BLE foi cancelado; não se ensaiou sua expiração natural de 120 s.

`worker_wakeups` conta retornos de `xQueueReceive`, incluindo comandos e timeouts;
não conta todas as trocas de contexto. As consultas do contador abrangem a janela
da UI e o tempo de transmitir o relatório. Portanto, 482 retornos não são uma
taxa exata de CPU ou um intervalo precisamente igual a 10 s. O zero com rádios
desligados demonstra ausência desse trabalho periódico no intervalo observado.

Todos os cinco casos candidatos em movimento tiveram 600 apresentações.
Refresh SD confirmou revisão 1 → 2/status 12/montado. Repouso teve 9800 retornos
antecipados, zero passagens de instrumentos/números e zero pixels.
Os gaps de 34 ms da barra coincidiram com passes sem invalidação, conforme
o método da [0.4.10](fluidez-0410.md); não foram pausas de 34 ms no despacho.
Não houve ensaio de escrita contínua, aquisição real, atuação ou operação veicular.

## Memória, imagem e preservação

| Linker normal | 0.4.10 | 0.4.11 | Diferença |
| --- | ---: | ---: | ---: |
| RAM estática | 96.832 B | 96.848 B | +16 B |
| Flash | 1.417.781 B | 1.417.833 B | +52 B |

O pequeno cache matemático e o contador usam memória fixa. DMA de 20 KiB e pool
LVGL de 32 KiB permanecem iguais; sem novo framebuffer ou cache de bitmap.
Menos linhas não seria uma medida de economia de processamento ou de RAM.

As capturas normal e com alertas receberam 153.600 bytes cada, com CRC
`d508fbca` e `7fb2f473`. Os PNGs são byte a byte iguais aos anteriores:
layout, fontes, cores, instrumentos e alertas foram preservados nesses estados.
As imagens já publicadas foram reutilizadas; captura congelada não mede fluidez.

O aplicativo 0.4.10 foi lido e preservado em backup privado antes da gravação.
A escrita atingiu somente o app em 0x10000 e teve hash verificado. Binário final:
1.424.416 B, SHA-256 `2bea5acf265b5845581fcf9d6da965fc7da6cdd62fcd9b4fcdbaa00fe49f87f2`.
Perfil e calibração foram preservados; nenhum GPIO, partição ou formato SD mudou.
Todos os casos encerraram SIM e restauraram rádios. Sensores e atuadores físicos
continuam desativados. A próxima etapa de sensores ainda exige drivers, pinos,
condicionamento e aquisição independente da apresentação.
