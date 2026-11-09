# Skill ESP32: instalação e auditoria local

Fonte solicitada: [ezrover/ESP32-AI-Agent-Skill](https://github.com/ezrover/ESP32-AI-Agent-Skill).
Auditoria de 2026-11-09, fixada no commit
[`124d298e4ccaa737dbf6d26bbae47722e17f3097`](https://github.com/ezrover/ESP32-AI-Agent-Skill/tree/124d298e4ccaa737dbf6d26bbae47722e17f3097).
Licença MIT, copyright (c) 2026 Vibe Coder; texto integral preservado em
[LICENSE](../.agents/skills/esp32/LICENSE).

## Instalação restrita ao projeto

O helper oficial `skill-installer` foi executado com estes argumentos:

```text
--repo ezrover/ESP32-AI-Agent-Skill
--ref 124d298e4ccaa737dbf6d26bbae47722e17f3097
--path skills/esp32
--name esp32
--dest <raiz-do-projeto>/.agents/skills
--method download
```

Destino: [.agents/skills/esp32/SKILL.md](../.agents/skills/esp32/SKILL.md).
Como o upstream distribui referências e scripts fora da pasta da skill, eles
também foram copiados para `.agents/skills/esp32/`, junto de `README.md` e
`LICENSE`. Os 64 arquivos upstream foram comparados byte a byte com o checkout
fixado; seu conteúdo não foi adaptado. A nota local
[UPSTREAM.md](../.agents/skills/esp32/UPSTREAM.md) explica o empacotamento.

O checkout original fica em `.tools/esp32-ai-agent-skill-124d298e`, ignorado
pelo Git. Não foi instalado o plugin Claude, nem alterado `$CODEX_HOME`,
configuração global, hooks, grupos, permissões ou drivers. O catálogo de skills
pode reconhecer a instalação no próximo turno; seu conteúdo já foi lido e usado
manualmente nesta auditoria.

## Compatibilidade com o NGK ECU

O pacote fornece orientação C/C++, Arduino, ESP-IDF e PlatformIO, com validador
para ESP32 clássico. Isso é compatível como apoio ao projeto atual: C++17,
Arduino no PlatformIO `espressif32@6.9.0`, LovyanGFX `1.2.7` e LVGL `8.4.0`.
As referências de LVGL 8.4 e do ESP32 clássico foram consultadas. Não foi
solicitada nem aplicada migração de framework, bibliotecas, GPIOs ou placa.

O modelo `WROOM` no JSON significa a classe de restrições do ESP32 clássico sem
PSRAM; não identifica uma nova revisão física da placa. `wifi_enabled=false`
corresponde ao cenário atual, sem uso de ADC ou de Wi-Fi neste mapa. BLE não
transforma os sinais digitais do touch em entradas ADC.

## Validação estática reproduzível

Executar na raiz do projeto, sem placa conectada ou acesso à serial:

```sh
sh scripts/validate-esp32.sh
```

O runner usa somente `.venv/bin/python` e o validador upstream auditado. Antes
da execução, confere os manifests
[esp32-audit-upstream.sha256](../tools/esp32-audit-upstream.sha256) e
[esp32-audit-board.sha256](../tools/esp32-audit-board.sha256). Uma alteração no
perfil, driver da placa, configuração PlatformIO ou JSON faz a execução recusar
o snapshot antigo. Revisar as diferenças e o JSON antes de atualizar hashes;
não regenerar o manifest apenas para silenciar a recusa.

Entrada: [esp32-audit-board.json](../tools/esp32-audit-board.json), snapshot dos
14 sinais existentes de `board_profile.hpp`:

| Subsistema | Implementação existente |
| --- | --- |
| LCD | SPI2; MISO desativado (`-1`, omitido do JSON); controle DC e backlight |
| Touch | XPT2046 por SPI em software, MISO e IRQ somente como entrada |
| SD | VSPI, separado da interface do touch e do LCD |

O inventário foi confrontado com os literais do C++, inclusive os quatro sinais
SD em `main.cpp`; não há GPIO de motor ou atuação nesse snapshot. A entrada
documenta a pinagem existente, não propõe ligações ou valida níveis elétricos.

Resultado observado:

| Checagem | Evidência |
| --- | --- |
| Pacote instalado | 64 arquivos idênticos ao commit upstream, incluindo licença |
| Mapa atual | `valid=true`, 14 sinais, zero erros, três avisos |
| Avisos preservados | `STRAPPING_PIN`: GPIO15/LCD CS, GPIO2/LCD DC e GPIO5/SD CS |
| Coerência do snapshot | 14 papéis coincidem com o perfil C++; GPIOs distintos; SD coincide com `main.cpp` |
| Controles negativos em memória | Saída no GPIO39 rejeitada com código 1; estrutura JSON inválida rejeitada com código 2 |

Os controles negativos não alteraram os arquivos de firmware nem a entrada
publicada. O validador também informou `current_draw_ma=55.0`; esse número é
uma heurística de 5 mA por saída, não medição de consumo da placa, orçamento de
alimentação, corrente do backlight ou prova de segurança elétrica.

## Limites e itens que não serão aplicados automaticamente

1. **Não há bloqueio automático da IA.** O pacote contém instruções e scripts
   chamados explicitamente; não intercepta comandos, edições, flash ou serial.
   O runner local verifica somente seus arquivos e o snapshot informado.
2. **O validador não lê C++ nem o esquema.** Não modela host SPI, SPI em software,
   mutexes, DMA, temporização, frequência admissível do painel, tensão dos sinais,
   circuitos de boot ou periféricos omitidos da entrada. O campo `device` não
   constitui isolamento de barramento. Os três avisos continuam relevantes ao boot.
3. **O gerador Arduino não serve diretamente à arquitetura atual.** A função
   `_build_arduino_spi_init` reúne sinais SPI em um único `SPI.begin`; não preserva
   LCD/SPI2, touch em software e SD/VSPI como três interfaces distintas. O gerador
   foi lido, mas não executado nem aplicado.
4. **As recomendações gerais exigem conferência.** Pinos convencionais dos
   exemplos, limites elétricos heurísticos e sugestões de ESP-IDF 5 não substituem
   o perfil existente e a documentação da versão usada. Não habilitar GPIO12,
   ADC, Wi-Fi, PSRAM ou novas saídas por sugestão da skill. Não aplicar exemplos
   de eFuse, reset ou gravação durante uma auditoria estática.
5. **Instalação não é validação física.** Não houve abertura de dispositivo,
   leitura de flash/NVS, compilação, upload, formatação SD, alteração de hardware
   ou teste no veículo nesta tarefa. O funcionamento físico já observado pelo
   responsável permanece uma evidência separada desta análise estática.

Referência primária para restrições do ESP32:
[GPIO e strapping — Espressif](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/peripherals/gpio.html).
