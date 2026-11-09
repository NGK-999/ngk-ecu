# NGK ECU — firmware de bancada

Aplicação própria para o projeto do Fusca 1300 carburado. A referência de
interface fornece somente cores, tipografia e padrões de interação. O domínio
da aplicação contém instrumentos do veículo, sem telas de produção industrial.

## Estado dos instrumentos

RPM, temperatura do bloco, velocidade e estado do óleo começam como
`-- / SEM SINAL`. Ainda não existem circuitos/sensores ligados e validados.
Nenhum GPIO de aquisição do motor ou saída de injeção/ignição é habilitado.

Menu inicial com botões Painel, Sensores, Ajustes e Sistema. O modo Painel
dedica a tela aos instrumentos grandes; duplo toque retorna ao menu.
As páginas secundárias têm botão Menu. A primeira inicialização
pede calibração de cinco pontos. Toque e solte cada alvo; o ajuste é salvo em
NVS próprio `ngk-touch`. Ajustes permite repetir a calibração. O firmware não
ativa Wi-Fi ou Bluetooth.

## Ferramentas e compilação

Arduino-ESP32 2.0.17, C++17, LVGL 8.4.0, LovyanGFX 1.2.7,
PlatformIO 6.1.18. Use a `.venv` Python 3.12 e cache isolado deste projeto:

```sh
PLATFORMIO_CORE_DIR="$PWD/.tools/platformio" .venv/bin/python -m platformio run \
  --project-dir firmware/ecu -e ecu-normal -e ecu-maintenance
```

Compilar não abre porta USB nem grava a placa. Os testes nativos não usam
hardware; execute os quatro arquivos de `tests/native` com C++17, avisos
habilitados e ASan/UBSan conforme `docs/validacao.md`.

## Protocolo de diagnóstico

A serial opera a 115200 baud. `info` seguido de LF fornece um objeto JSON
com versão, modo de bancada, estado dos instrumentos desabilitados e SD.
Não imprime MAC, CID, arquivos pessoais ou amostras de sensores.
O parser rejeita NUL, controles, CR interno e linhas maiores que 95 bytes;
CRLF final é aceito. Metadados do SD vêm de um snapshot único do worker.

## SD e manutenção

O worker possui exclusivamente o SPI3 do cartão. Consultas de geometria e
montagem não listam nem leem arquivos do usuário. Falha de montagem não
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
