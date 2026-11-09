# Escopo inicial

Fonte: requisitos fornecidos pelo responsável pelo projeto em 2026-11-09.
Este registro documenta o pedido; não é um projeto elétrico ou de firmware aprovado.

## Objetivo

Exibir rotação, temperatura do bloco e velocidade em uma tela touch ligada a
um ESP32, num motor boxer de Fusca instalado numa gaiola para uso fora de estrada.
Preservar um caminho de evolução para gerenciamento de combustível e automações.

## V1: instrumentação

1. Identificar placa, revisão, flash, tela, touch e interfaces disponíveis.
2. Validar tela e touch em bancada, inicialmente com valores sintéticos
   explicitamente identificados como demonstração.
3. Integrar sensores com circuitos de entrada e calibração documentados.
4. Validar microSD identificado, FAT32 se compatível e comportamento sem cartão.
5. Validar medições em bancada antes de ensaios no veículo.

A ordem é proposta técnica, sujeita ao hardware confirmado. Taxas de amostragem,
limites de alarme, faixas e precisão ainda não foram definidos.

## Comportamento esperado

- Cada leitura precisa distinguir valor válido, ausente, fora de faixa e antigo.
- Ausência de leitura não equivale a zero; perder RPM não prova que o motor parou.
- RPM exige pulsos por volta configuráveis conforme a origem real do sinal.
- Velocidade por roda exige pulsos por volta e circunferência calibrados;
  GPS é uma alternativa a decidir, não uma característica já escolhida.
- Temperatura do bloco não equivale a temperatura do óleo ou do cabeçote.
  O ponto de montagem e o tipo do sensor fazem parte da calibração.

## Organização proposta, ainda sem implementação

Separar aquisição e validade das medições, apresentação na tela e acesso ao
cartão. Falha ou lentidão na tela/cartão não pode bloquear aquisição.
Antes do firmware, comparar ESP-IDF e Arduino conforme o suporte da placa,
drivers e requisitos de aquisição. Nenhum framework ou GPIO está fixado ainda.

Para a etapa de atuação, avaliar controlador de motor dedicado comunicado com
o painel, ou controle integrado com requisitos de temporização e falha explícitos.
A primeira opção é a recomendação inicial para isolar travamentos da interface,
mas essa decisão não foi tomada pelo responsável pelo projeto.

## Etapas futuras

Mapas de combustível e acionamento de injetores/bobinas não fazem parte da V1.
Antes disso: definir sincronismo e roda fônica, sensores adicionais necessários,
drivers de potência, estados de falha e plano de validação em bancada. Não
transformar medições de painel em saídas de atuação sem revisar a arquitetura.

## Dados necessários para continuar

- Revisão exata da placa e esquema correspondente.
- Origem do RPM e da velocidade; modelo e posição do sensor de temperatura.
- Acesso autorizado à serial sem alterar grupos/permissões nesta tarefa.
- Identificação do cartão de 32 GB por leitor de cartões ou firmware conhecido.

O pedido autoriza formatar o cartão indicado, mas não identifica um disco do
computador como alvo. Nenhum disco deve ser escolhido somente pela capacidade.
