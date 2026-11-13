# Correções da auditoria 0.4.14

Especificação aprovada: `docs/auditoria-codigo-2026-11-12.md`, achados A1–A6.
Base: `40626184d754dd9b4e12a37ed68a74d3690ca8e0` mais alterações locais
0.4.13/0.4.14 preservadas na branch `preview/painel-disposicao`.

## Restrições

Preservar Arduino 2.0.17, IDF 4.4.7, LVGL 8.4, `.venv`, perfil de placa,
três barramentos, aquisição independente e proprietário único do LVGL.
Testes automatizados somente sintéticos. Sem formatação, alteração de NVS,
SDK, partições, GPIOs ou atuação. Sem commit/push automático.

## Tarefas

1. A1: coordenar conclusão/cancelamento da busca Wi-Fi com o callback Arduino
   antes de liberar resultados ou admitir outra busca. Cobrir cancelamento,
   timeout, desligamento e notificação tardia com testes de regressão.
2. A2–A4: sincronizar legendas com unidades efetivas em importação assíncrona;
   invalidar a rampa quando a escala RPM mudar; corrigir prévia imperial e
   verificar textos e pixels com LVGL real, preservando o boot rápido.
3. A6: distinguir solicitação pendente de autorização consumida. Comando
   inválido pode ser corrigido após resposta; formatação aceita continua
   consumindo autorização antes da operação e troca/remoção revoga acesso.
4. A5 e integração: revisar pinagem e renovar snapshot após alterações finais;
   identificar candidata como 0.4.15-audit. Compilar os seis ambientes,
   executar suítes pertinentes e prévia, inspecionar imagens e revisar diff.
5. Documentar evidências, limites e memória. Instalação normal em bancada
   depende de acesso serial; preservar backup e gravar só app após validação.

## Critérios de aceite

Regressões demonstram a falha anterior e passam após correção. Sem reconstrução
desnecessária no boot ou redraw contínuo em repouso. Sem buffer Wi-Fi liberado
antes do fim do callback, nem scans sobrepostos. Rejeição de formato permite
retry; aceite/revogação nunca permite segunda operação. Validador canônico
passa mantendo os três avisos conhecidos. Resultados host e físicos separados.
