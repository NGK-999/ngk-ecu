# Pedidos do projeto e etapas de execução

Este plano consolida os pedidos do responsável. O projeto é **NGK ECU**, para
motor boxer VW Fusca **1300 carburado**, ano desconhecido, numa gaiola. A
referência externa inicial valia apenas para identidade visual; não dá nome ao
produto. A confirmação mais recente aprovou o funcionamento e restringiu o
redesenho à **tela da ESP32**, preservando o simulador Linux.

## 1. Conta, repositório e hardware

- [x] Usar a conta NGK-999 e criar repositório público para guardar o projeto:
  https://github.com/NGK-999/ngk-ecu . Publicação pelo Git/CLI autenticado.
- [x] Identificar o hardware conectado, preservar backup privado e substituir
  o firmware autorizado. ESP32 clássico, flash 4 MiB, sem PSRAM, LCD 320×240,
  touch resistivo e SD. Placa/touch foram verificados em bancada; a revisão
  completa da PCB e o sufixo exato do controlador ainda não estão confirmados.
- [x] Instalar e usar REA localmente para inspeção do próprio ELF/fontes. REA
  não mapeia automaticamente os fios nem forneceu descompilação Xtensa.
- [x] Preparar o cartão correto de 32 GB: o cartão anterior de cerca de 2 GB
  foi preservado; o de 30.255 MiB recebeu FAT32 e ensaio próprio de leitura/
  escrita. Isso é histórico, não autorização para escolher outro cartão.
- [x] Conferir o estado atual do SD: montagem e capacidade de 30.255 MiB
  confirmadas nesta revisão, com exportação do perfil concluída. A consulta
  inicial antecedia a montagem assíncrona; não houve novo formato.

Fontes: [hardware](hardware.md), [validação](validacao.md),
[ferramentas](ferramentas.md).

## 2. Instrumentação do Fusca e limites reais

- [x] Incluir no escopo RPM, velocidade, temperatura na superfície do bloco e
  todos os sinais originais **efetivamente presentes**. Óleo é estado de chave,
  não pressão numérica inventada. Inventário documentado, sem supor ano/versão.
- [x] Implementar apresentação, validade, ausência de leitura e alertas de
  rotação/falhas para a fonte sintética. Referências visuais 4.000/4.500 RPM;
  independentes da escala e sem corte físico.
- [ ] Inventariar no veículo ignição, instrumentos/sinais presentes, sistema
  elétrico e peças. Falta essa evidência para entradas físicas e calibração.
- [ ] Definir sensor, posição exata e fixação na superfície do bloco. Não há
  temperatura normal/limite térmico calibrado; 200 °C no simulador é extremo
  manual, não declaração de funcionamento normal.
- [ ] Projetar condicionamento, alimentação e ensaios antes de habilitar
  medições reais. Injeção **monoponto**, mapa de combustível, ignição e demais
  automações continuam como fases futuras explicitamente pedidas para depois.

Os itens físicos dependem de inventário/peças/ensaios; não são habilitados por
inferência ao melhorar a interface. Fontes: [sensores](sensores-motor.md),
[referências do motor](referencias-motor-e-simulacao.md), [escopo](escopo.md).

## 3. Tela e interação na ESP32

- [x] Menu com Painel, Sensores, Ajustes e Sistema; instrumentos grandes no
  modo painel e retorno por duplo toque. Touch/calibração e gesto funcionando
  foram confirmados pelo responsável.
- [x] Ajustes de tela/brilho, atualização, unidades, escala/faixas RPM,
  redução de movimento, toque/DEMO, perfis SD, Wi-Fi e Bluetooth.
- [x] Concluir a revisão preto/laranja/vermelho: referências FuelTech para
  instrumentos e Apple para hierarquia/alinhamento, menu 2×2 e nova distribuição.
  A barra RPM foi substituída por uma rampa curva sólida, conforme a imagem
  enviada posteriormente, com preenchimento contínuo e cores graduadas.
- [x] Concluir a implementação de fluidez: interpolação curta dos instrumentos, microanimação
  local, barra com alvo de 60 Hz/números cerca de 30 Hz e resposta imediata de
  alertas/validade. Não é promessa de 60 quadros ópticos completos.
- [x] Conferir capturas reais LVGL, memória, teste físico de apresentação e
  instalar firmware normal revisado. Preservar funcionamento aprovado e alvos
  de toque de pelo menos 44 px.

Sem carregamento SD por quadro, fontes Apple, blur ou animação de tela inteira.
Fontes: [interface](interface.md), [plano desta revisão](superpowers/plans/2026-11-09-interface-preto-laranja.md).

## 4. Simulador, comunicação e cartão

- [x] Aplicativo Linux nativo com motor, acelerador, freio, embreagem,
  marcha R/N/1–4, valores manuais e falhas; abre offline e conecta por ação do
  operador. Diagnóstico de acesso USB e recuperação por ACL documentados.
- [x] Modelo ilustrativo com lenta variável, partida fria, inércia e
  aquecimento/resfriamento. Manual aceita 9.000 RPM para ensaio de alertas.
- [x] USB com identificação, sessão/sequência, validação, TTL 750 ms, pausa/
  retomada e indicação SIM. Wi-Fi e BLE implementados sob demanda; testes
  externos com senha/pareamento não constam como concluídos sem evidência.
- [x] Exportação/importação validada dos perfis visuais no SD, com NVS e
  arquivos temporários/backup. Cartão não amplia RAM; fontes principais ficam
  compactas na flash. Gráfico do simulador fica somente em memória.
- [x] Explicar usos possíveis do SD e seus limites: recursos opcionais e
  pacotes de atualização podem retirar conteúdo não essencial da flash.
  Logs de sensores ainda não foram implementados; o projeto atual não persiste
  amostras ou serial bruto. Estas possibilidades não são funções já entregues.

Fontes: [simulador](../simulator/README.md), [armazenamento e desempenho](armazenamento-e-desempenho.md).

## 5. Ferramentas de IA, otimização e entrega

- [x] Aplicar a skill ezrover/ESP32-AI-Agent-Skill de forma local, fixada e
  revisada; executar validadores estáticos sobre os pinos existentes. Verificar
  limitações do gerador para barramentos distintos antes de aproveitá-lo.
- [x] Avaliar e preparar horw/esp-mcp isoladamente. Verificar o que funciona
  no projeto Arduino/PlatformIO; não trocar target nem configurar MCP global
  para simular compatibilidade com ESP-IDF.
- [x] Avaliar itemis/tflite-esp-template e AvantMaker/ESP32_AI_Connect pelo
  objetivo pedido: assistência ao desenvolvimento e economia de recursos.
  Uma função de ML/API embarcada não foi definida; não adicionar consumo de
  RAM/CPU/rede apenas pela existência dos exemplos.
- [x] Medir a revisão: testes sem hardware, renderização host, compilação e
  bancada separados. Comparar o benchmark antigo e medir animação real em
  teste separado; apresentação parcial não é FPS óptico ou teste veicular.
- [x] Publicar código/documentação revisados no GitHub e atualizar AI Memory
  com fonte, versão, evidência e limites, lendo de volta o registro.

O firmware usa C++17 e bibliotecas C, buffers fixos/DMA, tarefas de UI/serviços
separadas e fontes compactas. A revisão evita conversões sem dado novo,
realocações dos números principais por quadro, redesenho completo e filas
de animação.
Não há percentual de CPU livre ou capacidade de aquisição real certificado
sem ensaio específico.

A entrega 0.4.2 está documentada em [validação](validacao.md#cockpit-preto-e-rampa-contínua--042).
Pendências marcadas na etapa 2 são trabalho no veículo, sensor/calibração e
futura injeção; não são funcionalidades físicas habilitadas por este firmware.
