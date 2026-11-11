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

## Desempenho conhecido e próximos ensaios

A 0.4.8 teve aproximadamente 599–601 apresentações parciais em dez segundos
nos cenários com movimento; em repouso, deixou de calcular/formatar os
instrumentos depois de estabilizar. A prévia 0.4.9 teve testes de layout e
compilação, mas ainda não tem nova medição física. Dados e limites completos
em [validação da 0.4.8](validacao.md#fluidez-graduação-e-falhas-simultâneas--048).

Próximos incrementos propostos, ainda não implementados:

1. Um roteiro temporizado no simulador: partida, aceleração, troca de marchas,
   frenagem, falhas simultâneas e recuperação, repetível com os mesmos tempos.
2. Um ensaio de 30–60 minutos reais com Qt e transporte em memória, acompanhando
   crescimento de memória, threads, atrasos dos timers e recuperação de link.
3. Medir a prévia na própria ESP32 com dados sintéticos antes de novas
   otimizações: regularidade dos quadros, latência, maior pausa de toque e
   memória disponível. Esse ensaio requer placa conectada, mas não sensores.

Para acompanhar fragmentação de memória na ESP32, comparar heap livre e maior
bloco livre, conforme a [documentação da Espressif](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32/api-reference/system/heap_debug.html).
O projeto já usa buffers parciais/DMA e recorte das regiões alteradas, mecanismos
descritos no [LVGL 8.4](https://lvgl.github.io/open-docs/HTML/8.4/overview/drawing.html).
A prioridade é medir os piores intervalos e o atraso dos dados, além da média
de quadros; nenhuma dessas medidas equivale automaticamente a CPU livre.
