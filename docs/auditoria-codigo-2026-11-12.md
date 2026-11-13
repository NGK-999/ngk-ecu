# Auditoria do código — 0.4.14-fastboot

Os achados abaixo registram o estado anterior às correções solicitadas depois.
Consulte as [correções da 0.4.15 e sua validação](correcoes-auditoria-0415.md)
para o estado posterior.

Auditoria de 2026-11-12 sobre o checkout local baseado em
`40626184d754dd9b4e12a37ed68a74d3690ca8e0`, incluindo as alterações ainda não
publicadas da 0.4.13/0.4.14. Foram preservados os arquivos existentes. Nenhuma
fonte de produção foi alterada nesta auditoria; não houve serial física, reset,
gravação, leitura de NVS ou formatação de SD.

**Conclusão:** há duas correções prioritárias de funcionamento, três ajustes de
interface/validação e uma inconsistência restrita à manutenção. Os testes
canônicos passaram, mas não cobriam os cenários adicionais reproduzidos abaixo.
Não foi identificado problema P0 no escopo inspecionado; isso não certifica
ausência de outros defeitos ou prontidão para uso no veículo.

## Correções prioritárias

### P1 — Cancelamento de busca Wi-Fi pode liberar memória ainda em uso

- **Local:** `firmware/ecu/src/radio.cpp:219–223`.
- **Cenário:** desligar Wi-Fi ou cancelar uma busca enquanto o task de eventos
  Arduino termina `SCAN_DONE`.
- **Causa:** `stopScan()` chama `esp_wifi_scan_stop()` e imediatamente
  `WiFi.scanDelete()`. No Arduino-ESP32 2.0.17, `WiFiScan.cpp:109–120` aloca e
  preenche `_scanResult` no callback; `:163–170` libera o mesmo ponteiro sem
  sincronização com esse preenchimento. O worker do projeto roda no core 0;
  a configuração do task de eventos Arduino usa core 1.
- **Evidência:** reprodução host com os dois métodos do SDK copiados literalmente
  e um driver sintético com pausa controlada entre receber o ponteiro e copiá-lo.
  AddressSanitizer identificou `heap-use-after-free`; a reprodução foi compilada
  e executada novamente pelo coordenador. Não é prova da frequência dessa janela
  no driver físico, nem de um incidente já ocorrido na placa.
- **Correção recomendada:** coordenar cancelamento e conclusão antes de liberar
  os resultados; um mutex usado somente pelo worker não protege o callback da
  biblioteca. Acrescentar regressão para conclusão concorrente ao cancelamento.

O guia oficial do [ESP-IDF 4.4.7](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-guides/wifi.html#wi-fi-event-description)
confirma que parar um scan pode gerar `WIFI_EVENT_SCAN_DONE`; não se deve tratar
o retorno de `scan_stop` como confirmação de que o processamento Arduino desse
evento terminou. A localização exata da falha vem das fontes locais 2.0.17 e
da reprodução, não de documentação de uma versão mais recente.

### P2 — Importação assíncrona pode deixar unidades erradas no painel

- **Local:** `firmware/ecu/src/ui.cpp:1188–1216`.
- **Cenário:** iniciar importação de perfil imperial, voltar ao menu e abrir o
  painel antes de a UI consumir a conclusão da importação.
- **Causa:** as configurações e os números são atualizados, mas as legendas só
  são reconstruídas se `firstProfile` for verdadeiro. A importação posterior
  ocorre com essa flag falsa.
- **Evidência:** UI de produção em LVGL 8.4 real, com serviço assíncrono em
  memória. Uma amostra de 100 km/h e 100 °C apareceu como **62 km/h e 212 °C**;
  os números já estavam em mph/°F e as legendas continuavam métricas.
- **Correção recomendada:** sincronizar as legendas sempre que as unidades
  efetivamente mudarem, preservando as guardas de calibração/modal e a ausência
  de reconstrução quando nada mudou. Incluir regressão do fluxo completo.

## Interface e validação

| Prioridade | Local | Evidência | Correção recomendada |
| --- | --- | --- | --- |
| P3 | `ui.cpp:481–505`, `:1216` | Perfil tardio de 10.000 RPM atualiza `MAX 10000` e as legendas em RAM, mas a rampa ainda mostra 2000/4000/6000 da escala anterior. Uma invalidação explícita muda 380 pixels para 2500/5000/7500. | Invalidar a rampa quando a escala efetivamente mudar, mesmo sem novos dados SIM. |
| P3 | `tools/ui_preview/hooks.cpp.inc:18–22`; `harness.cpp:109–112` | A cena imperial altera valores, mas mantém km/h/°C. O teste confere somente os números e passa. | Atualizar a prévia pelo fluxo correto e verificar também as legendas. |
| P3 | `tools/esp32-audit-board.sha256:4`; `scripts/validate-esp32.sh:12–14` | O comando canônico termina com código 2 porque o hash de `main.cpp` é antigo. Os demais quatro hashes conferem. | Após revisar os sinais do código atual, renovar o snapshot auditado; não remover a trava. |

A execução direta do validador de pinagem sobre o JSON revisado retornou 14
sinais, zero erros e os três avisos conhecidos de strapping: GPIO 15, 2 e 5.
O perfil C++ e os sinais SD de `main.cpp` permanecem coerentes com esse JSON.
Essa execução isolada não substitui a restauração do comando canônico nem
confirma revisão de PCB/esquema ou segurança elétrica no veículo.

## Manutenção e dependências

**P3, restrito a `ecu-maintenance`:** `main.cpp:535–540` fixa `submitted=true`
quando a fila aceita qualquer linha com prefixo `FORMAT_SD `. Se a política
depois rejeitar token/conteúdo inválido, esse latch não é liberado e a linha
correta também será recusada até um novo boot. Isso contradiz o contrato de
`MaintenancePolicy::request`, segundo o qual entrada inválida não consome a
autorização. Achado por inspeção do encadeamento; não foi executado formatter.
Corrigir o estado de submissão sem permitir repetição de um formato já aceito.
Esse caminho não existe no aplicativo normal instalado.

**Observação condicional de dependência, sem incidente reproduzido:** o
`initArduino()` do framework 2.0.17, em `esp32-hal-misc.c:249–255`, apaga a
partição NVS padrão quando recebe `ESP_ERR_NVS_NO_FREE_PAGES` ou
`ESP_ERR_NVS_NEW_VERSION_FOUND`. O caminho de chamada também está presente no
ELF. Essa partição contém configurações e calibração do projeto. Não há evidência
de que esses erros tenham ocorrido nesta placa; não é motivo para apagar NVS,
alterar SDK ou migrar partições durante a auditoria. Recomenda-se definir uma
política explícita de recuperação antes de tratar a preservação como garantida
em todas as falhas de inicialização.

## Verificações executadas

| Verificação | Resultado |
| --- | --- |
| 22 executáveis nativos ASan/UBSan | Passaram |
| 102 testes do simulador, Qt offscreen/transporte em memória | Passaram |
| 13 testes de setup em pastas temporárias | Passaram |
| 15 testes de fontes, 2 de ícones e 6 de captura | Passaram, sem skips |
| 3 testes do driver de prévia | Passaram |
| Seis ambientes PlatformIO declarados, incluindo `ecu-benchmark` | Compilaram |
| Validação canônica de pinagem | Falhou no snapshot desatualizado, código 2 |
| Validação direta após confronto com o código | Zero erros; três avisos conhecidos |
| REA 6.8.0, seções do ELF normal | Exit 0, resultado completo, artefato inalterado |
| Reproduções adicionais UI/unidades, escala e prévia | Defeitos confirmados |
| Reprodução concorrente Wi-Fi sob AddressSanitizer | Erro de memória confirmado no interleaving sintético |
| 630 navegações do harness privado com `lv_mem_test` | Passaram; não demonstram ausência universal de vazamento |

No transporte, não foi confirmado novo defeito P1/P2: parser, orçamento RX,
reservas TX, ordem de captura, sessões, sequências, TTL e codec/modelo/ciclo
Python foram confrontados com os contratos e testes. A expansão conservadora
da resposta `info` ficou em 989 B, dentro da reserva de 1024 B. Isso não elimina
a necessidade de retestes físicos quando esse código mudar.

## Ferramentas, proveniência e limites

- Espressif Engineering: catálogo e workflow `storage/esp-filesystem`, com
  referências de verificação por versão e preservação NVS.
- Espressif Documentation: semântica de scan/eventos; os resultados `latest`
  foram confrontados com o guia 4.4.7 e os headers/fontes locais.
- LVGL Documentation: contratos de buffer estático e conclusão de DMA, depois
  confrontados com LVGL 8.4 local. Não foram copiadas APIs de versões novas.
- REA: inspeção estrutural do ELF, sem executar firmware nem acessar placa.
- Revisão independente por domínio e reproduções host privadas; os 59 arquivos
  de produção acompanhados por SHA-256 permaneceram byte a byte iguais.

Registros completos e fontes das reproduções ficam em `local/audit-20261112/`,
ignorados pelo Git. Este relatório e os agregados não incluem serial bruto,
credenciais, dumps NVS ou identificadores de hardware. Nenhum teste abriu porta
física. Testes host e análise de ELF não validam funcionamento no veículo,
interferência elétrica ou frequência de uma corrida em hardware.

**Preflight:** builds e testes canônicos de software passaram; pinagem canônica
permanece bloqueada pelo snapshot. Falhas de UI e o interleaving Wi-Fi foram
reproduzidos; incidência física e erros NVS continuam não observados. USB,
persistência física, corte de alimentação e veículo não foram ensaiados.
Firmware instalado permanece 0.4.14-fastboot, sem correções desta auditoria.

[Agregados sanitizados](measurements/code-audit-2026-11-12.json).
