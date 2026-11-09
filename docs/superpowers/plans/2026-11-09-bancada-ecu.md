# NGK ECU bancada Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Track steps below.

**Goal:** instalar uma base própria NGK ECU e manter a manutenção SD separada.

**Architecture:** interface LVGL em uma tarefa, armazenamento em worker separado,
snapshots sem leituras de motor inventadas. Firmware normal exclui formatação.

**Tech Stack:** C++17, Arduino-ESP32 2.0.17, LVGL8.4.0, LovyanGFX1.2.7,
PlatformIO6.1.18 isolado.

**Spec:** `docs/superpowers/specs/2026-11-09-bancada-ecu-design.md`.

## Global Constraints

- Sem domínio industrial na aplicação: somente instrumentos ECU.
- Sem entrada de sensor ou saída de atuação habilitada nesta base.
- Não publicar backups, credenciais ou dados brutos.
- Formato SD somente em manutenção e para cartão confirmado.
- Preservar backup local verificado; somente host principal acessa serial.

## Review Focus

- Instrumentos sem dados nunca mostram zero como leitura válida.
- DMA não reutiliza buffer antes de concluir transferência.
- Toque produz uma ação por clique e possui calibração explícita.
- Falha/ausência de SD não bloqueia a GUI nem apaga cartão.
- Cartão de capacidade divergente não pode ser formatado implicitamente.

## Task 1 — UI e perfil

- [x] Testar geometria de navegação/layout com alvos>=44px e sem sobreposição.
- [x] Implementar `firmware/ecu/include/ui.hpp`, `board.hpp`, `lv_conf.h`,
  `firmware/ecu/src/ui.cpp`, tema e páginas próprias.
- [x] API `ecu::ui::begin()`, `tick()`, `setStorage(bool,uint32_t)`, sem dependência
  de aquisição; inicialmente RPM/temp/velocidade/óleo indisponíveis.

## Task 2 — Política de manutenção SD

- [x] Testar comandos completos, limites, overflow, formato único e capacidade.
- [x] Implementar política C++ pura em `firmware/ecu/include/maintenance_policy.hpp`.
- [x] Nenhum teste acessa serial ou disco físico.

## Task 3 — Integração e bancada

- [x] Implementar entrypoint e worker SD em `firmware/ecu/src/main.cpp`,
  `platformio.ini` e partições; normal exclui código de formato.
- [x] Testar bibliotecas nativas e compilar ambos os ambientes.
- [x] Revisão independente antes de gravar; instalar manutenção apenas se necessária.
- [x] Resolver divergência do cartão antes de qualquer comando destrutivo.
- [x] Instalar normal sem comando de formato, verificar identificação e atualizar evidências.

## Complemento solicitado — menu e painel de carro

- [x] Menu inicial 2×2 e painel sem dock, com números grandes.
- [x] Duplo toque retorna ao menu; rejeita arraste/toque longo e trata wrap de tempo.
- [x] Cinco executáveis nativos, builds e gravação da versão 0.1.1-bench verificados.
- [ ] Confirmar visualmente e testar o gesto na tela real com o responsável.
