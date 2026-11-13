# Correções da auditoria — 0.4.15-audit

Correções solicitadas após a [auditoria da 0.4.14](auditoria-codigo-2026-11-12.md),
na branch `preview/painel-disposicao`, base
`40626184d754dd9b4e12a37ed68a74d3690ca8e0` mais trabalho local preservado.

## Alterações

| Achado | Correção |
| --- | --- |
| A1 — buffer Wi-Fi liberado durante callback | O callback Arduino publica conclusão por atomic release/acquire. Só então o worker copia/descarta e libera resultados. Cancelamento/timeout mantêm a reserva; nova busca/conexão fica bloqueada até cleanup. |
| A2 — unidades após importação assíncrona | `applyPreview()` sincroniza as duas legendas com os valores efetivos, sem reconstruir o painel nem remover overlays. |
| A3 — pixels antigos na escala RPM | Mudanças nas legendas de graduação invalidam explicitamente a rampa; perfil idêntico não provoca redraw em repouso. |
| A4 — prévia imperial enganosa | A prévia usa ações reais dos ajustes e verifica números, mph/°F, importação assíncrona, pixels da escala e preservação de objetos. |
| A5 — snapshot de pinagem vencido | Os 14 sinais foram confrontados com perfil/barramentos e o hash de `main.cpp` foi renovado; a trava de hashes continua ativa. |
| A6 — retry de comando SD inválido bloqueado | A pendência usa ID e confirmação do worker; outra tentativa só entra após enfileirar o resultado anterior. A política continua consumindo autorização antes de uma operação aceita, inclusive se o formatter falhar. |

O início da busca usa `esp_wifi_scan_start` com a configuração anterior
(ativa, 100–200 ms por canal, todos os canais, redes ocultas excluídas).
O modo STA continua sendo habilitado pelo worker. O processamento dos resultados
permanece no Arduino 2.0.17, seguido pelo callback da aplicação. Isso evita a
janela do wrapper `scanNetworks`, que escreve seus bits de estado depois de
iniciar o driver. O timeout de 4.000 ms mantém o limite efetivo anterior do
Arduino (`200 * 20`); ele solicita cancelamento, sem autorizar liberação precoce.

Os clientes dos diagnósticos aceitam explicitamente as versões 0.4.12 e 0.4.15
de sua respectiva variante. Protocolo, modo, flags de segurança e demais
pré-condições continuam sendo verificados.

## Evidências e limites

Validação final local: seis ambientes PlatformIO aprovados (`ecu-normal`,
`ecu-render-reference`, `ecu-load-benchmark`, `ecu-sd-benchmark`,
`ecu-maintenance` e `ecu-benchmark`); 24 executáveis nativos ASan/UBSan,
107 testes do simulador, 13 de setup, 15 de fontes, dois de ícones, seis do
codec de captura e três do driver de prévia passaram. Onze cenas e as
regressões adicionais de UI passaram; contato e imagem imperial inspecionados.
O validador canônico conferiu 14 sinais, zero erros e os três avisos já
conhecidos de strapping (GPIO 15, 2 e 5). REA inspecionou as seções do ELF:
exit 0, complete, artefato inalterado; isso não demonstra ausência de bugs.

Artefato normal: RAM estática 96.848 B, flash do linker 1.418.809 B,
binário 1.425.392 B, SHA-256
`869078a1e6ae92ea41139754e767d9bf797a933be087238318938e7ce5fb4bf6`.
RAM estática igual à 0.4.14, com acréscimo de 848 B no aplicativo. O novo
callback pode ter custo de heap; esse custo não foi medido na placa nesta fase.

**Normal 0.4.15-audit instalado e verificado em bancada.** Após o operador
liberar o acesso temporário à serial, a placa foi identificada como 0.4.14,
com SD/perfil prontos e fontes desligadas. O backup privado de 1.966.080 B do
app anterior conferiu com o hash conhecido; a tabela coincidiu com o build.
Foram executados `write-flash` e `verify-flash` somente no app `0x10000`.
A tabela de 4.096 B foi lida novamente e permaneceu byte a byte idêntica.

A captura após o boot, sem comandos de navegação/SIM, mostrou o painel direto
com `-- / SEM SINAL`: 153.600 B, CRC32 `eaa0e4ed`, imagem inspecionada.
Não houve nova medição de alimentação→imagem, boot óptico ou touch.

Bancada Wi-Fi: cinco buscas foram observadas em andamento e canceladas por
desligamento; duas buscas posteriores completaram. O reinício após cancelamentos
foi confirmado, sem travamento nessas rodadas. Esperas observadas pelo condutor:
96–99 ms para desligar após cancelamento; 2.468–2.539 ms para conclusão das
buscas, desde a observação inicial. Incluem polling/serial/scheduler; não são
tempos internos do callback. Sem associação a rede, credenciais ou logs de SSID.
O teste físico não força a ordem interna de execução controlada no teste host.

Bancada USB100: produtor em 99,995 Hz, 1.607 frames, zero slots pulados pelo
produtor; sessão viva em 12 consultas. A janela UI de 10 s teve 600 apresentações,
p95 de intervalo 24,675 ms e tick máximo 13,781 ms. A pausa de 900 ms confirmou
invalidação pelo TTL. São dados sintéticos; contagem do produtor não comprova
recepção integral, e apresentações são transferências parciais DMA, não FPS óptico.

Encerramento observado: SIM e rádios desligados, SD montado, perfil persistido,
sensores/atuadores false e portas fechadas. Nenhum comando de gravação de perfil,
calibração/NVS/tabela ou formatação SD. Importação física SD, manutenção destrutiva,
sensores reais e veículo continuam sem novo ensaio.

Regressões anteriores foram observadas antes das correções: ASan confirmou o
uso de memória já liberada na reprodução do SDK; a UI real mostrou legendas
métricas com números imperiais e graduações obsoletas; a trava de submissão
impediu retry após rejeição. Os novos testes usam transporte, tempo e drivers
sintéticos. A revisão também exercitou saída textual cheia: o resultado não
pode ser descartado nem confundido com snapshot de outra operação ativa.

A documentação e os MCPs Espressif Engineering/Documentation e LVGL foram
confrontados com as fontes locais Arduino 2.0.17, ESP-IDF 4.4.7 e LVGL 8.4.
O [guia Wi-Fi 4.4.7](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-guides/wifi.html#wi-fi-event-description)
documenta eventos de conclusão também no cancelamento; as garantias de ordem
usadas aqui foram verificadas em `WiFiGeneric.cpp`/`WiFiScan.cpp` locais.

Se o driver deixar de entregar o evento de conclusão, o worker mantém a busca
reservada e o desligamento pendente: não libera nem inicia outra busca às cegas.
Esse caso exige diagnóstico/reinício; não há promessa de recuperação automática.
A reprodução host não mede a frequência de nenhuma janela na placa.

Arduino/IDF/LVGL, bootloader, partições, perfil de GPIO, três barramentos e
política NVS permanecem nas versões/configurações anteriores. A observação
condicional sobre autoerase NVS do SDK segue documentada na auditoria; não foi
transformada em migração ou recuperação automática nesta etapa.

Resultados finais, identidade do artefato e estado da instalação constam em
[agregados revisados](measurements/audit-fixes-2026-11-12.json).
Logs, executáveis de reprodução, backups e capturas de bancada ficam somente
em diretórios locais ignorados. Sem commit/push nesta etapa.
