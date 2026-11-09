# Escopo inicial

Fonte: requisitos e complementos fornecidos pelo responsável em 2026-11-09.
Este registro documenta o pedido; não é um projeto elétrico ou de firmware aprovado.

## Objetivo

Exibir rotação, temperatura do bloco e velocidade em uma tela touch ligada a
um ESP32, num motor boxer de Fusca **1300 carburado**, ano desconhecido,
instalado numa gaiola para uso fora de estrada. Incluir todos os sensores e
sinais de fábrica efetivamente presentes nesse motor, após inventário físico,
sem assumir equipamentos de versões injetadas.
Preservar um caminho de evolução para injeção **monoponto** e automações.

## Autorizações e fluxo de manutenção

O responsável autorizou trocar o firmware atual e usar a própria placa para
preparar/formatar o cartão informado como 32 GB, pois não pode usar leitor
externo. Não pedir novamente autorização genérica para essas ações.

Após acesso serial: identificar chip/flash, preservar backup local do firmware
atual quando possível, confirmar o perfil de hardware e instalar manutenção
dedicada ao SD. Identificar capacidade/estado do cartão antes de apagar e testar
escrita/leitura apenas com arquivo de teste próprio. Reinstalar firmware normal
sem o comando de formatação. Uma falha de montagem nunca deve formatar sozinha.

A falta atual de acesso serial é um bloqueio operacional, não falta de
autorização. O comando temporário de ACL está no README.

## V1: instrumentação

1. Identificar placa, revisão, flash, tela, touch e interfaces disponíveis.
2. Validar tela e touch em bancada, inicialmente com valores sintéticos
   explicitamente identificados como demonstração.
3. Integrar sensores com circuitos de entrada e calibração documentados.
4. Validar microSD identificado, FAT32 se compatível e comportamento sem cartão.
5. Validar medições em bancada antes de ensaios no veículo.

A ordem é proposta técnica, sujeita ao hardware confirmado. Taxas de amostragem,
limites de alarme, faixas e precisão ainda não foram definidos.
O inventário de fábrica e as medições que exigem sensores adicionais estão em
[sensores-motor.md](sensores-motor.md). A orientação visual está em
[interface.md](interface.md).

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

Mapas de combustível e acionamento de injetor(es)/bobinas não fazem parte da V1.
O caminho de injeção definido pelo responsável é **monoponto**, não multiponto.
Antes disso: definir a referência de rotação/sincronismo e avaliar a necessidade
de roda fônica conforme o controlador escolhido, além de sensores adicionais,
drivers de potência, estados de falha e plano de validação em bancada. Não
transformar medições de painel em saídas de atuação sem revisar a arquitetura.

## Dados necessários para continuar

- Revisão exata da placa e esquema correspondente.
- Origem do RPM e da velocidade; modelo e posição do sensor de temperatura.
- Disponibilizar o acesso à serial já autorizado, por ACL temporária específica.
- Identificação do cartão de 32 GB por firmware de manutenção conhecido.

O pedido autoriza formatar o cartão indicado, mas não identifica um disco do
computador como alvo. Nenhum disco deve ser escolhido somente pela capacidade.
