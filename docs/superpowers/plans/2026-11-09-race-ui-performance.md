# Race UI e fluidez Implementation Plan

> **For agentic workers:** use superpowers:subagent-driven-development ou execução nativa. Não abrir hardware nos testes unitários.

**Goal:** interface automobilística refinada, configurações reais e conexões
com fluidez medida, preservando segurança e SD.

**Architecture:** LVGL de dono único; instrumentos persistentes e áreas parciais;
rádios e armazenamento em workers, snapshots e filas limitadas.

**Tech Stack:** C++17, Arduino2.0.17, LVGL8.4, LovyanGFX1.2.7, NimBLE2.5.1;
Python3.12/.venv e ferramentas isoladas.

**Spec:** `docs/superpowers/specs/2026-11-09-race-ui-performance-design.md`.

## Global Constraints

- Sem sensores/atuadores externos ou dados fictícios apresentados como reais.
- DEMO explícito e desligado no boot; preservar duplo toque/calibração.
- Sem logs de dados brutos, nomes de redes, senhas ou identificadores de rádio.
- Não formatar SD nesta tarefa; perfis somente em diretório próprio.
- Sem mudanças de configuração global ou frequência SPI acima de 40 MHz.

## Review Focus

- Último DMA define apresentação; tiles não podem inflar contagem.
- Radio/SD não bloqueiam callbacks LVGL ou geram filas ilimitadas.
- Perfis inválidos/corrompidos não mudam configurações nem revelam credenciais.
- Controles tocam valores validados e persistem somente ao aplicar.
- Heap insuficiente e operações assíncronas produzem estado de erro legível.

## Tarefas

- [x] Medir base instrumentada: estatísticas puras testadas, cena DEMO e relatório agregado.
- [x] Modelo/codec de configurações versionado, com validação e testes de corrupção/faixas.
- [x] Worker Wi-Fi/BLE e política de entrada limitada, sem acesso físico por subagentes.
- [x] Tema e layout automobilístico, ajustes por páginas e atualizações parciais.
- [x] Integrar perfis SD e persistência assíncrona; evitar remontagem saudável a cada5s.
- [x] Comparar métricas antes/depois e cenários com rádio, revisar e gravar normal.
- [x] Publicar fontes/evidência sanitizada e confirmação visual separada.

## Resultado e desvios verificados

Doze testes nativos, nove testes de fonte e builds normal/manutenção/benchmark
passaram. Firmware0.2.0-race normal instalado, hash no source-manifest.json.
Perfis SD exportados/importados e restaurados após reinício; nenhum formato
nesta tarefa. REA inspecionou o ELF próprio sem alterá-lo.

A fonte reduzida poupou 92.084bytes. Atualização dos números restringe a área
invalida à largura do conteúdo; barra retangular evita máscaras arredondadas.
Produtor17/33/50ms separado do timer LVGL 5/11/16ms; benchmark mantém produtor 17ms.

Bancada final:56,6parciais/s,p9518,089ms,fullp9556,696ms; Wi-Fi scan e BLE
advertising separados56,5/s; serial sob carga56,0/s. Ver relatório de validação.

Limite observado: proteção de RAM recusou iniciar segundo rádio. Implementações
estão disponíveis uma por vez nesta unidade. Pareamento com cliente externo e
associação Wi-Fi com senha não foram ensaiados. Confirmação visual do usuário
para a versão nova ainda pendente; capturas do código em host foram revisadas.
