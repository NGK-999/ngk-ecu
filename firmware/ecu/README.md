# NGK ECU — firmware de bancada

Aplicação própria para o projeto do Fusca 1300 carburado. A referência de
interface fornece somente cores, tipografia e padrões de interação. O domínio
da aplicação contém instrumentos do veículo, sem telas de produção industrial.

## Estado dos instrumentos

RPM, temperatura do bloco, velocidade e estado do óleo começam como
`-- / SEM SINAL`. Ainda não existem circuitos/sensores ligados e validados.
Nenhum GPIO de aquisição do motor ou saída de injeção/ignição é habilitado.

Menu inicial com Painel em destaque e botões Sensores, Ajustes e Sistema. O modo Painel
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
hardware; execute `sh scripts/test-native.sh` (12 executáveis C++17 com avisos e
ASan/UBSan). As fontes têm 12 testes adicionais: `.venv/bin/python -m unittest discover -s tests/font`.

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
Não há comando serial para informar senha ou aceitar pareamento.

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
