# Inicialização direta no painel — 0.4.13

Pedido do responsável em 2026-11-12: ao ligar, entrar diretamente no painel,
pois o dispositivo será usado como painel da gaiola. O aplicativo normal se
identifica como `0.4.13-panel`.

## Comportamento

`ui::begin()` cria o painel, sem passar pelo menu. Sem dados, RPM, velocidade e
temperatura mostram `--`; óleo mostra `SEM SINAL`. Não inicia DEMO nem SIM USB.
Duplo toque continua retornando ao menu, com os mesmos limites de contato,
debounce e mudança de página fora do callback LVGL.

Se não houver calibração válida do touch, os cinco pontos continuam obrigatórios;
a conclusão da calibração inicial abre o painel. Recalibração manual pelos
ajustes continua retornando ao menu. Nenhuma calibração é apagada pela atualização.

O perfil é carregado pelo serviço de armazenamento depois que a UI inicia.
Na primeira chegada do perfil, o painel atual é reconstruído uma vez para usar
as legendas das unidades salvas. Isso não redireciona quem já abriu outra página.
Nenhum GPIO, protocolo, parser serial, tarefa, buffer ou SDK foi alterado.
Variantes diagnósticas/referência preservam suas identidades anteriores.

## Verificação local

- 22 executáveis nativos com ASan/UBSan passaram, incluindo gesto e calibração.
- 102 testes do simulador/transporte em memória passaram.
- `ecu-normal` compilou: RAM estática 96.848 B, flash 1.417.933 B.
- Prévia com LVGL 8.4 real passou nas onze cenas; capturas inspecionadas.
- Três testes do driver de prévia passaram.

O harness observa o boot real antes de qualquer navegação de prévia. A regressão
falhou primeiro com a entrada antiga no menu e depois passou. Outro caso
publica um perfil imperial após o boot: falhou antes da correção das legendas e
passou após ela. Conclusão de calibração é sintetizada no host para verificar
navegação, sem comprovar ajuste físico dos cinco pontos ou persistência NVS.

## Estado da instalação

A versão normal `0.4.13-panel` foi instalada em 2026-11-12 somente em app
`0x10000`, com escrita e `verify-flash` aprovados. Backup privado da 0.4.12-rx
validado pelo prefixo conhecido antes da gravação; tabela comparada com o perfil
antes da escrita. Aplicativo de 1.424.512 B, SHA-256
`59b3d1341247adacfa068a13f38e0ff57d71d16f7a5cff911431a0e6cabc9b41`.

Após reiniciar, foram enviados apenas comandos de leitura de estado/captura,
sem comando de navegação ou início de SIM. A captura de 153600 B com CRC32
`eaa0e4ed` foi inspecionada: painel aberto com leituras indisponíveis e dica de
duplo toque. Isso confirma o boot da aplicação no painel nesta placa, sem
representar ensaio elétrico de partida no veículo.

A primeira checagem posterior à captura encontrou o SD ainda desmontado e
recusou a aprovação final. Leituras seguintes acompanharam a conclusão da
montagem; três consultas estáveis confirmaram SD montado e perfil persistido.
Não foi necessário comando de refresh, formato ou alteração do firmware.
Estado final: SIM/Wi-Fi/BLE desligados, entradas e atuadores desabilitados,
porta fechada. Não houve comando de escrita NVS/calibração/tabela ou formato SD.
Duplo toque e calibração passaram no host; não houve novo ensaio manual do touch.

[Agregados sanitizados da entrega](measurements/boot-panel-2026-11-12.json).

Prévia host não valida boot elétrico, toque físico, FPS óptico, sensores reais
ou veículo. Backup e evidência da instalação devem permanecer locais e privados.
