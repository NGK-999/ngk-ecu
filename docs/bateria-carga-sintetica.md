# Bateria de carga sintética

Bateria concluída em 2026-11-12 sobre a base 0.4.11: **nove casos completos e
duas falhas de controle USB a 100 Hz**, preservadas como resultado. Todas as
tentativas confirmaram encerramento da carga/SIM e restauração dos rádios.
O firmware normal original foi restaurado e verificado no final.

[Dados agregados dos onze casos](measurements/synthetic-load-2026-11-12.json).
Nenhuma medição física de sensor ou atuação foi habilitada.

## O que a carga representa

Um build de diagnóstico separado, `ecu-load-benchmark`, acrescenta trabalho
determinístico no core 1, com prioridade 1, onde também executa o loop da UI.
São três modos no mesmo binário:

| Modo | Canais virtuais por lote | Alvo de lotes/s | Iterações inteiras por canal |
| --- | ---: | ---: | ---: |
| Controle | 0 | 0 | 0 |
| Nominal | 16 | 100 | 32 |
| Pico | 32 | 1,000 | 128 |

O kernel calcula um checksum que chega aos agregados observados. Não lê ADC,
ignição, sensores, barramentos ou GPIOs. O número de canais virtuais não
certifica quantos sensores físicos a placa suportará. Drivers, interrupções,
condicionamento, conversões e calibração têm custos próprios ainda não medidos.
O painel recebe movimento sintético por USB em paralelo; esse movimento não
vem do mailbox de carga.

## Contagem e tempos

A troca entre produtor e consumidor mantém somente o trabalho mais recente.
A contabilidade deve respeitar:

`produzidos = consumidos + substituídos + pendentes`

Há zero ou um item pendente. Substituição é descarte intencional de uma versão
antiga desse mailbox; não é perda de um pulso físico ou erro do protocolo USB.
O consumidor continua drenando após a parada para fechar os contadores.

O scheduler usa a janela `[início, fim)` e nunca recupera períodos atrasados
em rajada. Períodos vencidos são contados separadamente dos trabalhos executados.
O maior atraso de despacho é relativo ao prazo nominal anterior ao descarte
de períodos vencidos. A idade de consumo começa no fim do kernel e cobre
somente itens que foram consumidos, excluindo os substituídos.

Tempos do kernel e da UI são **tempo de parede**, incluindo preempção/esperas;
não são percentual de CPU. O SDK Arduino-ESP32 2.0.17 deste projeto não habilita
as estatísticas de runtime/trace do FreeRTOS. Ele foi mantido sem reconstrução.
A [documentação da Espressif](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32/api-guides/performance/speed.html)
distingue temporização de funções e estatísticas de execução por tarefa.

`sim-status.seq` informa a maior sequência aceita; gaps são permitidos pelo
protocolo. Seu avanço não é contador de frames aceitos nem mede perdas exatas.
O cliente guarda apenas o delta desse watermark, sem sequências ou amostras brutas.

## Memória e apresentação

Cada caso começa após reset e inicialização do mesmo binário de diagnóstico.
O mínimo de heap inclui a inicialização e o caso desde aquele boot. A menor
folga da pilha inclui toda a vida da tarefa naquele boot. Não são mínimos
isolados da janela de dez segundos da UI. O diagnóstico também ocupa memória;
esses valores não devem ser tratados como consumo do aplicativo normal.
As unidades de `uxTaskGetStackHighWaterMark` neste ESP-IDF são bytes.
Referência: [medição e redução de RAM](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32/api-guides/performance/ram-usage.html).

A janela de apresentação dura dez segundos. Apresentações são transferências
parciais concluídas, não quadros ópticos completos. O p95 entre apresentações
usa os últimos 256 intervalos; latência amostra→primeira mudança usa os últimos
128 casos. Extremos/contadores abrangem a janela; p95 da rampa é histograma
com bins de 1 ms. Captura de imagem congelada não mede fluidez.

## REA e reprodução

REA 6.8.0 é executado localmente no ELF próprio com `section_headers`, verificando
hash, conclusão e ausência de alteração do artefato. Esta versão não descompila
Xtensa. O toolchain Xtensa local complementa a inspeção do kernel e da integração;
REA não mede jitter, precisão de sensores ou CPU da placa.

O condutor `tools/benchmark_load.py` exige `--execute-physical-bench` e a identidade
exata do diagnóstico, sem conexão no import ou `--help`. Testes do cliente usam
transporte em memória. A bancada USB é uma ação explícita separada. O runner
recusa SIM preexistente ou conexões externas e encerra carga, SIM, busca/anúncio
antes de restaurar os estados de rádio. Não associa Wi-Fi nem pareia BLE.

Somente metadados permitidos e agregados são persistidos. Firmware extraído,
saída completa de ferramentas, credenciais e identificadores ficam fora dos
artefatos públicos; amostras e conteúdo serial bruto não são registrados.
O aplicativo normal original é restaurado ao final, somente no slot de app,
sem formato SD nem mudança de NVS, calibração ou partições.

## Resultados na placa

| Caso | Lotes produzidos / previstos | Substituídos no mailbox | UI parcial / 10 s | p95 UI (ms) | Heap mínimo (B) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Controle 1 | 0 / 0 | 0 | 600 | 24,940 | 102356 |
| Nominal | 1500 / 1500 | 88 | 600 | 25,077 | 102356 |
| Pico | 9856 / 15000 | 4298 | 600 | 25,746 | 102356 |
| Nominal + refresh SD | 1500 / 1500 | 36 | 599 | 25,155 | 102356 |
| Nominal + busca Wi-Fi | 1500 / 1500 | 34 | 600 | 25,123 | 52268 |
| Nominal + anúncio BLE | 1500 / 1500 | 62 | 601 | 24,077 | 85204 |
| Nominal + USB100 | timeout de controle | — | sem janela completa | — | — |
| Controle + USB100 | timeout de controle | — | sem janela completa | — | — |
| Nominal + USB50 | 1500 / 1500 | 59 | 595 | 24,916 | 102356 |
| Controle 2 | 0 / 0 | 0 | 598 | 25,014 | 102356 |
| Nominal 120 s | 12000 / 12000 | 289 | 600 | 24,575 | 102356 |

No pico, 5144 dos 15.000 períodos não foram executados
(34,29%). Foram cerca de 657,1 lotes/s
neste scheduler e nesta carga; **não é o limite máximo do ESP32**. O tick RTOS
de 1 ms, a espera arredondada para ticks e a concorrência com a UI fazem parte
do desenho ensaiado. Manter a UI atualizada não prova aquisição a 1.000 Hz.

O caso de 120 s produziu 12000 lotes, com 0 períodos
perdidos. Heap livre observado por polling: início 103868 B,
fim 103868 B, menor 103868 B.
Foram 121 consultas de status. A janela UI continua sendo apenas
dez segundos no início dessa execução; não há medição de fluidez durante todos
os 120 s. Estes dados não demonstram ausência universal de vazamento ou estabilidade veicular.

### Limite do USB de diagnóstico

As duas tentativas a 100 Hz falharam com `control_timeout`, inclusive o controle
sem kernel. Ambas encerraram SIM/carga, restauraram rádios e fecharam a porta.
O ensaio nominal a 50 Hz concluiu. O estágio exato dos dois timeouts não foi
identificado nesta bateria.

No SDK local, o ring RX padrão tem 256 B. `serialCommands()` deixa de consumir
RX enquanto o outbox TX está pendente, e respostas de diagnóstico são longas.
Isso é compatível com pressão/backlog de recepção, mas **overflow não foi
confirmado** por contador. Não atribuir o timeout à falta de CPU, nem converter
o watermark SIM em número de frames perdidos. A taxa de 100 Hz não foi validada
por esta bateria; 20/50 Hz são resultados deste caminho e destas condições.

SD refresh e busca Wi-Fi são estímulos transitórios observados durante a janela;
o anúncio BLE foi observado ativo no início e encerrado no cleanup. Não foi ensaiada gravação contínua
no SD, associação Wi-Fi ou pareamento/conexão BLE.

### Folga das pilhas observada

| Tarefa | Menor folga entre os casos completos (B) |
| --- | ---: |
| Main/UI | 4708 |
| SD | 5872 |
| Rádio | 4088 |
| Carga | 3480 |

São mínimos dos caminhos exercitados, não autorização para reduzir stacks:
conexão/pareamento, teclado e todos os menus não foram estressados nesta bateria.
Buffers DMA de 20 KiB e pool LVGL de 32 KiB permaneceram iguais.

## Software, isolamento e restauração

Os testes Qt/cliente rodaram headless, com transporte em memória. A bancada
USB mediu a UI da placa por contadores; não houve nova inspeção visual de
fluidez, medição óptica do LCD nem ensaio no veículo.

Passaram 21 executáveis nativos ASan/UBSan, 102 testes do simulador/cliente
(15 novos), cinco builds e auditoria de 14 sinais: zero erros e três avisos
conhecidos. Foram exercitados parser/busy/limites, cancelamento queued, scheduler
sem catch-up, contabilidade, final pendente, mensagens malformadas, privacidade,
overlap dos relógios e cleanup assíncrono/ACK perdido, com regressões RED→GREEN.

O build normal não contém símbolos ou comandos de carga. Código e dados
executáveis correspondem à base; somente 65 bytes de metadados/checksums do novo
binário normal mudaram por causa do ELF reconstruído. Foi restaurado o original,
não esse rebuild. O diagnóstico acrescenta 360 B de RAM estática e 4.480 B de
flash, além da stack de 4.096 B/TCB alocada em runtime e instrumentação. Esse custo
está presente tanto no controle quanto nas cargas.

REA inspecionou o ELF normal original (SHA-256
`6626fa61c947bed972beeaf548651912c03ad0b2c8093dd37abc681b94046838`)
e o diagnóstico (`5f205d3b5acc76424b41e8a5e4a74ae8d7018a04220c6e2e3048f1c83401f98a`):
exit 0, complete, artifact_changed=false. O disassembler Xtensa confirmou os
loops XOR/rotação/adição materializados e o uso de seu resultado. Não houve
descompilação Xtensa pelo REA nem medição de CPU por ele.

Normal final: `0.4.11-resource`, 1.424.416 B, SHA-256
`2bea5acf265b5845581fcf9d6da965fc7da6cdd62fcd9b4fcdbaa00fe49f87f2`.
A escrita do backup no slot de app teve hash verificado. Consulta posterior
confirmou SD montado, perfil persistido, SIM/rádios desligados e flags físicas
false. NVS, calibração, GPIOs, partições e formato do cartão foram preservados.

## Executar outro caso deliberadamente

Compilar o diagnóstico localmente:

```sh
PLATFORMIO_CORE_DIR="$PWD/.tools/platformio" .venv/bin/pio run -d firmware/ecu -e ecu-load-benchmark
```

Após identificar a placa, preservar o aplicativo atual e instalar somente a
variante diagnóstica, executar um caso explícito com caminho de resultado novo:

```sh
.venv/bin/python tools/benchmark_load.py --port /dev/ttyUSB0 --mode 1 --load off --rate 20 --duration 15000 --output local/load-case.json --execute-physical-bench
```

O cliente não grava/restaura firmware nem reinicia a placa. Nesta bateria, essas
ações foram feitas separadamente pelo operador do ensaio, incluindo reset entre
casos e restauração do original. Encerrar GUI/serial concorrente antes da bancada.
O encerramento trata exceções, KeyboardInterrupt e SIGTERM; SIGKILL/desconexão
abrupta impede garantir limpeza dos rádios, embora a carga tenha duração limitada
e o SIM perca validade pelo TTL. Não interpretar timeout como resultado aprovado.
