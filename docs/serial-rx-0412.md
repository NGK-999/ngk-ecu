# Recepção serial independente de respostas longas — 0.4.12

O pedido de setup foi ampliado explicitamente para usar as ferramentas na
melhoria do código. Esta alteração trata o acoplamento RX/TX encontrado na
bateria anterior. A etapa inicial foi validada no computador e por compilação,
sem gravar a placa. **Na etapa física autorizada de 2026-11-12, os quatro casos
concluíram e o normal `0.4.12-rx` foi instalado e verificado.**

## Problema e evidência

Antes, `serialCommands()` retornava imediatamente enquanto o outbox TX tivesse
bytes pendentes e processava no máximo uma linha por iteração. Uma resposta de
diagnóstico longa impedia a aplicação de drenar o ring RX, cujo padrão no
Arduino-ESP32 2.0.17 é 256 bytes. As direções UART são independentes; a pausa
era uma decisão do consumidor da aplicação.

A bateria física anterior registrou `control_timeout` com USB100Hz, inclusive
sem kernel de carga, mas não confirmou overflow nem o estágio da falha.
O modelo em memória reproduz um mecanismo compatível: UART115200/8N1, RX 256 B,
TX 64 B por iteração, loop de 6 ms, resposta longa de 900 B e uma consulta de controle em
seguida. Na política antiga, 100 Hz resulta em 175 B descartados e timeout.
Isso não transforma a hipótese de overflow físico histórico em confirmação.

## Alteração limitada

[SerialIngress](../firmware/ecu/include/serial_ingress.hpp) mantém o parser real
de 95 caracteres e uma linha pronta dentro do próprio parser. Não aloca heap,
não copia uma segunda linha nem cria tarefa. Processa várias linhas até um
orçamento de 256 bytes por iteração, deixando o loop retornar à UI.

- Frames `SIM1` e erros do parser esperam reserva de 64 B para sua eventual
  rejeição. Um frame válido continua silencioso e pode ser consumido durante TX.
- Controles só executam quando há 1024 B disponíveis para a resposta. Se não há,
  a linha permanece pendente e nenhum efeito/ACK é repetido ou ultrapassado.
- `screen-capture` exato é uma barreira até TX vazio, preservando a ordem
  texto→imagem e o `uart_wait_tx_done` existente. Os outros comandos não
  passam a transmitir binário sobre texto pendente.
- O ring RX permanece 256 B e a saída limitada a 2048 B. Não houve aumento de buffers,
  troca de baud rate, alterações de GPIO, TTL de 750 ms ou protocolo/sessão.

O novo parser ingressa na mesma tarefa main/UI. Nenhuma chamada LVGL passa para
ISR ou outra tarefa. Filas e snapshots dos demais serviços permanecem iguais.

A admissão de 1024 B foi auditada contra os formatos atuais, com máximos de
campos numéricos e variantes condicionais: info≤996B, load-status≤893B,
SD-status≤778B, radios≤386B, SIM-status≤94B e ready≤65B.
O erro de frame SIM cabe em 48 B e o erro de parser em 28 B. O relatório assíncrono
da UI é maior e conserva seu caminho anterior de append/repetição posterior.
Ao acrescentar campos/novos controles, rever esses limites antes de executar
efeitos; não aumentar respostas silenciosamente acima da reserva.

## Uso concreto das referências

Espressif Documentation foi consultado por MCP sobre ring RX e bloqueio TX;
LVGL Documentation, sobre propriedade da UI e validade dos buffers DMA.
Como as respostas incluíram fontes atuais, foram confrontadas com
[UART ESP-IDF4.4.7](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/peripherals/uart.html),
[LVGL8.4 threads](https://lvgl.io/docs/open/8.4/porting/os), o SDK Arduino local
e a skill ESP32 já instalada. Foram aplicados drenagem limitada sem espera,
memória fixa e proprietário único, sem copiar APIs de versões novas.

O workflow de instalação nova recuperado de Engineering foi lido e considerado
inaplicável ao Arduino existente; não foi usado para trocar SDK/target.
REA permanece ferramenta local de inspeção, não medidor de CPU nem prova de
correção física. Não há alegação de FPS ou percentual de CPU economizado.

## Testes e limites

O [teste nativo](../tests/native/test_serial_ingress.cpp) usa o helper real de
produção, parser, TX e estado SIM com transporte/tempo em memória.
RED→GREEN: no mesmo modelo, o caminho novo recebe 250/250 frames a 100 Hz, com
zero bytes descartados e ocupação RX máxima de 43 B; o controle recebe sua resposta.
20/50 Hz e orçamento por iteração também são exercitados.

Outros casos cobrem efeito/ACK exatamente uma vez, frame inválido, NUL/CRLF,
limites de 95/96 caracteres, ordem start→frame→stop→frame, TTL sem renovação em idle
e captura retida até drenar o último byte de texto. O modelo é determinístico;
não mede USB físico, escalonamento real da UI ou transientes elétricos.

Reprodução sem hardware:

```sh
sh scripts/test-native.sh
sh scripts/test-simulator.sh
PLATFORMIO_CORE_DIR="$PWD/.tools/platformio" .venv/bin/pio run -d firmware/ecu -e ecu-normal
```

A candidata usa `0.4.12-rx`; os ambientes diagnósticos e seus clientes foram
identificados com 0.4.12 para evitar ensaiar uma versão diferente por engano.
Ao encerrar a etapa inicial, a última instalação confirmada ainda era 0.4.11;
o teste em memória não foi tratado como resultado físico. A bancada posterior
está registrada separadamente abaixo.

## Bancada USB concluída — 2026-11-12

Base `40626184d754dd9b4e12a37ed68a74d3690ca8e0`, após nova execução dos
22 executáveis nativos ASan/UBSan, 102 testes Qt/cliente em memória e builds
`ecu-normal`/`ecu-load-benchmark`. A ACL temporária foi concedida pelo operador
somente ao usuário/dispositivo da serial. O aplicativo 0.4.11 foi preservado
em backup privado do slot e teve seu prefixo conferido pelo SHA-256 conhecido;
a tabela de partições foi comparada antes de qualquer escrita.

O diagnóstico `0.4.12-load-bench` executou controle USB100, nominal USB100 e
nominal USB50, com reset antes de cada caso e carga de 15 s. Depois, somente
o aplicativo normal novo foi gravado em `0x10000`, com escrita e
`verify-flash` aprovados. No normal `0.4.12-rx`, outro caso USB100 manteve
12 consultas concorrentes de estado e verificou o TTL ao interromper o envio.

| Caso | Envio observado | Slots pulados pelo host | Apresentações / janela UI | p95 do intervalo UI | Maior tick UI |
| --- | ---: | ---: | ---: | ---: | ---: |
| Controle USB100, diagnóstico | 100,025 Hz | 0 | 600 / 10.000 ms | 25,637 ms | 16,512 ms |
| Nominal USB100, diagnóstico | 100,020 Hz | 0 | 600 / 10.001 ms | 25,672 ms | 16,858 ms |
| Nominal USB50, diagnóstico | 50,054 Hz | 0 | 600 / 10.000 ms | 25,377 ms | 16,639 ms |
| USB100, aplicativo normal | 100,031 Hz | 0 | 600 / 10.000 ms | 25,929 ms | 14,591 ms |

Os quatro casos concluíram sem timeout, com janelas UI sintéticas completas e
não interrompidas, SIM encerrada, rádios restaurados e porta fechada; as cargas
diagnósticas também tiveram parada confirmada. No normal, a consulta após uma
pausa de 900 ms confirmou `stale`/`live=false`, seguida de `sim-stop` confirmado.
Os casos de controle e nominal USB100 que antes falharam com `control_timeout`
concluíram neste reteste; as falhas históricas continuam preservadas na
[bateria anterior](bateria-carga-sintetica.md).

Na carga nominal USB100: 1500 lotes produzidos, 1380 consumidos,
120 substituídos, zero pendentes e zero períodos perdidos do scheduler.
As 120 substituições pertencem ao mailbox da carga sintética, não ao USB.
Taxa de envio e slots pulados descrevem o produtor no host; o watermark de
sequência SIM não conta frames aceitos nem permite afirmar perda USB zero.
Heap mínimo e folgas de pilha do diagnóstico cobrem o boot/tarefa inteiro,
não apenas a janela UI nem o consumo do aplicativo normal.

A barreira texto→imagem também foi exercitada na placa: `info` seguido de
`screen-capture`, na mesma escrita, concluiu com texto antes do binário,
153600 B recebidos e CRC32 `d93988db` verificado. O PNG privado foi inspecionado:
menu legível, SD pronto e conexões desligadas. Isso não compara seus pixels com
capturas históricas nem mede atualização óptica do LCD.

Estado final, reconferido após a captura: normal `0.4.12-rx`, SD montado,
perfil persistido, SIM e rádios desligados, sensores/atuadores desabilitados.
A tabela de partições permaneceu byte a byte igual. Não foram enviados comandos
de escrita de NVS/calibração/tabela nem de formato SD. Aplicativo final:
1.424.480 B, SHA-256
`f3a58a5df29cf06d5fffc7801331c33f92e10407547a54532764eefa89a22ea5`.

[Agregados revisados dos quatro casos, entrega e captura](measurements/serial-rx-2026-11-12.json).
Uma execução por caso, sem refresh SD, busca Wi-Fi ou anúncio BLE nesta rodada.
A prévia host pertence à etapa anterior e não foi reexecutada nesta bancada.
Apresentações contam transferências parciais DMA; tempos UI são de parede.
Não há medição de FPS óptico, percentual de CPU livre, sensores reais,
condicionamento automotivo, injeção/ignição ou veículo.
