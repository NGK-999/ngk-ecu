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
`761f211bef2c094b01bc84ac6ac135c3c7d852ceedde87e542821c6bee 241834`.
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

A revisão encontrou e corrigiu: símbolo inexistente no LVGL 8.4, nomes dos
botões cortados por alinhamento relativo ao centro, distinção entre perfil
corrompido e erro de leitura, e memória disputada pelo teclado/modal BLE.
Quando chega pedido BLE durante digitação, a entrada local é apagada e o
teclado destruído antes de abrir a confirmação.

### Inspeção visual headless

Capturas renderizadas a partir do próprio ui.cpp e LVGL 8.4 em host, com drivers
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

Heap em `info`/`radios` usa MALLOC_CAP_INTERNAL; o benchmark usa 8 BIT.
Essas capacidades diferem, portanto não subtrair valores entre esses relatórios
como se fossem a mesma métrica. Heap mínimo cobre todo o boot, inclusive
ensaios de rádio anteriores; não é mínimo isolado de cada janela.

## Cockpit 0.3.0 — composição e navegação

Fonte: responsável rejeitou o acabamento 0.2.0 e autorizou pesquisa/escolha da
reformulação. Confirmou que os botões respondiam na placa 0.2.0; não houve relato
de defeito de touch. Revisão0.3.0 preservou pinagem, driver, calibração e gesto.

O painel agora é contínuo e assimétrico: RPM/barra no alto, velocidade maior
à direita, bloco/óleo à esquerda. Menu com Painel em destaque. Ajustes possui
seis categorias, abas Unidades/RPM e Salvar somente nos parâmetros visuais.
DEMO abre diretamente o painel. Fontes numéricas Barlow Condensed Bold/OFL,
convertidas oficialmente e com cmap normalizado para LVGL 8.4; sem leituraSD.

### Execução local e revisão visual

Doze executáveis nativos ASan/UBSan e doze testes de fonte passaram. Três testes
novos verificam o mapa e os callbacks/fontes reais; com caches locais, reprodução
exata e 24glifos comparados ao conversor oficial. Sem rede durante os testes;
a comparação de reprodução é explicitamente pulada em instalação sem caches.
Gerador --check passou. Builds normal/manutenção/benchmark passaram.

O teste de layout foi observado falhando na geometria antiga e passando na
nova. Dois revisores independentes examinaram navegação, callbacks/DMA e pool.
Harness local de ponteiro real LVGL usou display/touch/tempo/serviços sintéticos:
menu, 6categorias, abas, Voltar/Menu/Salvar, confirmação tardia e DEMO/duplo toque
passaram. Rascunho preservado entre telas. Quarenta ciclos não perderam memória:
17.488 B livres antes/depois no índice; modalBLE sobre painel 6200B e após fechar
9632B. São valores do host 64bits, não RAM da placa. Host sem recortes nas
transições de dígitos; capturas publicadas são renderizações do código.

### Medição física da revisão

Mesmo produtor sintético 17 ms,20 redraws completos e 10 s parciais; conta últimaDMA.
Uma execução por cenário, sem inferir resultados ópticos ou precisão de sensores.

| Cenário 0.3.0 | Parciais/s | p95 parcial | p95 completo |
| --- | ---: | ---: | ---: |
| Rádios off |57,6|17,114ms|55,688ms|
| Wi-Fi com busca durante a cena |57,6|17,114ms|53,755 ms|
| BLE anunciando presença |57,5|18,081ms|54,744ms|
| Diagnósticos seriais contínuos |57,2|18,102ms|53,795ms|

A versão 0.2.0 mediu56,6/s,p95 18,089ms e full p95 56,696ms; as cenas possuem
composições/fontes diferentes. A comparação é da aplicação completa, não ensaio
isolado de cada otimização. ScanWi-Fi e anúncioBLE separados; proteçãoMemoryLow
voltou a recusar segundo rádio. Associação com senha/pareamento cliente ainda
não ensaiados. Sensores/atuadores permanecem desabilitados, sem teste veicular.

Perfis SD exportados/importados; cartão30.255MiB montado ao fim, rádios off.
Após reiniciar, a consulta confirmou normal 0.3.0, perfil restaurado, SD montado
e rádios desligados; o comando inválido de formato foi rejeitado. O responsável
confirmou na placa que menu, números maiores, índice e toque aparecem e respondem
corretamente. Isso confirma renderização/interação observadas, sem equivaler a
avaliação de leitura no veículo ou preferência estética definitiva.

Imagem normal 0.3.0: **1404240 bytes**, SHA-256
`5ca470324a7cf12f2f74274b0141e3f4027c9e87216a08f86c072cc1faf14ffd`. Gravação conferida pelo esptool.
Linker:95.552B de RAM estática;1.397.665B de aplicação (71,1% do slot).

## Simulador Linux e firmware 0.4.0

Aplicativo Python3.12,PySide6-Essentials6.12.0 e pyserial3.5 na .venv do projeto.
Dependências isoladas, pip check sem incompatibilidades; nenhum runtime do sistema,
grupo, ACL ou configuração global de agente alterado. Atalho instalado no menu
de aplicativos do usuário, sem sobrescrever atalho alheio.

### Execução local, sem serial física

35 testes Python passaram:15 modelo,6 codec,8 link com transporte em memória e6 GUI Qt.
A suíteGUI usaQT_QPA_PLATFORM=offscreen. Valida controles, modo Manual/TPS,
falhas exclusivas, presets, parar/pausar e geometria980×680. CurvaRPM limitada
a 10 s/400pontos em memória; sem arquivos de amostras. RXlimitado, snapshotlatest
sem fila crescente e thread serial separada daGUI. Appinicialmenteoffline;
identificaNGK ECU/protocolo antes de iniciar transmissão.

Regressões RED/GREEN: cancelar antes de ready não deixa sessão órfã (nonce
conhecido antes do start); ready protocolbooleano e token divergente rejeitados;
portas USB oferecidas antes das portas legadas, sem abrir nenhuma na enumeração.

14 executáveis nativos ASan/UBSan e12 testes de fontes passaram. Casos de
protocoloC++/Python golden, limites, replay, stop,TTL 750/wrap e apresentação
validada:0!=ausente, conversões, prioridades e stale sem payload antigo.
Foi reproduzido/corrigido reviver frame após ciclo completodemillis: expiração
observada fica latched até novo frame válido/sessão. TTL também é observado
na calibração. Noncerepetido não altera navegação antes da rejeição.

Dois revisores examinaram protocolo/main, runtimeeUI e cliente. Harness real
LVGL/service com relógio fake passou em live/erros/stale/saída e Sensors. Pool 32 KiB
nohost:PanelSIM74%,8624Blivres; modalBLEsobreSIM85%,5192Blivres,semOOM.
Esses são valores dehost 64bits, não folga medida noESP32.

### Janela real no Linux

Aplicativo visível em Wayland,1120×760, captura inicialoffline inspecionada.
Offscreen também inspecionado980×680; gráfico opcional oculta em janelas
menores, preservando controles e números. O processo ficou aberto para o
operador, sem conexão automática. A captura publicada é da janela nativa com
dados sintéticos em estado inicial, não uma amostra de motor real.

### Bancada física separada

Porta conferiu projetoNGK ECU e normal 0.3.0 antes da atualização. Instalado
normal 0.4.0 emapp0x10000, hashconferido. SD permaneceu montado; nenhum formato.
Protocolo1 identificado, nonceeco confirmado, frame válido aceito, replay
recusado, benchmark bloqueado duranteSIM, TTLstale observado, novosframes
retomaram leituras, flags de falha conferiram e stopcorreto encerrou sessão.
Stopcomtokendivergentefoirecusado. O SerialLink real transmitiu, pausou até
stale, retomou e fechouoffline. Nenhum dado bruto foi gravado/publicado.

Firmware manteve sensor_inputs_enabled=false eactuators_enabled=false.
Sem operação no veículo, sensores reais, injeção/ignição ou medição óptica.
Desempenho do cockpit 0.3.0 permanece histórico; não foi repetido um ensaioFPS
completo0.4.0. O produtorUSB tem cadência alvo20 Hz; isso não é FPSóptico.

Imagem normal 0.4.0: **1409104 bytes**, SHA-256
`fbcf02892fda613158f27ab20cd1cb879a9df7e32474ea2892a6f7a9564c7bc6`. Linker95.648 B RAMestática/1.402.525Bflash (71,3% do slot).

## Simulação de motor e alertas — 0.4.1

### Testes e compilação

`sh scripts/test-simulator.sh`: **55 testes passaram**, com Qt offscreen e
transporte em memória. Cobrem limiares e histerese, RPM ausente, valor manual
9.000, temperatura manual 200 exata, partida fria, lenta variável, inércia,
motor desligado, envelope de marcha e estado térmico. Os testes não abrem
porta física. `sh scripts/test-native.sh`: **14 executáveis passaram** com
ASan/UBSan, incluindo validade, prioridade, TTL e alertas de RPM.

Compilação dos ambientes normal, manutenção e benchmark passou. RAM estática
normal: **95.648 B**; flash: **1.402.945 B** (71,4% do slot). Classificação dos
alertas não adiciona buffers de tela; atualizações preservam o desenho parcial.
Não foi feito novo ensaio de FPS ou medição óptica nesta versão.

### Inspeção da aplicação

Capturas do Qt offscreen conferidas em 1120×760 e no mínimo de 980×680: controles
visíveis, RPM 9.000 integral em vermelho e texto de giro crítico. A aplicação
nativa foi reaberta no Linux após a atualização; começa offline e prioriza a
porta USB. Isso não substitui confirmação visual do responsável na ESP32.

### Bancada física separada

Alvo identificado como NGK ECU normal 0.4.0 antes da escrita. Aplicação normal
**0.4.1-simulator** instalada em 0x10000, hash conferido pelo esptool; não houve
escrita de bootloader, tabela de partições, NVS ou formato do SD.

Firmware/protocolo conferidos após reiniciar. O SerialLink real transmitiu
9.000 RPM, transições de giro e RPM inválido; o receptor confirmou sessão live,
vencimento por pausa e retomada. As cores/alertas são verificados pelo código
puro e pela captura Qt; a bancada não mede opticamente o LCD nem comprova que
o usuário viu os avisos. Encerramento devolveu o link ao estado offline.

`sensor_inputs_enabled=false` e `actuators_enabled=false` conferidos. O SD
reportava **não montado antes e depois** desta atualização; não foi formatado
nem houve tentativa de recuperação, pois isso não era necessário para a
simulação. Nenhuma amostra ou serial bruto foi persistido.

Imagem normal: **1.409.520 B**, SHA-256
`c6dbf2ce1972a78732027478bba78edfd97231ac325e4a54108bf5957ee373d8`.
Referências e limites de interpretação em
[motor e simulação](referencias-motor-e-simulacao.md).

## Cockpit preto e rampa contínua — 0.4.2

Pedido: redesenhar **somente ESP32**, preservando o funcionamento aprovado e o
simulador Linux. Preto/laranja/vermelho, referências FuelTech/Apple e rampa
RPM sólida curva, sem blocos. Firmware normal instalado em `0x10000`, hash de
escrita conferido pelo esptool. Não houve erase_flash integral, formato SD ou escrita de
bootloader/tabela de partições. Modo Suave foi aplicado pelo serviço normal e
salvo em NVS; não houve apagamento de calibração ou credenciais.

### Testes sem hardware e revisão

- `sh scripts/test-native.sh`: **15 executáveis** C++17, Wall/Wextra/Werror,
  ASan/UBSan. Inclui interpolação limitada, retarget, validade/sessão, urgência,
  redução de movimento, relógio racional sem backlog e rollover.
- `.venv/bin/python -m unittest discover -s tests/font -v`: **13 testes**.
  Fonte Gear48 somente `-NR01234`, mapa esparso validado com callbacks reais
  LVGL e geração reproduzível offline; fontes Cockpit anteriores preservadas.
- `sh scripts/test-simulator.sh`: **55 testes**, Qt offscreen/transporte em
  memória. Nenhum deles abre serial física. Modelo, protocolo e visual Linux
  não foram alterados nesta revisão.
- Builds normal/manutenção/benchmark passaram no PlatformIO isolado. Auditoria
  ESP32: 14 sinais, zero erros, três avisos existentes GPIO2/5/15; hash de
  `main.cpp` também protege os pinos SD contra divergência do snapshot.
- ESP-MCP local: stdio, sete schemas, zero tool calls/API/hardware. Runtime e
  fontes fixados, sem registro global ou compilação fictícia via ESP-IDF.

Renderer local com código de produção LVGL 8.4 em host conferiu menu, painel,
extremos 12.000/250/200, N/R, perda de sinal, óleo baixo/superaquecimento e modal.
Testes de integração observaram RED/GREEN para fonte/sessão, urgência, rampa sem
gaps, recoloração após mudar escala e contador DMA aceitando as áreas ampliadas
pela LVGL. Capturas publicadas são desse renderer, com valores sintéticos;
não são fotografias da placa. No host 64 bits, painel usou 51% e modal 62% do pool
32 KiB, sem OOM; esses percentuais não representam o heap do ESP32.

### Bancada física: comparação do benchmark preservado

Antes: firmware 0.4.1, rádios desligados. Depois: 0.4.2, mesmos buffers/DMA,
SPI 40 MHz e benchmark com produtor 17 ms e refresher 5 ms. O método conta somente
a conclusão do último DMA por apresentação; o p95 parcial representa tempo do
pedido à conclusão, nos últimos 256 registros. Não mede luz emitida pelo LCD.

| Medida | Antes 0.4.1 | Depois 0.4.2 |
| --- | ---: | ---: |
| Apresentações / duração | 576 / 10.000 ms | 574 / 10.007 ms |
| Taxa parcial | 57,6/s | 57,36/s |
| p95 pedido→último DMA parcial | 17.108 µs | 17.097 µs |
| p95 tela inteira | 52.979 µs | 45.778 µs |
| Maior intervalo de consulta touch, incluindo tela inteira | 68.072 µs | 56.079 µs |
| Heap livre / mínimo do boot | 109.900 / 108.136 B | 109.564 / 107.744 B |
| Maior bloco de heap livre | 94.196 B | 94.196 B |
| Pool LVGL usado / fragmentação | 30% / 1% | 28% / 1% |

A cena mudou de composição, fontes e objetos; comparação da aplicação completa,
sem atribuir o ganho de tela inteira a uma única técnica. A fase parcial tem
teto próximo de 58,8/s pelo produtor 17 ms; não demonstra por si só animação 60 Hz.

### Bancada física: caminho SIM com interpolação

`motion-bench` observou 10 s com fonte USB sintética a 20 Hz alvo, painel ativo,
modo Suave (`refresh_mode=0`), movimento habilitado e rádios desligados.
Barra tem alvo 60 Hz; números cerca de 30 Hz, detalhes acompanham a fonte 20 Hz.
Não houve touch desabilitado, persistência de amostras ou comandos de atuação.

| Medida | Resultado |
| --- | ---: |
| Duração / apresentações parciais | 10.000 ms / 600 |
| Taxa de apresentações parciais | 60,0/s |
| Transferências contendo a última linha da rampa | 600 |
| Intervalo da rampa, limite superior p95 / máximo | 22.000 / 22.484 µs |
| Intervalo entre apresentações, p95 / máximo | 31.941 / 33.429 µs |
| Maior quantidade de pixels numa apresentação | 15.436 |
| Maior intervalo de consulta touch | 27.021 µs |
| Tempo decorrido acumulado em ui::tick / maior chamada | 4.949.462 / 17.197 µs |

A contagem da rampa marca a conclusão do tile DMA que contém sua linha inferior,
mesmo quando LVGL amplia/junta áreas. Seu p95 usa histograma de 1 ms de toda a
captura; no último bucket, usa o máximo observado como limite conservador.
O intervalo das apresentações usa os últimos 256 registros e mistura números
maiores a 30 Hz com a borda pequena a 60 Hz. Os indicadores medem eventos
diferentes; nenhum equivale a latência óptica ou 60 telas completas/s garantidas.
`ui::tick` inclui espera/preempção; seu tempo decorrido não é utilização de CPU
nem certifica folga para aquisição física. O ensaio não cobre tráfego de rádio,
interrupções reais de motor ou jitter elétrico veicular.

O redesenho mantém dois buffers DMA 320×16 RGB565 (20 KiB), pool LVGL 32 KiB e
nenhum framebuffer extra. RPM usa um objeto de desenho com colunas sujas,
cobertura Q8 e trechos sólidos; números têm buffers estáticos, conversões de
View são cacheadas e não há SD por quadro. Alarmes, TTL e validade continuam
baseados nos dados brutos e ultrapassam o limitador de apresentação.

### SD, sistema e limites físicos

SD montado com **30.255 MiB** após inicialização assíncrona. Exportação e
importação do perfil Suave concluíram com estados Exported/Imported; nenhum
formato nesta revisão. Esse ensaio não é um novo teste de manutenção integral
do cartão nem expande RAM. A falha inicial de consulta ocorreu antes de carregar
perfil/montagem; aguardar o worker resolveu a observação.

Simulador nativo Wayland reaberto após os ensaios; a construção padrão permanece
offline. A ação explícita do agente restaurou cenário marcha lenta e conexão
para o operador experimentar, sem alterar o código do produto Linux.
Firmware conferiu `sensor_inputs_enabled=false` e `actuators_enabled=false`.
Não há ensaio no veículo, sensores reais, limite térmico do bloco calibrado,
controle de combustível/ignição ou medição óptica. Confirmação estética do
responsável para a nova composição ainda é uma observação separada.

Imagem normal: **1.416.512 B**, SHA-256
`985e139abed17f6a9855218088e02ca53bc865ae5a4be37f91b9e6a208dfb72e`.
Linker: **95.984 B RAM estática**, **1.409.933 B flash** (71,7% do slot 1.966.080 B).
RAM estática cresceu 336 B frente à 0.4.1; isso não é medição de heap máximo.
Planos e pendências físicas em [plano-geral.md](plano-geral.md).

## Acabamento dos instrumentos — 0.4.3

Autorização: “faça”, após a pesquisa de referências e a proposta de três
refinamentos. Implementados somente na ESP32: ícones consistentes, âncoras
numéricas estáveis e faixa inferior com ícone/texto maior. Paleta, barra curva,
menu 2×2, gesto, unidades, validade, prioridades e modelo Linux preservados.

### Verificação sem hardware

- **15 executáveis nativos** ASan/UBSan, incluindo faixa que não cobre os
  instrumentos; **13 testes de fontes**, **2 testes de ícones** e **55 testes
  do simulador** passaram. Fontes/ícones foram reproduzidos offline. Os testes
  usam host/transporte em memória e não abrem dispositivos físicos.
- Gerador alpha4: SVGs/licença pinados no Lucide, hashes conferidos. Decoder
  LVGL 8.4 real comparou todos os pixels, ordem de nibbles e largura ímpar.
  Objeto Xtensa dos cinco recursos: 2308 B constantes, zero data/bss/text.
- Renderer LVGL real em host observou RED→GREEN para mudança de quantidade de
  dígitos sem mover a borda de referência e para faixa independente de 22 px.
  Conferiu limites reais dos glifos, todos os 10 alertas + estado normal,
  km/h/mph, °C/°F, máximos/mínimos/ausência, TTL e transições sob modal.
- Revisão independente conferiu lifetime, tintas, flags de toque, geometria,
  prioridades e ausência de nova alocação de texto principal por quadro;
  nenhum problema concreto restante. Três ambientes PlatformIO passaram.
- Auditoria estática ESP32 passou: 14 sinais/0 erros/3 avisos existentes.
  Manifest atualizado após conferir que main mudou somente versão e o filtro
  de benchmark incluiu o novo asset; GPIOs e barramentos não mudaram.

Capturas publicadas são do código LVGL em host com dados sintéticos, não fotos
do LCD. O renderer usa stubs de hardware/serviços. Ele confere desenho/estado e
limites de texto, sem substituir toque real ou medição de latência óptica.

### Instalação e comparação física

Antes da escrita: alvo identificado NGK ECU, normal 0.4.2, flash 4 MiB e entradas/
atuadores desabilitados. Normal 0.4.3 instalado somente em app 0x10000, com hash
conferido pelo esptool. Não houve apagamento integral, formato SD, alteração de
partições, calibração, permissões ou parâmetros de perfil nesta revisão.

`bench` preservou fonte 17 ms/refresher 5 ms e rádios desligados. O método e a
composição diferem do observador de animação; p95 é pedido→último DMA parcial.

| Métrica | 0.4.2 | 0.4.3 |
| --- | ---: | ---: |
| Apresentações / duração | 574 / 10.007 ms | 569 / 10.000 ms |
| p95 parcial | 17.097 µs | 18.089 µs |
| p95 tela inteira | 45.778 µs | 47.975 µs |
| Heap livre / mínimo | 109.564 / 107.744 B | 109.556 / 107.736 B |
| Maior bloco livre | 94.196 B | 94.196 B |
| LVGL usado / fragmentação | 28% / 1% | 28% / 1% |

Observador separado `motion-bench`, fonte SIM USB 20 Hz alvo, Suave com movimento:

| Métrica | 0.4.3 |
| --- | ---: |
| Apresentações parciais / duração | 600 / 10.000 ms |
| Transferências da última linha da rampa | 600 |
| Intervalo da rampa, p95 superior / máximo | 22.000 / 23.771 µs |
| Intervalo entre apresentações, p95 / máximo | 32.019 / 33.994 µs |
| Pixels máximos numa apresentação | 17.782 |
| Maior intervalo de consulta touch | 27.522 µs |
| Tempo decorrido ui::tick acumulado / máximo | 5.065.148 / 18.015 µs |

Mesmas definições/limites da seção 0.4.2: eventos de software/DMA, não FPS óptico,
60 telas completas/s ou percentual de CPU livre. Fonte 20 Hz, barra alvo 60 Hz e
números cerca de 30 Hz. Tempo ui::tick inclui espera e preempção. Não houve
ensaio de rádio/motor, aquisição/atuação física ou validação veicular.

Consulta final confirmou normal 0.4.3, modo Suave/movimento, SD montado com
30.255 MiB e perfil visual persistido carregado. Não houve exportação/importação
ou formato do cartão nesta revisão.

Simulador Linux reaberto ao final, com ação explícita de conectar marcha lenta;
o produto continua iniciando offline e seu visual/código não mudou.

Imagem normal **1.419.104 B**, SHA-256
`b7352b94421aa321ac700eec1d29544bfbb0003cc32c141c54b2a06bf5d8d987`.
Linker **95.992 B RAM estática** e **1.412.529 B flash**, crescimento de 8 B RAM
estática e 2596 B flash frente à 0.4.2. Buffers DMA 20 KiB/pool LVGL 32 KiB mantidos.
Ícones permanecem na flash e não há SVG/Qt/Flow embarcado ou framebuffer extra.

## Captura direta dos pixels — 0.4.4

Pedido adicional durante finalização: capturar a tela da própria ESP32.
Implementado `screen-capture`, somente por comando explícito no firmware normal
de bancada. Espelha os tiles enviados ao LCD, sem alocar snapshot de 153600 B;
RGB565_BE ordenado com contagem/CRC32. Quadro congela durante transmissão e
TTL/validade voltam a ser observados no próximo tick. Isso não é leitura de
retorno físico ou fotografia do LCD.

### Verificação sem hardware

**16 executáveis nativos** ASan/UBSan passaram: inclui geometria de tiles,
ordem/limites, CRC32 conhecido, escrita parcial, timeout e rollover. **6 testes
do cliente** passaram com streams sintéticos: RGB565/PNG, bytes LF no payload,
truncamento, CRC/geometry inválidos, recusa e timeout transitório.
**55 testes do simulador** passaram com Qt offscreen e transporte em memória.
Três builds PlatformIO passaram; auditoria de pinos/hashes passou com os três
avisos preexistentes. Fontes/ícones são os mesmos já conferidos na 0.4.3.

Renderer real LVGL em host conferiu que o payload completo corresponde byte a byte
aos pixels enviados ao display fake, sem arquivo de serial bruto. Conferiu
recusa antes de header nas páginas sensíveis/calibração e retomada de TTL após a
captura. Os contratos anteriores de âncoras, alarmes, unidades e modal passaram.
Revisão independente não encontrou defeitos de ownership, privacidade,
ordenação ou integridade; sua revisão não abriu dispositivos físicos.

### Bancada física separada

Alvo normal 0.4.3 identificado antes da gravação. App normal 0.4.4 instalado em
0x10000, hash de escrita conferido. Nenhum formato SD, NVS, partição ou permissão
alterados. O cliente identificou NGK ECU e protocolo de captura 1 com entradas/
atuadores desabilitados antes de solicitar imagens.

| Captura recebida da placa | Pixels de payload | CRC32 | Duração |
| --- | ---: | --- | ---: |
| Menu | 153600 B | d93988db | 13.364 s |
| Painel com alerta SIM | 153600 B | 8a17f27e | 13.357 s |

Os dois CRCs conferiram no Linux antes de salvar PNG. PNGs inspecionados e
publicados em [captura-tela.md](captura-tela.md); o painel usa somente dados
sintéticos identificados. Nenhuma amostra/serial bruto foi guardada, só os PNGs
pedidos e metadados agregados. LCD físico ainda requer observação própria para
confirmar brilho/cores/toque; captura verifica os pixels produzidos pela ESP32.

Com captura desligada, o caminho SIM foi medido novamente: **599
apresentações parciais em 10000 ms**, rampa 599,
p95 superior da rampa 22000 µs e máximo 34012 µs.
Maior gap de consulta touch 27024 µs. São eventos DMA;
não garantem 60 telas completas/s, latência óptica ou folga de CPU para aquisição.
Os resultados do benchmark de 17 ms anterior continuam identificados como 0.4.3.

Imagem normal **1420480 B**, SHA-256
`f3156d574cf34801e85a0887495ab0ae3a6ff3049919609e536eb302d7265ff5`.
Linker normal: **96016 B RAM estática / 1413909 B flash**. Captura adicionou
24 B RAM estática e 1380 B flash frente à 0.4.3; não há novo framebuffer, driver SVG
ou fontes. Simulador reaberto/streaming após os ensaios; os padrões do produto
Linux continuam offline ao iniciar e seu visual permanece igual.

## RPM compacto e rampa maior — 0.4.5

Pedido: valor RPM mais compacto no topo e gráfico de rotação maior, com nova
imagem FuelTech como referência. RPM agora usa a fonte Barlow48 de 34 px efetivos
(antes 48 px), com referência fixa numa área 120×34 em y2. Legendas/fonte/escala
ficam ao lado, sem conflito; identificação SIM segue visível. A marca fica no
menu. Gráfico 304×56 em y44..99, antes 304×24: altura aumentada 133%. Demais
instrumentos, toque/gesto, alertas, captura e fonte SIM foram preservados.

**Verificações sem hardware:** 16 executáveis nativos ASan/UBSan e 13 testes de
fontes passaram. O teste de geometria falhou primeiro no cabeçalho antigo e
na altura 24, depois passou com a nova composição. Gerador reproduziu 48/64/72 e
Gear48 offline; bytes 64/72/Gear ficaram iguais. Fonte 48 tem linha 34/base 0,
12000 ocupa 101 px e cinco dígitos até 115 px. Renderer real LVGL em host conferiu
extremos/ausência, °C/°F, kmh/mph, todos os 10 alertas+normal, modais, captura e
textos do cabeçalho inclusive MAX12000. Revisão independente sem achados.

Três builds passaram. ELF normal contém 48/72 e descarta a fonte 64 por ausência
de referência. Linker: RAM estática 96016 B (igual 0.4.4), flash 1411149 B, redução
de 2760 B. Buffers DMA 20 KiB e pool LVGL 32 KiB não mudaram. Auditoria estática de
pinos/hashes passou, mantendo os três avisos existentes. main mudou somente
versão; GPIOs e barramentos permanecem iguais.

**Bancada:** alvo normal 0.4.4 identificado antes. Normal 0.4.5 instalado só em
app 0x10000, com hash de escrita conferido. Sem formato SD, NVS, partições ou
permissões alteradas. Captura real dos pixels enviados pela placa recebida e
inspecionada, payload 153600 B, CRC32 `64495377`, duração 13.363 s.
A imagem usa dados SIM e está em [captura-tela.md](captura-tela.md).

Com captura desligada e modo Suave: **598 apresentações parciais em
10000 ms**, rampa 598, intervalo p95 superior da rampa
21000 µs/máximo 35308 µs. Maior gap de consulta
touch 24088 µs; ui::tick decorrido acumulado 3773423 µs,
máximo 13642 µs. São eventos DMA/tempo decorrido, sem medição óptica,
60 telas completas/s garantidos ou percentual de CPU/folga de aquisição certificada.

Imagem normal **1417728 B**, SHA-256
`45db589d0d26955b405ba711f2460a02d8b02cdf5d52761107ae95f60ac1cbd2`.
O simulador Linux foi reaberto/streaming após os ensaios. Sensores/atuadores
físicos continuam desabilitados; nenhum ensaio de motor/veículo.

## Distribuição mais compacta — 0.4.6

Pedido: analisar a captura e aproveitar os espaços. Identificados vazios à
esquerda da velocidade, acima da marcha e entre RPM e curva. A velocidade foi
aproximada de sua unidade, a marcha passou de 34 para 48 px e a temperatura de 28
para 34 px. O TPS já existente em SIM ocupa o vão superior, com barra de 120×4 px;
a escala do gráfico fica visível também em SIM. Sem novos canais de aquisição.

**Testes sem hardware:** 16 executáveis nativos ASan/UBSan e 15 testes de fontes
passaram. Gear64 preserva baseline 1/linha 48 e contém somente -NR01234; os demais
arquivos de fontes foram preservados byte a byte e reproduzidos offline.
Renderer real LVGL em host passou em extremos/ausência, C/F, kmh/mph, N/R, 10 alertas +
normal, captura, modal, escala máxima e TPS inválido/stale. O layout anterior falhou
primeiro no contrato novo de distribuição/fontes maiores, depois passou.

Recorte da rampa limita agora X e altura efetiva da curva. Teste observou
RED→GREEN: pequena variação de giro deixou de transferir o vazio acima da curva.
Comparação pixel a pixel confirmou que atualizações incrementais coincidem com
redesenho completo em subidas/descidas, frações, zero e máximo. Dois revisores
conferiram geometria/estados e código final, sem achados concretos.

Três builds e auditoria de pinos/hashes passaram. RAM estática normal 96024 B
(+8 B frente à 0.4.5), flash 1413613 B (+2464 B). DMA 20 KiB/pool LVGL 32 KiB mantidos,
sem imagem de tela inteira ou SD por quadro. A fonte Gear48 anterior é descartada
pelo linker normal; marcha usa Gear64, RPM/temperatura reutilizam Cockpit48.

**Bancada física:** normal 0.4.5 identificado antes da escrita. Instalado normal
0.4.6 só em app 0x10000, com hash de escrita verificado. Sem formato SD, NVS,
partições, calibração ou permissões alterados. Captura recebida da própria ESP32:
153600 B de pixels, CRC32 `1e4507fe`, duração 13.364 s. PNG inspecionado
com dados SIM em [captura-tela.md](captura-tela.md).

Modo Suave, captura desligada, fonte USB 20 Hz alvo: **598 apresentações
parciais em 10000 ms**, rampa 598, p95 superior da rampa
22000 µs/máximo 34889 µs. Maior intervalo de
consulta touch 26564 µs. ui::tick decorrido acumulado
4029471 µs/máximo 14706 µs. Eventos DMA/tempo decorrido,
não medição óptica, 60 telas completas/s garantidas ou percentual de CPU livre.

Imagem normal **1420192 B**, SHA-256
`1971be3deebe56a0388db032f483f604dda46a9e4bbe4144f9c322fadf2e7d04`.
Simulador Linux reaberto/streaming após os ensaios; modelo/visual Linux intocados.
Aquisição e atuação físicas continuam desabilitadas, sem teste veicular.

## Remoção do TPS do cockpit — 0.4.7

Pedido explícito: retirar TPS do painel. Removidos texto e barra; acelerador,
codec e diagnóstico continuam no simulador/protocolo. RPM/rampa, velocidade,
marcha, bloco, óleo, alertas e identificação SIM USB/DEMO preservados.

**Sem hardware:** build normal passou; renderer real LVGL em host passou com
fonte UI de produção copiada sem alterações e hooks locais. Contratos verificam
ausência do indicador TPS no USB, incluindo alertas, além de stale/TTL, unidades,
extremos, marcha N/R, captura e comparação incremental/completa da rampa.
Revisão independente não encontrou problemas concretos. Auditoria estática:
14 sinais, zero erros e os três avisos conhecidos de strapping.

RAM estática 96016 B (menos 8 B), flash 1413097 B (menos 516 B frente à 0.4.6).
Sem novo buffer ou alteração de cadência/animação. O benchmark de 0.4.6 permanece
histórico; não houve nova medição de FPS/CPU nesta remoção.

**Bancada:** NGK ECU normal 0.4.6 identificado antes da gravação. Instalado normal
0.4.7 somente no aplicativo em 0x10000, hash verificado pelo esptool. Imagem
1419680 B, SHA-256 `6023cc505923448e3a550709a3fb36eb534fe927d7e46a3e9ebd31398b01f3a6`. Sem formato SD, apagamento
de NVS/calibração ou mudança de pinos/permissões. Captura real de pixels LCD
inspecionada: 153600 B, CRC32 `eada1b33`, 13.36 s; fonte
sintética identificada. Simulador Linux restaurado após a gravação/captura.
Entradas e atuadores físicos seguem desabilitados; sem ensaio veicular.

## Fluidez, graduação e falhas simultâneas — 0.4.8

Cinco melhorias aprovadas: graduação estática do RPM; parada dos cálculos e
formatação dos instrumentos estáveis; duração adaptativa da animação; comparação
de regularidade com SD/rádios; indicação +N das outras falhas, preservando a
prioridade. TPS continua ausente. Fonte/modelo/protocolo Linux preservados.

**Sem hardware:** 16 executáveis nativos ASan/UBSan passaram. Novos contratos
cobrem consulta de movimento, tempos adaptativos/extremos float/uint32, final
exato/rollover/repetição, máscara/contagem e 10752 combinações de falhas/óleo/RPM,
stale/disabled/TTL, além do timestamp de aceitação sem alterar o formato SIM1.
Três builds e auditoria estática passaram: 14 sinais, zero erros, três avisos
conhecidos de strapping. Nenhum GPIO mudou.

Renderer real LVGL em host passou: 92 graduações nas 23 escalas, limites reais
dos glifos, C/F e km/h/mph, N/R, alertas, ausência de TPS, valor final antes do
repouso, amostras idênticas sem reiniciar trabalho, captura/TTL e igualdade entre
redesenho incremental e completo. Regressão de latência conferida: um redraw
antigo não pode contar uma amostra ainda aguardando cadência; só uma mudança
visível arma a métrica. Timestamp acompanha o último tile DMA imutável.
Revisões independentes de lógica/geometria e revisão final concluídas.

**Bancada USB, dados sintéticos:** fonte de 20 Hz, modo Suave, dois segundos
para estabilizar e dez segundos medidos por caso. Base 0.4.7 identificada antes
da gravação; mesmos quatro casos repetidos na imagem final 0.4.8. SD: remount
pontual com conclusão/status observados, sem exportar/importar perfis, formatar
ou simular gravação contínua. Rádio: busca Wi-Fi ou anúncio BLE, um por vez,
sem conexão/pareamento com terceiros. SIM e rádios foram restaurados após cada
caso. Uma recusa temporária de iniciar o benchmark com DMA pendente foi tratada
com repetição limitada e confirmação de fonte viva.

| Cenário | Apresentações 0.4.7 / 0.4.8, ~10 s | Rampa p95 superior / máximo 0.4.8 | Maior intervalo touch | Amostra→primeira mudança p95 / máximo |
| --- | --- | --- | --- | --- |
| Movimento, rádios off | 601 / 599 | 23 / 34.561 ms | 25.237 ms | 22 / 31 ms |
| Valores estáveis | 0 / 0 | — | 10.046 ms | — |
| Movimento + SD + busca Wi-Fi | 601 / 600 | 23 / 34.473 ms | 25.232 ms | 22 / 31 ms |
| Movimento + SD + anúncio BLE | 600 / 601 | 23 / 22.569 ms | 26.285 ms | 28 / 30 ms |

Valores estáveis na 0.4.8: **zero passagens de rampa, zero formatações de números
e zero passagens de detalhes**, com 9799 retornos em repouso e
consulta de toque ativa. Zero apresentações neste caso é o resultado esperado,
pois nenhum pixel mudou. Na base o texto já não era reenviado se igual, mas os
cálculos/formatação periódicos continuavam.

`ui_tick_total_us` estático: 569751 →
323398 µs acumulados em ~10 s. Tempo de parede dentro do tick,
não percentual real de CPU: inclui esperas/preempção. Os contadores comprovam
trabalho de apresentação eliminado; não certificam folga sob aquisição real.
Sem rádio, o tempo decorrido de tick em movimento foi
3955111 → 4190458 µs.
O ganho em repouso não demonstra economia global de CPU em movimento;
regularidade e custo devem continuar sendo medidos no uso real.
P95 de latência considera as 128 últimas primeiras mudanças visíveis dos
instrumentos; relógio local em ms, aceitação SIM→conclusão do último DMA.
Não mede chegada ao alvo, caminho completo do computador nem resposta óptica.
Média de apresentações parciais não equivale a telas completas/s ou ausência
universal de engasgos. Carga SD pontual e rádios de bancada não substituem veículo.

RAM estática normal **96712 B** (+696 B frente à 0.4.7), flash **1416029 B**
(+2932 B). Acréscimo principalmente de observação limitada/graduação; sem novo
framebuffer, fonte ou dependência. DMA 20 KiB e pool LVGL 32 KiB mantidos.
Imagem normal **1422608 B**, SHA-256 `6c8b4c327652e47f53f7a15e9e5ef6998a4dce217dc5987a0cdaee56c5f1052d`.
Gravação somente app 0x10000 com hash verificado; NVS/calibração/partições e
permissões preservadas. Entradas e atuadores físicos seguem desabilitados.

Capturas reais dos pixels enviados ao LCD, conferidas e inspecionadas:
normal CRC32 `bc04c979`, 13.363 s; múltiplas
falhas CRC32 `b4c8e2d4`, 13.363 s; 153600 B cada.
Simulador Linux restaurado após os ensaios. Sem teste veicular/óptico.
