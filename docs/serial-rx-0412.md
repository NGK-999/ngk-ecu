# Recepção serial independente de respostas longas — candidata 0.4.12

O pedido de setup foi ampliado explicitamente para usar as ferramentas na
melhoria do código. Esta alteração trata o acoplamento RX/TX encontrado na
bateria anterior. **Foi validada no computador e por compilação; não foi
gravada nem ensaiada na placa nesta etapa.**

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
A última instalação confirmada continua 0.4.11. Uma próxima etapa física deve
gravar deliberadamente a candidata e repetir os casos de 100 Hz, sem presumir que
o resultado do modelo já resolveu o timeout na placa.
