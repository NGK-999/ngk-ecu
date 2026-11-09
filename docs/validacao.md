# Validação de bancada — 2026-11-09

## Sistema e execução local, sem hardware

Ferramentas isoladas na pasta do projeto: Python 3.12, esptool 5.6.0,
PlatformIO 6.1.18, Node 24.22.1 e REA 6.8.0. `pip check` passou.
O cache PlatformIO está em `.tools/platformio`; não houve alteração de
Python/Node do sistema, grupos ou configuração global de agentes.

A conta da sessão não tinha sudo. O operador usou uma conta administradora
local para conceder ACL somente ao usuário/dispositivo da serial. Uma
reconexão USB removeu a concessão e exigiu repeti-la. O agente não recebeu senha
nem alterou sudoers. A confirmação final de acesso precedeu cada gravação.

Testes nativos não abrem portas, montam discos ou formatam cartão:

```sh
sh scripts/test-native.sh
```

A suíte cobre layout/áreas de toque, debounce, calibração de cinco pontos,
parser serial (incluindo NUL/CR/overflow), política de autorização de manutenção
e validação sintética de CID/CRC7/CRC16. Cinco executáveis passaram. Usou C++17, `-Wall -Wextra -Werror`,
AddressSanitizer e UndefinedBehaviorSanitizer. A revisão de menu/duplo toque
passou nos casos de dois contatos completos, limites temporais, arraste,
contato longo, troca de página, debounce e virada de `uint32_t`.

Builds da base própria normal e de manutenção passaram com Arduino-ESP32
2.0.17, LVGL 8.4.0 e LovyanGFX 1.2.7. A versão com o fluxo novo de menu/painel
é `0.1.1-bench`, instalada e confirmada pela serial, com SD montado.

Artefato normal final: **748.560 bytes**, SHA-256
`761f211bef2c094b01bc84ac6ac135c3c7d852ceedde87e542821c6bee241834`.
Gravação verificada pelo esptool. O linker reportou 58.336 bytes de RAM
estática e 748.201 bytes de aplicação. Buffers DMA são alocados separadamente;
isso não constitui medição de pico ou de folga do heap em execução.

## Identificação e bancada real

| Verificação | Resultado observado |
| --- | --- |
| Ponte USB | CH340, VID:PID `1a86:7523` |
| Chip | ESP32-D0WD-V3, revisão 3.1, dois núcleos, 240 MHz |
| Flash | 4.194.304 bytes (4 MiB) |
| PSRAM | 0 bytes detectados pela aplicação de bancada |
| Backup anterior | Cópia integral local privada; `verify-flash` conferiu contra a placa antes de sobrescrever |
| Gravação de bancada | Bootloader, partições e aplicação conferidos pelo esptool; atualizações seguintes somente aplicação |
| Instrumentos | Modo `bench`, entradas de sensores e saídas de atuação desabilitadas |

A imagem com perfil ST7789 foi confirmada legível pelo responsável. Isso
não determina o sufixo do controlador, o vidro ou a revisão completa da PCB.
Não houve leitura de MAC/CID publicada nem dump incluído no repositório.

## Cartão: divergência bloqueada e alvo correto validado

A primeira consulta reportou **1.898 MiB**, divergindo dos 32 GB autorizados.
Nenhum comando de formatação foi enviado para esse cartão. O responsável
retirou o cartão errado e informou ter inserido o correto.

Uma consulta nova confirmou **61.962.240 setores de 512 bytes**, ou
**30.255 MiB**. O cartão respondia, mas não montava como FAT. Não se inferiu
exFAT, NTFS ou defeito somente desse resultado.

Na manutenção própria NGK ECU, capacidade e identidade CID foram validadas;
a identidade permaneceu apenas em RAM. O comando explícito foi enviado uma
única vez, vinculado ao snapshot/setores/token da sessão. O worker reconferiu
CID e geometria antes da escrita. O resultado foi FAT32 com sucesso, montagem
aprovada, escrita/leitura de arquivo próprio temporário aprovada e remoção desse
arquivo. Não foram listados nem lidos arquivos pessoais.

Após a operação, foi instalado novamente o firmware **normal**. A consulta
confirmou `build=normal`, SD montado e o mesmo tamanho. Uma tentativa inválida
de comando de formato recebeu `ERROR command rejected`. O artefato normal não
contém o comando próprio `FORMAT_SD `; o SDK ainda vincula `f_mkfs` pela sua
biblioteca, mas ambas as montagens passam `format_if_empty=false`.

O campo `sd_rw_ok` informa o teste de escrita/leitura feito **naquele boot**.
Ele foi verdadeiro na manutenção concluída e começa falso no normal, que não
repete escrita de teste a cada inicialização. Isso não transforma uma montagem
normal aprovada em falha de leitura/escrita.

## Inspeção visual e interação

A referência externa vale somente como identidade visual. O firmware próprio
contém apenas o domínio da ECU: RPM, velocidade, temperatura e óleo, com dados
indisponíveis até instalar os sensores. Sem telas de turno/produção industrial.

O responsável confirmou fisicamente, na versão `0.1.1-bench`, o painel com
instrumentos grandes e o duplo toque retornando ao menu. Essa confirmação
valida o fluxo de interação observado nesta unidade, separadamente dos testes
nativos. Não houve medição de precisão do touch ou latência óptica.
As métricas de 59 apresentações parciais/s da referência não são medições da ECU.

## Motor, veículo e próximos ensaios

Não foram conectados sensores do motor, injetores, bobinas ou outras cargas.
Não houve teste de alimentação automotiva, ruído, transientes, vibração, sol,
latência óptica ou ensaio no veículo. A ECU permanece uma base de instrumentação
em bancada, sem injeção/ignição. O próximo levantamento é dos sensores/circuitos
reais do 1300 carburado e das entradas adicionais de RPM, temperatura e velocidade.
