# Validação inicial — 2026-11-09

## Sistema e ferramentas locais

| Verificação | Resultado observado |
| --- | --- |
| Identidade GitHub via CLI | Conta NGK-999 confirmada pela API |
| USB | Ponte CH340 `1a86:7523` enumerada em `/dev/ttyUSB0` |
| Acesso à porta | Sem leitura/escrita para o processo atual; elevação não disponível |
| Inventário de discos | Nenhum dispositivo de bloco correspondente ao SD conectado |
| Node isolado | 24.22.1, arquivo Linux x64 conferido por SHA-256 oficial |
| REA CLI | `sh tools/rea/run --version` retornou `6.8.0`; ajuda executada |
| esptool | `.venv/bin/python -m esptool version` retornou `5.6.0` |
| Dependências Python | `pip check` na `.venv`: `No broken requirements found` |
| Script de execução | `sh -n tools/rea/run` sem erro de sintaxe |

Instalações são locais ao projeto. Não houve mudança de grupos/permissões,
Python/Node do sistema ou configuração global de agentes.

## Inspeção visual

Foto traseira fornecida pelo responsável indica família ESP32-2432S028,
módulo ESP-32, microUSB, USB-C e cartão inserido no slot TF. Revisão e
controladores reais continuam sem confirmação. A foto não foi publicada.

## Firmware, bancada e veículo

Não há firmware de produto nesta entrega. Não foram executados testes de
sensores ou tela/touch, nem comunicação serial, reset, leitura/escrita de
flash, formatação de cartão, acionamento ou ensaio no veículo.
Versões de ferramentas e pesquisa documental não validam hardware.

## Pendências

1. Disponibilizar acesso autorizado à porta serial, preservando grupos e
   permissões existentes conforme as regras desta tarefa.
2. Identificar o cartão no computador por um leitor microSD antes de formatar,
   ou definir um fluxo na própria placa com hardware e firmware confirmados.
3. Confirmar revisão/esquema e modelos dos sensores.
4. Definir projeto elétrico e firmware da V1 antes de gravar a placa.
