# Ciclo automático e estabilidade Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans.

**Goal:** Entregar o ciclo automático aprovado, executar 30 minutos reais de estabilidade em memória e medir a prévia na ESP32 quando o acesso Linux permitir.

**Architecture:** Linha temporal pura com controles sintéticos; janela Qt apenas aplica snapshots/mostra fase e permite parar. Ensaio separado injeta transporte em memória, exercita a janela/modelo/codec/link reais e guarda somente agregados. Benchmark físico usa comandos já existentes, mantendo aquisição e atuação desabilitadas.

**Tech Stack:** Python3.12/.venv, PySide6, unittest, transporte em memória; C++17/LVGL8.4/PlatformIO na bancada física.

**Spec:** Três próximos passos propostos na conversa e aprovados por “pode fazer”.

## Global Constraints

- App inicia offline; ciclo não conecta USB nem altera rádios/SD/firmware.
- Não persistir amostras, serial bruto, tokens, credenciais ou identificadores de hardware.
- Modelo continua ilustrativo; sem novo limite mecânico/termal calibrado, GPIO ou atuador.
- Preservar pausa manual, watchdog produtor250ms, TTL750ms e último snapshot.
- Manter branch preview/painel-disposicao e arquivos existentes; não mudar permissões/grupos por ferramentas.
- Ensaio prolongado é de30minutos REAIS (1800s), com eventos Qt ativos; teste virtual não o substitui.

## Review Focus

- Passagem exata entre fases, salto de tempo, ciclo único/repetido e controle original preservado.
- Mutabilidade dos Controls não pode contaminar templates/original; relógio inválido/reversão rejeitado.
- Parar/fechar restaura controles e não inicia conexão; editar controles manuais durante ciclo não cria disputa.
- Ensaio em memória não pode acessar porta física nem acumular samples/linhas; recuperação verifica sessão e threads.
- Métricas distinguem modelo/Qt/USB simulado de LCD/USB físico/CPU real da ESP32.

## Task 1 — Linha temporal pura

- [x] Criar scenario.py e testes: DriveCycle.start(now,controls,repeat=True)->CycleFrame; update(now)->CycleFrame|None; stop()->Controls|None; running bool.
- [x] CycleFrame expõe phase_name,index,cycle_number(1-based),phase_elapsed,total_elapsed,controls(cópia).
- [x] Dez fases somando60s: parado4, partida/lenta8, primeira6, segunda6, terceira6, quarta6, frenagem8, falhas6, recuperação6, desligamento4.
- [x] Produzir controles: automático/termalauto, temperatura inicial25°C, primeira45%/segunda60%/terceira65%/quarta35%; frenagem70%; falhasLowOil|Overheat; recuperaçãosemfalhas; semRPMmanualforçado.

## Task 2 — Janela e ensaio prolongado

- [x] Integrar Iniciar/Parar/Repetir e fase visível; preservar controles originais, bloquear edição do modelo durante ciclo, manter conexão e pausa acessíveis.
- [x] Criar scriptsoak --seconds1800 --output agregado, Qt offscreen e MemorySerial própria limitada, link/modelo/UI reais.
- [x] Exercitar fases, pausa e retomada, produtor parado, desconectar/reconectar; contar frames/erros/threads, medir timers e RSS e só persistir sumário.
- [x] Rodar suiteunitária e ensaio30min, inspecionar visualnormal/mínimo980×680 e 1120×760.

## Task 3 — Benchmark e entrega

- [x] Verificar acesso ao dispositivo sem alterar permissões; liberação depende do operador se Linux negar.
- [x] Quando acessível: identificar NGK/versão/4MiB/flagsfalse; compilar prévia, gravar somente app0x10000/hash; repetir cenários movimento/repouso/SD+WiFi/SD+BLE e capturar tela real.
- [x] Acesso concedido pelo operador; caminho de bloqueio não se aplica.
- [x] Revisão independente, documentação com evidências/limites, commit/push e memória com leitura de volta.

## Resultado

74 testes passaram; revisão independente sem defeito material. Ensaio real
concluído em 1800,005759 s, todos os 16 critérios aprovados: 30 recuperações de
cada perturbação, 31 sessões fechadas e worker encerrado. RSS após aquecimento
sem crescimento contínuo observado; não prova ausência de vazamentos.

Prévia 0.4.9 instalada somente no aplicativo, hash verificado. Quatro medições
físicas concluídas; SIM/rádios restaurados e duas capturas inspecionadas.
Documentação revisada; entrega na mesma branch, sem merge em main.
