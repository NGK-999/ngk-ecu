# Reduzir trabalho contínuo antes da aquisição física

**Pedido:** otimizar e limpar o código para ampliar sensores no futuro.
**Base:** 482e3fc, preview/painel-disposicao; normal 0.4.10 instalado.

## Escopo da implementação

1. [x] Evitar módulo64 no caminho comum de FrameClock, mantendo fórmula exata
   para saltos grandes, rollover, modos e descarte de quadros atrasados.
2. [x] Recalcular fator Q8 da barra só na mudança de escala; amostrar easingRPM
   uma vez por quadro quando barra/número precisarem. Preservar pixels e alarmes.
3. [x] Com ambos os rádios desligados, bloquear o worker na própria fila; comandos
   continuam despertando-o imediatamente. Com rádio ativo, manter20ms de polling.
4. [x] Retornar antes de consultar UART quando TX vazio; consumir snapshotSD como
   mailbox de estado mais recente, atualizando a UI somente após uma publicação.
5. [x] Validar equivalência/testes/builds, observar recursos e fluidez na placa,
   revisar e publicar na mesma branch. Não atribuir economia de CPU a linhas removidas.

## Limites

Não habilitar GPIO/sensores/atuadores nem alterar protocolos/TTL/cadência/layout,
NVS, calibração, partições ou cartão. Não criar filas de sensores ou logger sem
consumidor real. DMA20KiB e poolLVGL32KiB preservados. Não retomar o helper de
invalidação de glifos rejeitado. Não alterar SDK/global/dependências por estética.

A tentativa de usar getAttVal no RX foi descartada na compilação: é protected
na NimBLE2.5.1. O getter por cópia continua; não usar cast ou acesso interno.

## Evidência e validação

Fonte/objdump da base mostram __umoddi3 no FrameClock e __divsf3 no progresso
RPM/easing. Testes usarão oráculo original e sequências determinísticas de
modos/tempo, mais posições Q8 em todas as escalas permitidas. Testes nunca abrem
hardware. A UART tem mutex no availableForWrite, mesmo sem bytes a enviar;
SD tem um único consumidor de snapshot; rádio publica estado inicial antes de esperar.

Bancada é separada: identificar versão/flags e preservar app anterior antes de
qualquer flash. Comparar fonte sintética20Hz/movimento/repouso e resposta dos
rádios/refreshSD, sem associação ou pareamento com terceiros. Contadores de
trabalho são agregados; tempo de parede não é percentual de CPU livre.

## Resultado

Concluído e revisado: normal0.4.11 instalado, hash de escrita conferido,
20 testes nativos/87 simulador/4 builds/auditoria aprovados. Nove cenários
concluídos, worker sem retornos periódicos off, ativo responde a comandos,
Wi-Fi scan/BLE anúncio e refreshSD aprovados. Capturas byte a byte iguais.
Detalhes: [relatório](../../otimizacao-recursos-0411.md). Não declarar novo
percentual de CPU livre; a evidência é equivalência e operações eliminadas.
