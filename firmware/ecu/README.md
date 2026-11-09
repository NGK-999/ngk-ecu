# NGK ECU — firmware de bancada

Aplicação própria para o projeto do Fusca 1300 carburado. A referência de
interface fornece somente cores, tipografia e padrões de interação. O domínio
da aplicação contém instrumentos do veículo, sem telas de produção industrial.

## Estado dos instrumentos

RPM, temperatura do bloco, velocidade e estado do óleo começam como
`-- / SEM SINAL`. Ainda não existem circuitos/sensores ligados e validados.
Nenhum GPIO de aquisição do motor ou saída de injeção/ignição é habilitado.

Menu inicial 2×2 com Painel, Sensores, Ajustes e Sistema. O modo Painel
dedica a tela aos instrumentos grandes; duplo toque retorna ao menu.
As páginas secundárias têm botão Menu. A primeira inicialização
pede calibração de cinco pontos. Toque e solte cada alvo; o ajuste é salvo em
NVS próprio `ngk-touch`. Ajustes permite repetir a calibração. Wi-Fi e Bluetooth BLE começam desligados;
a ativação é explícita nas categorias de ajustes.

## Ferramentas e compilação

Arduino-ESP32 2.0.17, C++17, LVGL 8.4.0, LovyanGFX 1.2.7,
NimBLE-Arduino 2.5.1 e PlatformIO 6.1.18. Use a `.venv` Python 3.12 e cache isolado deste projeto:

```sh
PLATFORMIO_CORE_DIR="$PWD/.tools/platformio" .venv/bin/python -m platformio run \
  --project-dir firmware/ecu -e ecu-normal -e ecu-maintenance
```

Compilar não abre porta USB nem grava a placa. Os testes nativos não usam
hardware; execute `sh scripts/test-native.sh` (16 executáveis C++17 com avisos e
ASan/UBSan). As fontes têm 13 testes adicionais: `.venv/bin/python -m unittest discover -s tests/font`.

Os ícones têm dois testes adicionais: `.venv/bin/python -m unittest discover -s tests/icons -v`.
Reprodução offline: `.venv/bin/python tools/icons/generate.py --check`.
Eles usam as ferramentas Qt já fixadas no host; a placa recebe somente máscaras
alpha4 constantes, sem runtime SVG/Qt.

## Ajustes e conexões

Índice com seis categorias: Tela, Instrumentos, Toque/DEMO, Perfis SD, Wi-Fi
e Bluetooth. Instrumentos tem abas Unidades e Escala RPM. Brilho e cadência têm prévia imediata; Salvar ajustes grava em NVS próprio.
Outras alterações permanecem em rascunho até Salvar. Importação do SD substitui
ajustes/rascunho após confirmação. Faixas de RPM são visuais, sem corte de giro.
DEMO abre diretamente o painel, é explicitamente sintético e não persiste
após reiniciar. Rádios/SD não exibem o botão Salvar dos parâmetros visuais.

Wi-Fi usa modo estação, busca assíncrona e até seis redes no snapshot.
SSID manual e senha ASCII são digitados na própria tela; a senha aparece
mascarada e nunca é enviada à serial ou ao SD. O driver Arduino pode alocar
resultados temporários de todos os APs durante a busca. Uma conexão explícita
bem-sucedida salva o perfil em NVS privado. Não há conexão automática no boot.

Bluetooth é BLE, não Bluetooth clássico/SPP. Ativar inicializa o periférico;
Descobrir abre uma janela de 120 s. Pareamento exige Secure Connections,
MITM e comparação numérica confirmada na ECU em até 30 s. O serviço NUS
aceita somente `PING` e `INFO` de conexão autenticada/criptografada. Não há
comando de formato, mapas ou atuação pelo rádio. O cliente precisa suportar
esse tipo de pareamento. Apagar vínculos exige confirmação local.

Os rádios têm limites de memória e falhas visíveis. A bancada recusou iniciar o segundo rádio por memória insuficiente: usar
um por vez nesta unidade. Busca Wi-Fi e anúncio BLE separados foram medidos;
guardas de heap não garantem toda alocação futura.
Manutenção não inicializa rádios.

## Protocolo de diagnóstico

A serial opera a 115200 baud. `info` seguido de LF fornece um objeto JSON
com versão, modo de bancada, estado dos instrumentos desabilitados e SD.
Não imprime MAC, CID, SSID, senhas, códigos de pareamento, arquivos pessoais
ou amostras de sensores. `radios`/`status` mostram somente flags e memória.
O parser rejeita NUL, controles, CR interno e linhas maiores que 95 bytes;
CRLF final é aceito. Metadados do SD vêm de um snapshot único do worker.

Comandos locais adicionais: `wifi-on`, `wifi-off`, `wifi-scan`, `ble-on`,
`ble-off`, `ble-discover`, `profile-export`, `profile-import` e `sd-refresh`.
São pedidos assíncronos; a resposta QUEUED não significa conclusão.
`info` inclui revisão/estado de perfis. `bench` mede a cena sintética de 20
redesenhos completos +10 s parciais e retorna ao menu; não envia leituras reais.
`motion-bench` observa por 10 s as apresentações do caminho SIM ativo no
Painel, sem desligar o touch. Nova sessão, perda de link, troca de página ou
modal interrompem a captura. É uma observação de software/DMA, sem amostras
persistidas, não um teste óptico ou veicular. `ui-smooth` solicita e salva modo
Suave com movimento pelo serviço normal de perfis; preserva os outros campos.
`info` inclui `refresh_mode` e `reduced_motion`. Nenhum desses comandos habilita
sensores ou atuadores. Não há comando serial para informar senha ou aceitar pareamento.

## Simulação USB — protocolo 1, normal 0.4.5

O appLinux está em [simulator/](../../simulator/README.md). Nenhuma porta é
aberta ao iniciar o app; Conectar identifica o projeto e simulation_protocol1.
Manutenção anuncia protocolo 0 e não aceita comandos de simulação.

`sim-start TOKEN` abre sessão com token1..UINT32MAX definido pelo cliente antes
da escrita; isso permite encerrar mesmo se cancelar antes da resposta ready.
A variante `sim-start` sem argumento gera token no painel para diagnóstico.
Ready é JSON kind sim,state ready,protocol1 e eco de session. Não é uma
credencial de controle do motor; não existem comandos de atuação.

Frame ASCII com LF,até 95bytes:

```text
SIM1 TOKEN SEQ RPM SPEED10 TEMP10 TPS10 GEAR OIL FAULTS
```

RPM0..12000; SPEED10=km/h×10(0..2500); TEMP10=C×10(-400..2000);
TPS10=percentual×10(0..1000); GEAR-1(R),0(N),1..4; OIL0normal/1baixo/2ausente.
FAULTS bits1/2/4/8/16/32/64/128 representam ausênciaRPM/velocidade, bloco aberto/
curto, óleo baixo, superaquecimento, TPS/marcha inválidos. Aberto+curto são
mutuamente exclusivos. Tokens/seq sãouint32nãozero; sequência estritamente
crescente sem wrap na sessão. Nova sessão reinicia sequência.

`sim-status` retorna somente metadados live/seq/faults; nenhuma amostra.
`sim-stop TOKEN` encerra apenas a sessão correspondente. Parser rejeita
overflow, campos inválidos/extra/garbage e replay sem atualizar validade.

TTL 750 ms contado da última amostra válida, com expiração observada fixada
até novo frame ou nova sessão. Não revive frame antigo após voltar millis.
Stale mostraSIM SEM LINK, remove valores/alertas antigos e mantém saída local
emAjustes/Toque-DEMO/EncerrarSIM USB. Stop/reboot limpa a sessão; nenhum frame
é persistido. Benchmark eDEMO recusados durante sessãoUSB.

SIM USB mostra marcha/TPS e sinaliza texto/cor por validade. Ausência vira--,
zero permanece valor. Os instrumentos arredondam velocidade/bloco para inteiros;
os dados do protocolo mantêm décimos. USB é fonte sintética prioritária, sem
misturar valores deDEMO ou sinais físicos.

## Captura explícita da tela — 0.4.4

`screen-capture` espelha os pixels enviados ao LCD e transmite RGB565_BE com
contagem/CRC. Usa somente os tiles DMA existentes; não aloca uma tela completa.
É diagnóstico de bancada: o quadro fica congelado durante a transferência,
com timeout. Páginas/estados sensíveis são recusados antes do header.
`info.screen_capture_protocol` é1 no normal e0 na manutenção.
Cliente e instruções em [captura-tela](../../docs/captura-tela.md).

## SD e manutenção

O worker possui exclusivamente o SPI3 do cartão. Consultas de geometria e
montagem não listam arquivos do usuário. Perfis leem/escrevem somente nomes
próprios em `/ngk-ecu`. Consulte [armazenamento e desempenho](../../docs/armazenamento-e-desempenho.md).
No normal, cartão montado não é remontado a cada 5 s; use atualização explícita
para troca de cartão. Falha/ausência de montagem tenta novamente a cada 5 s. Falha de montagem não
formata automaticamente. CID é consultado somente na manutenção e seu
fingerprint permanece apenas em RAM, fora da serial, de arquivos e de logs.

`ecu-normal` não contém o comando próprio de formatação. A biblioteca SD do
SDK ainda inclui sua implementação de FatFS; ambas as chamadas de montagem
passam `format_if_empty=false`. Não afirmar ausência física de `f_mkfs` no ELF.

`ecu-maintenance` é separado. Uma operação autorizada exige o comando exato
`FORMAT_SD <setores> <token>`, com os valores da sessão identificada. O worker
reconfere CID e geometria antes de consumir uma autorização única por boot.
Retirada/troca observada revoga a sessão. Falha não dispara retry automático.
O formato usa FAT16 até 2 GiB e FAT32 acima disso, até o limite de 32 GiB.
Após sucesso, verifica somente um arquivo próprio temporário e o remove.
Reinstalar `ecu-normal` depois da manutenção.

Não retirar cartão nem reconectar a placa durante uma operação. A checagem
de CID não é autenticação criptográfica de cartão e não pode impedir uma
troca física exatamente depois da última consulta. Divergência entre o cartão
informado e o observado bloqueia a operação, mesmo que exista autorização geral.

## Recuperação e limites

Existe backup local privado da flash anterior, verificado antes da primeira
gravação. Ele não integra o Git e pode conter dados que não devem ser publicados.
Não usar comandos genéricos de apagar toda a flash para trocar esta aplicação.

Builds e testes nativos não validam sol, vibração, ruído elétrico, latência
óptica, sinal de bobina ou funcionamento no motor. Esses ensaios pertencem a
etapas futuras, após definir sensores, condicionamento e alimentação.
