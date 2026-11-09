# Cockpit fluidez e leitura Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans.

**Goal:** Aplicar as cinco melhorias aprovadas após a revisão do cockpit 0.4.7.

**Architecture:** Manter fontes e aquisição isoladas do LVGL. Apresentação usa movimento adaptativo e termina trabalho quando estabiliza; alertas são calculados nos dados brutos, com prioridade e contagem. Graduação é estática e ensaios usam somente dados sintéticos.

**Tech Stack:** C++17/Arduino ESP32, LVGL 8.4, LovyanGFX, PlatformIO; testes nativos ASan/UBSan e renderer LVGL host.

**Spec:** Cinco propostas na conversa aprovadas pelo usuário com “faça”.

## Global Constraints

- Somente tela ESP32: TPS ausente, simulador/modelo/protocolo Linux preservados.
- Não habilitar entradas físicas/atuadores, formatar SD, apagar NVS/calibração ou mudar permissões/GPIOs.
- Manter RPM/rampa, números grandes, tema preto/vermelho/laranja, menu e duplo toque.
- Alertas e TTL usam dados brutos e ignoram easing; zero válido difere de ausência.
- Não registrar amostras/serial bruto nem credenciais; somente agregados/capturas sintéticas verificadas.

## Review Focus

- Animação deve terminar de apresentar o alvo, inclusive entre cadências 60/30 Hz.
- Alvo repetido não reinicia easing; invalidade, sessão nova, urgência e rollover mantêm semântica.
- Contagem de falhas não duplica óleo nem RPM alto/crítico; stale remove tudo.
- Graduação acompanha escala 1000–12000 e não invade números/rampa nem amplia redrawing de rotina.
- Medidas distinguem regularidade, espera e CPU real; nunca prometer FPS óptico/limite seguro do motor.

## Task 1 — Movimento adaptativo e repouso

- [x] Implementar API pura de estado animating(now) e duração adaptativa por delta normalizado, com limites explícitos, preservando retarget atual e sem heap.
- [x] Testar transições pequenas/grandes, alvo repetido, término exato, invalidade, urgência e rollover.
- [x] Integrar no serviço UI: cessar cálculo/formatação sem mudança/movimento, com atualização final de números.

## Task 2 — Falhas simultâneas

- [x] Expor máscara/contagem de alertas na View sem alterar alert primário existente.
- [x] Contar causas independentes; óleo sem duplicação; RPM alto/crítico exclusivos; RPM ausente suprime aviso de giro; stale/disabled zera.
- [x] Mostrar “+N” separado do texto principal, preservando legibilidade e fonte SIM.

## Task 3 — Graduação e integração

- [x] Colocar referências discretas de RPM na área livre acima da curva; escala acompanha ajustes, sem alterar calibração/escala salva.
- [x] Validar layout/extremos/alertas/TTL no renderer real e testes; compilar normal/manutenção/benchmark e auditar pinos/hashes.

## Task 4 — Bancada, métricas e entrega

- [x] Identificar alvo antes de gravar app, verificar hash, preservar dados e restaurar simulador.
- [x] Ensaiar regularidade e repouso, comparar base 0.4.7 e candidato; ensaiar atividade SD/rádio suportada, registrar limites reais.
- [x] Capturar tela real, revisar, documentar resultado, publicar source e atualizar memória com leitura de volta.

## Resultado

0.4.8 instalada em app0x10000 com hash conferido; 16 nativos, três builds, host
LVGL e auditoria passaram. Comparação de quatro cenários base/final concluída;
capturas reais inspecionadas; simulador restaurado. Evidência e limites em
`docs/validacao.md`, seção 0.4.8. Nenhuma aquisição/atuação física habilitada.
