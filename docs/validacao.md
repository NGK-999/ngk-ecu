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
Gravação verificada pelo esptool. O linker reportou 58.336  bytes de RAM
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

## Revisão automobilística 0.2.0-race

Pedido explícito: tema inspirado nas fotos FuelTech, configurações ampliadas,
Wi-Fi e Bluetooth nesta etapa. O cartão não foi reformatado. A bancada continua
sem sensores/atuadores do motor.

### Execução local e inspeção de código

Doze executáveis nativos C++17 com ASan/UBSan cobrem também codec de ajustes,
recuperação de perfil e escrita interrompida, preservação após erro de leitura,
entrada local de senha, rascunho preservado após confirmação tardia, limites de
credenciais/BLE, deadlines e montagem saudável estável. Nove testes Python da
fonte reduzida verificam geração, estrutura e equivalência de pixels/métricas
através dos callbacks reais do LVGL. Compilar não acessa USB.

REA executado sobre ELF próprio, conforme [ferramentas.md](ferramentas.md).
A fonte de dígitos economiza 92.084 bytes constantes; Wi-Fi/BLE acrescentam
código e RAM à imagem integrada. Não confundir economia de um recurso com
redução líquida do firmware ao adicionar funcionalidades.

A revisão encontrou e corrigiu: símbolo inexistente no LVGL8.4, nomes dos
botões cortados por alinhamento relativo ao centro, distinção entre perfil
corrompido e erro de leitura, e memória disputada pelo teclado/modal BLE.
Quando chega pedido BLE durante digitação, a entrada local é apagada e o
teclado destruído antes de abrir a confirmação.

### Inspeção visual headless

Capturas renderizadas a partir do próprio ui.cpp e LVGL8.4 em host, com drivers
stub e redes sintéticas; não são fotos da placa. Avaliação de menu, painel sem
sinal, demonstração, ajustes, redes, teclado e confirmação. O host usa ponteiros
de 64bits e não serve para inferir consumo exato no ESP32 de 32bits. Essa inspeção
não valida precisão do touch, brilho físico ou latência óptica.

### Desempenho de bancada

Cena controlada:20 redesenhos completos e 10 s de alterações parciais solicitadas
a cada 17 ms. Mesma rampa de valores nos ensaios; revisão nova acrescenta barra
segmentada ao desenho. Fonte dos números [método](armazenamento-e-desempenho.md).
Base instrumentada SHA-256:
`f5f27fda755a09501c057b744e3d7c2bd9c6504c77d8e46a16f21aed37e53224`.

| Cenário | Parciais/s | p95 parcial | p95 tela completa |
| --- | ---: | ---: | ---: |
| Base anterior, rádios off |53,1|21,126ms|64,987ms|
| 0.2.0, rádios off |56,6|18,089ms|56,696ms|
| 0.2.0, Wi-Fi com scan durante a cena |56,5|19,068ms|56,693ms|
| 0.2.0, BLE anunciando presença |56,5|19,030ms|56,693ms|
| 0.2.0, diagnósticos seriais contínuos |56,0|19,675 ms|53,581ms|

O p95 parcial é uma janela dos últimos 256 tempos; não representa todas as
amostras do ensaio. Contagem/máximo abrangem a fase parcial. Não há medição
óptica nem validação de sensores, cargas ou comportamento no veículo.

O primeiro protótipo novo havia regredido para 49,2/s e p95 de 26,117 ms.
A revisão final usa segmentos retangulares, polling LVGL mais frequente e
invalidação restrita à largura dos números. A cena nova também inclui barra
segmentada, ausente na base. Não atribuir causalidade individual a cada ajuste
sem ensaio isolado. Resultado final é comparação da aplicação como um todo,
uma execução por cenário, sem afirmar distribuição estatística entre ensaios.

No alvo: exportação de perfil completou, importação completou e confirmou
persistência em NVS; SD continuou montado com 30.255MiB. Repetições de ativar e
desligar Wi-Fi/BLE completaram. A tentativa de iniciar BLE com Wi-Fi ligado
foi recusada com MemoryLow; não alegar teste simultâneo dos dois. Conexão Wi-Fi
com senha e pareamento autenticado com cliente externo não foram ensaiados.

Após reiniciar, a consulta confirmou perfil restaurado, modo normal, rádios
off e SD montado; a rejeição do comando inválido de formato foi observada. A versão anterior
teve painel/duplo toque confirmados fisicamente pelo responsável; a confirmação
visual da revisão 0.2.0 pelo responsável permanece separada e pendente.

Imagem normal final: **1393056 bytes**, SHA-256
`b4b5b89f354111a66a655da2a7de7f6f3747af7a3d4647981f2f25e40c96e120`. Gravação conferida pelo esptool.
Linker: 95.544 bytes de RAM estática e1.386.485 bytes de aplicação (70,5% do slot).
Os números em execução não equivalem ao RAM estático do linker.

Heap em `info`/`radios` usa MALLOC_CAP_INTERNAL; o benchmark usa 8BIT.
Essas capacidades diferem, portanto não subtrair valores entre esses relatórios
como se fossem a mesma métrica. Heap mínimo cobre todo o boot, inclusive
ensaios de rádio anteriores; não é mínimo isolado de cada janela.
