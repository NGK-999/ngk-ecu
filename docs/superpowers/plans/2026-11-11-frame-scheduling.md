# Distribuir atualizações numéricas entre quadros

**Pedido:** resolver os engasgos; prioridade explícita do responsável: fluidez da tela.
**Base:** `1a1ef9d`, branch `preview/painel-disposicao`.
**Escopo:** apresentação na ESP32; manter aparência, valores, alertas e protocolos.

## Evidência e decisão

O firmware normal só grava pequenos perfis no SD. Não há consumidor de logs
contínuos que justifique introduzir um buffer de escrita nesta etapa.

Três estratégias de invalidação de glifos foram testadas e descartadas:
reduzir pixels não produziu ganho útil de fluidez. Na última, A/B no mesmo
binário, 1094 comparações de framebuffer passaram e os pixels caíram12,26%,
mas o tempo acumulado do tick só caiu0,40% e a rampa p95 passou22→23ms.
A preparação compensou quase toda economia de desenho. Esse helper não integra
esta entrega; fontes e resultados experimentais completos permanecem locais.
O aplicativo original0.4.9 foi restaurado após os ensaios, com hash conferido.

RPM e velocidade vinham sendo apresentados juntos a aproximadamente30Hz,
concentrando dois números grandes no mesmo quadro. A nova arquitetura
reparte essa carga: no modo Suave, slots60Hz alternam RPM e velocidade,
conservando aproximadamente30Hz para cada número e60Hz para a rampa.
Não depende de novo framebuffer, cache de bitmap ou cartão como RAM.

## Requisitos

- Pendências independentes para RPM e velocidade, consumidas somente depois
  que cada instrumento apresentar seu valor, inclusive o terminal da animação.
- Urgência, validade/TTL, sessão/página e mudanças que exigem pintura completa
  atualizam ambos imediatamente. Não suavizar alarmes nem manter dados antigos.
- Equilibrado, Econômico e Reduzir movimento conservam o caminho combinado.
  No Suave não aplicar limitador30Hz antes do alternador60Hz.
- Não roubar o slot sem trabalho de um instrumento nem recuperar quadros
  atrasados em rajadas. Repouso continua sem interpolação/formatos/desenho.
- UI continua dona do LVGL; DMA só é liberado quando concluído. Sem GPIO,
  atuação, NVS, calibração, partições ou formato do SD alterados.

## Passos

1. [x] Helper portátil de seleção de instrumentos, testes de cadência,
   pendências, urgência, mudança de modo e ausência de rajadas.
2. [x] Integrar no caminho SIM, mantendo o setter convencional e aparência.
   Referência `NGK_COMBINED_NUMBERS` conserva o agendamento combinado.
3. [x] Acrescentar agregados de pixels, passes e maior gap de despacho por
   número. Gap inclui períodos sem trabalho; não é idade da amostra.
4. [x] Compilar/auditar; comparar combinado e alternado no mesmo binário
   temporário. Depois medir o aplicativo final sem instrumentação temporária.
5. [x] Capturar/inspecionar pixels da placa, revisar resultados e documentar.
   Manter candidata somente com ganho útil; senão restaurar o backup original.

## Validação

Testes locais usam somente memória/dados sintéticos e não abrem hardware.
Bancada USB é separada: fonte sintética20Hz, janelas10s, mesmo perfil, carga e
política de consultas; observar rampa, tick máximo, pixels e despacho dos dois
números. Preservar backup e escrever somente aplicativo0x10000/hash conferido.
`sample_to_present` mede a primeira mudança de instrumento, incluindo rampa;
não mede a conclusão simultânea dos dois números. Tempo decorrido do tick
inclui esperas/preempção e não deve ser apresentado como percentual de CPU livre.
Apresentações parciais não são FPS óptico nem telas completas60Hz.

## Resultado

Concluído: 19 testes nativos, 87 do simulador, quatro builds, auditoria e revisão
independente aprovados. A/B no mesmo binário: p95 entre apresentações −27%,
maior tick −21,5%, tempo acumulado +4,4%. O normal sem probe confirmou a faixa
e manteve repouso sem desenho; gaps DMA de 34 ms foram associados a passes
sem invalidação, com despacho máximo de 18,126 ms. Refresh SD pontual passou.
Capturas normais/alertas coincidem byte a byte com os PNGs anteriores.
Versão 0.4.10-staggered mantida na placa; documentação e dados em
[fluidez 0.4.10](../../fluidez-0410.md).
