# Validação e evolução sem sensores

Situação em 2026-11-11: o projeto pode evoluir com dados sintéticos. A ausência
dos sensores não bloqueia interface, simulação, validade dos dados ou testes
do protocolo. Alimentação, condicionamento, ruído, precisão e calibração reais
continuam dependendo da bancada física e dos sensores escolhidos.

## Correção da origem do simulador

A janela Qt produz os snapshots; o worker USB envia o mais recente. Antes, se
a janela parasse de produzir, o worker continuava transmitindo o último valor
com sequências novas. Reprodução em transporte em memória: uma produção única
resultou em 2 frames iniciais e 18 frames após outros 800 ms sem produção. Isso
podia renovar o TTL na ESP32 com um valor antigo. Foi um caso reproduzido
sinteticamente, não um incidente observado no veículo ou no USB físico.

Agora cada `set_sample` registra horário monotônico, inclusive quando os valores
são iguais. Sem produção inicial ou após 250 ms sem novo snapshot, o envio SIM1
fica suspenso e o app indica **ORIGEM SEM DADOS**. Consultas de estado continuam;
o operador pode desconectar. Produção nova retoma a mesma sessão com sequência
monotônica, usando o último snapshot, sem reproduzir uma fila antiga. A pausa
manual continua independente.

Essa proteção não altera o TTL de 750 ms do firmware. A expiração na placa é
contada desde o último frame recebido; portanto não se promete limpar o LCD
250 ms após o travamento do produtor. Não houve mudança de firmware ou porta
física nesta correção. Uma instância do app já aberta precisa carregar o novo
código na próxima abertura para usar a proteção.

## Evidência obtida sem hardware

- **60 testes passaram**, Qt offscreen e transporte em memória. Regressão
  RED→GREEN cobriu produção inexistente, vencimento, valores idênticos frescos,
  retomada, pausa, encerramento e estado dos botões. Revisão independente passou.
- Modelo e codec executaram **144000 passos de 50 ms**, totalizando **7200
  segundos virtuais**: 24 ciclos de 12 fases com partida, marchas, frenagem,
  falhas, ré, extremos manuais e desligamento. Valores permaneceram finitos e
  dentro do contrato; frames tiveram no máximo 38 bytes, abaixo do limite.

O segundo ensaio levou aproximadamente 1,4 s de execução. São duas horas
**virtuais do modelo**, sem avaliar duas horas reais de Qt, memória, USB, LCD,
CPU da ESP32 ou veículo. Apenas resumos agregados foram preservados; sem
amostras ou conteúdo serial bruto em arquivos.

## Ciclo automático

Implementado em 2026-11-11: dez fases em 60 segundos, com partida/lenta,
quatro marchas, frenagem, falhas simultâneas, recuperação e desligamento.
**Iniciar ciclo**, **Parar** e **Repetir** ficam em Cenários rápidos. A fase e
a volta aparecem na janela; os controles anteriores retornam ao parar,
finalizar uma volta única ou fechar. Cada volta reinicia o estado do modelo.
O ciclo não conecta USB e continua sendo uma dinâmica ilustrativa.

Validação atual: **74 testes passaram**, sem hardware. Janelas Qt offscreen
em 980×680 e 1120×760 inspecionadas, com opções de falha legíveis. Revisão
independente não encontrou defeito material.

## Ensaio prolongado

O script `scripts/soak-simulator.py` exercita a janela, o modelo, o codec e o
worker reais com Qt offscreen e transporte MEMORY limitado. A cada minuto,
provoca pausa USB simulada, interrupção do produtor e desconexão/reconexão.
Somente contadores e agregados de memória, threads e intervalos são guardados.
Não usa USB física, LCD ou sensores. Comando e limites em
[simulator/README.md](../simulator/README.md#ensaio-prolongado-em-memória).

**Ensaio concluído:** 1.800,006 segundos reais, 54.710 produções sintéticas e
33.782 frames aceitos, sem erros de protocolo, sequência ou valores inválidos.
As 30 pausas, 30 paradas do produtor e 30 desconexões tiveram recuperação
confirmada. Dez fases observadas; controles restaurados, 31 sessões encerradas
e worker finalizado. Threads Python: pico de duas, uma ao terminar.

RSS inicial 85.000.192 B, final 90.877.952 B e máximo 91.140.096 B. Houve
crescimento de 5,61 MiB desde o início, que inclui aquecimento do Qt. Nas
leituras de progresso após o primeiro minuto, a faixa foi
90.832.896–90.935.296 B: não apareceu crescimento contínuo nesse trecho.
Isso é uma observação de 30 minutos, sem comprovar ausência de vazamentos.

O observador de 50 ms teve intervalo máximo de 55,424 ms. A maior lacuna de
produção foi 1,713 s e inclui as interrupções intencionais do produtor.
TTL vencido foi observado nos dois tipos de pausa; o critério exige ao menos
uma observação por tipo durante o ensaio, não uma por interrupção.
Somente agregados foram preservados; esse resultado não valida hardware.

## Desempenho medido na ESP32

A prévia **0.4.9-layout-preview** foi instalada e medida em bancada em
2026-11-11, com dados sintéticos e aquisição/atuação físicas desabilitadas.
Foram **598–601 apresentações parciais em dez segundos** com movimento,
inclusive com SD e busca Wi-Fi ou anúncio BLE, separadamente. Em repouso,
não houve redesenho/formatação dos instrumentos após estabilizar.

A rampa teve p95 superior de 23 ms nos três casos com movimento. A maior pausa
de consulta do toque foi 30,055 ms. A latência amostra→primeira mudança chegou
a 32 ms; o p95 sem rádio foi 31 ms nesta execução, contra 22 ms no ensaio
histórico da 0.4.8. Não se conclui ganho global de desempenho. A média próxima
de 60 apresentações parciais/s não significa 60 telas completas/s, nem mede
CPU livre ou resposta óptica do LCD. Carga SD foi um remount pontual.

Métricas completas, metodologia e capturas da placa em
[validação da prévia](validacao.md#ciclo-automático-e-bancada-da-prévia--2026-11-11).

Para acompanhar fragmentação de memória na ESP32, comparar heap livre e maior
bloco livre, conforme a [documentação da Espressif](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32/api-reference/system/heap_debug.html).
O projeto já usa buffers parciais/DMA e recorte das regiões alteradas, mecanismos
descritos no [LVGL 8.4](https://lvgl.github.io/open-docs/HTML/8.4/overview/drawing.html).
A prioridade é medir os piores intervalos e o atraso dos dados, além da média
de quadros; nenhuma dessas medidas equivale automaticamente a CPU livre.
