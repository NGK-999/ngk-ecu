# Bateria de carga sintética — plano de execução

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Medir atrasos, perdas por substituição e margem de memória durante carga sintética concorrente na ESP32, sem sensores físicos.

**Architecture:** Um build temporário `ecu-load-benchmark`, isolado por `NGK_LOAD_BENCH`, adiciona um produtor determinístico e mailbox de um elemento. O painel continua recebendo SIM USB; a carga não escreve GPIOs nem valores persistentes. A bateria compara controle/carga no mesmo binário e restaura o aplicativo normal original por hash.

**Tech Stack:** Arduino-ESP32 2.0.17, ESP32 Xtensa 240 MHz, C++17, LVGL 8.4, PlatformIO local, Python 3.12 `.venv`, REA 6.8.0 local.

**Spec:** Pedido explícito de executar a bateria proposta: carga concorrente, memória/pilhas e atrasos, usando REA. Este documento delimita a implementação do diagnóstico e seus critérios.

## Global Constraints

- Projeto NGK ECU, base `f4fcfc2cebf55ec0b2a9431b1af804a31e66cad6`, branch `preview/painel-disposicao`; InterfaceNeural não será alterado.
- Nenhum sensor, atuador, GPIO, NVS, calibração, partição, SDK global ou formato do SD será alterado.
- Testes nativos/Qt usam somente memória; bancada física é execução explícita separada, exclusivamente pelo agente principal.
- Guardar apenas agregados/metadados permitidos. Sem amostras, serial bruto, SSIDs, credenciais ou identificadores de hardware.
- CPU por tarefa não está habilitada no SDK; tempo de parede, jitter e contadores não serão apresentados como percentual de CPU livre.
- REA inspeciona ELF/seções; não fornece decompilação Xtensa nesta versão. Complementar com ferramentas Xtensa locais, sem executar setup global.

## Review Focus

- Entrada inválida/busy: recusar requisição sem sobrescrever execução corrente.
- Prazo excedido: pular períodos vencidos, sem rajada para recuperar backlog.
- Mailbox sobrescrito: contar lacunas/sequências e consumo pendente corretamente.
- Timeout/interrupção tratada do host: carga limitada, cleanup de SIM/rádios; morte forçada/desconexão não garante restauração dos rádios.
- Distinção entre carga ilustrativa e sensores: não converter canais sintéticos em promessa de capacidade de drivers/ADC/ignição.

## Contrato do diagnóstico

- `info`: build `load-benchmark`, firmware `0.4.11-load-bench`, `load_benchmark_protocol:1`, flags físicas false.
- `load-start <id> <mode> <duration_ms>`: id positivo, mode 0/1/2, duração 1000–120000 ms. Responde `LOAD_QUEUED` ou `LOAD_REFUSED`.
- Mode0: controle sem trabalho de aquisição; mode1:16 canais virtuais/100Hz/32 iterações inteiras por canal; mode2:32 canais/1000Hz/128 iterações. São cargas definidas de cálculo/transferência, sem acesso a periféricos.
- `load-stop`: parada explícita, `LOAD_STOP_QUEUED` ou `LOAD_REFUSED`.
- `load-status`: JSON único `kind:load_benchmark, protocol:1`, id/mode/state e agregados limitados; sem amostras.
- Agregados: tempo, trabalhos produzidos/consumidos/substituídos/pendentes, períodos perdidos, pior atraso de despacho/consumo, tempo total/máximo do kernel, checksum; heap livre/mínimo/maior bloco e pilha mínima main/SD/rádio/carga em bytes.
- Worker da carga no core1/prioridade1, mailbox1; espera com yield e prazo automático, sem alocar por leitura. Instrumentação ausente do build normal.

## Etapas

### 1. Diagnóstico isolado e verificação local

**Files:** `firmware/ecu/include/load_bench_policy.hpp`, `load_bench.hpp`, `firmware/ecu/src/load_bench.cpp`; integração condicional em `main.cpp`, `radio.hpp/.cpp`, `platformio.ini`; teste `tests/native/test_load_bench.cpp`.

- [x] Escrever testes de parser, limites, scheduler sem catch-up, kernel determinístico e contabilidade de consumo; observar RED e depois GREEN.
- [x] Implementar diagnóstico com snapshots protegidos, fila limitada e parada automática.
- [x] Compilar variante e normal, executar testes nativos/simulador e auditoria. Comparar normal com base e conferir ausência de símbolos/comandos de carga no normal.
- [x] Usar REA nas seções/ELF e ferramenta Xtensa no kernel para confirmar execução materializada e configuração efetiva. Revisar limites antes da gravação.

### 2. Condutor de bancada e evidências

**Files:** `tools/benchmark_load.py`, testes sem serial físico em `tests/simulator/test_load_bench_client.py`, resultados privados ignorados em `local/synthetic-load/`.

- [x] Implementar condutor explícito sem conexão em import/help, parser com whitelist e verificação de versão/protocolo/flags; testes de mensagens incompletas, dados privados e cleanup.
- [x] Preservar backup privado do aplicativo atual e verificar prefixo pelo SHA-256 conhecido; gravar somente app0x10000 do diagnóstico.
- [x] Medir controle repetido, nominal, pico, nominal+SD refresh, nominal+busca Wi-Fi, nominal+anúncio BLE; 20Hz de SIM e janela UI10s. Medir também recebimento SIM em taxa maior, sem confundir com taxa física.
- [x] Executar ensaio prolongado de 120s na carga nominal e observar memória/atrasos. Parar se reset, falha de memória ou UI interrompida.
- [x] Encerrar toda sessão/carga/rádio e restaurar aplicativo normal original; verificar hash e estado SD/perfil/SIM/rádios.

### 3. Relatório e revisão

**Files:** `docs/bateria-carga-sintetica.md`, `docs/measurements/synthetic-load-2026-11-12.json` e links pertinentes.

- [x] Publicar no relatório apenas agregados revisados, método e limites; separar software, análise estática e bancada.
- [x] Revisão independente do código/privacidade/métricas e restauração; corrigir defeitos materiais e repetir somente o que a correção invalidar.
- [x] Atualizar e ler de volta AI Memory no escopo explícito. Entregar resultados concretos sem prometer CPU livre ou validação veicular.

## Decisões de execução

O pedido autoriza a execução desta bateria; escolhas rotineiras do diagnóstico são responsabilidade do implementador. Agentes auxiliares trabalham em arquivos separados, sem USB. Não reconstruir SDK para adicionar runtime stats nesta etapa: isso mudaria a base da comparação. A liberação temporária USB foi solicitada ao operador porque a ACL anterior desapareceu.

Ruling após bancada: USB100 falhou com control_timeout, inclusive no modo 0; preservar ambas as falhas, sem aumentar buffers ou alterar pacing para ocultar o limite. Acrescentados nominal USB50 e controle USB100; total de 11 tentativas, com nove casos completos. Heap/pilhas observados desde boot de cada caso. Aplicativo normal original restaurado por backup/hash; novos caminhos existem somente no diagnóstico.
