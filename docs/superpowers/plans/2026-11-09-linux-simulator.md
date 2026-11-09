# Simulador Linux Implementation Plan

> **For agentic workers:** módulos independentes corePython, GUIQt, protocoloC++ em paralelo; integração e hardware somente pelo responsável principal.

**Goal:** app Linux para condução/falhas ilustrativas, offline e USB explícito para painelESP32.

**Architecture:** modelo/codecPython puro, GUIQt na threadprincipal, serialworkercomsnapshotlatest. EstadoUSB eUIESP32 mesmo dono, TTL 750 ms e fonte marcadaSIM USB.

**Tech Stack:** Python3.12 em.venv, PySide6-Essentials6.12.0, pyserial3.5, C++17Arduino2.0.17/LVGL8.4.

**Spec:** docs/superpowers/specs/2026-11-09-linux-simulator-design.md

## Global Constraints

Nenhum sensor/atuador habilitado, formatoSD ou alteração de pinos/calibração.
Sem amostras em arquivos/logs. App inicia offline; conexão exige clique.
Testes de transporte em memória; bancada separada dos unitários.
Sem alterar runtimes do sistema ou permissões/grupos. Fontes publicadas revisadas.

## Review Focus

Handshake precisa identificarNGK ECU/protocolo antes de transmitir.
Sessões/replay/TTL não podem deixar números antigos como dados vivos.
Falhas ausente/aberto/curto distinguem-- de zero; fonteSIMpermanecevisível.
Fechar/desconectar não bloqueiaGUI e para a sessão; nenhuma autoconexão.
Filas/RX/TX limitados, saídas deCSV/dumps inexistentes.

## Tarefas

- [x] Modelo e codecPython com TDD de finitude, limites, dinâmica manual/condução e goldensUSB.
- [x] Parser/StateC++ com TDD de limites, token/seq, TTL 750 e uint32wrap; runtimeUIowner.
- [x] SerialLink com transportefake, framerlimitado, handshake/start/stop/pausa/erro/conexão explícita.
- [x] GUIQt testadaoffscreen, presets/falhas/controles e launcherlocal; inspeção visível.
- [x] Integração FWmain/UI: parâmetros emSIM USB, erros, stale e saída local; compatbenchmark.
- [x] Revisão independente, testes Python/nativos/fontes, builds e bancadaUSBseparada.
- [x] App abertooffline, atalhouserlocal, docs/manifest/fontes publicadas e memória lida de volta.

## Resultado

35Python/Qt+14 native+12 fonttestsPASS;3 buildsPASS. HardwareUSB manual separado
passou emlive/replay/TTL/stale/retomada/stop ebackendSerialLinkreal. JanelaLinux
Wayland visíveloffline e atalho criado. Dados/amostras somente em memória.
Imagem/hash no manifest. Operador escolhe Conectar; nenhum sensor/atuador habilitado.
