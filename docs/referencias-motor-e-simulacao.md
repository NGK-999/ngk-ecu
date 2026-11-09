# Motor 1300: referências e limites da simulação

O motor informado é um Fusca 1300 carburado, ano desconhecido, com futura
injeção monoponto. O responsável pretende medir temperatura na **superfície
do bloco**. Ainda faltam posição exata, sensor, fixação e calibração.

## Rotação real e escala do instrumento

A [ficha histórica da Volkswagen](https://www.volkswagen-newsroom.com/de/motorversionen-kaefer-1300-steckbrief-19592)
informa versões europeias do 1300 com potência máxima a 4.000 ou 4.100 RPM.
O [manual brasileiro VW 1300/1500, página impressa 55](https://cariridasantigas.com.br/wp-content/uploads/2021/03/manual-fusca-70-71-72.pdf#page=56)
informa 38 cv DIN a 4.000 RPM e 46 cv SAE a 4.600 RPM para a versão descrita.
São dados de potência; não especificam um redline universal. Não foi encontrada
fonte primária aplicável que estabeleça 9.000 RPM como giro admissível desse
motor. A escala do painel e a faixa de entrada de um protocolo não homologam
nenhuma rotação para uso no veículo.

O [mesmo manual, página impressa 35](https://cariridasantigas.com.br/wp-content/uploads/2021/03/manual-fusca-70-71-72.pdf#page=36)
especifica marcha lenta quente de 650–700 RPM para sua configuração Solex H30 PIC.
Isso não identifica o carburador ou a regulagem do motor instalado.

## Temperatura depende do ponto

200 °C na superfície do bloco, no óleo e no cabeçote não são medições
intercambiáveis. O [instrumento CHT Dakota Digital](https://www.dakotadigital.com/pdf/odyr-11.pdf)
usa anel sob a vela e tem sua própria faixa de medição e ajuste de alarme;
isso não estabelece uma temperatura normal da carcaça VW. O
[instrumento de óleo do mesmo fabricante](https://www.dakotadigital.com/pdf/odyr-07.pdf)
usa outro sensor e outro local de instalação. Conferir também unidade:
200 °F correspondem a aproximadamente 93,3 °C.

Não há neste projeto um limite térmico real calibrado para a superfície do
bloco. Os 200 °C do simulador representam o extremo manual permitido para
ensaiar a apresentação. Não são um alvo normal nem uma referência de segurança.
A falha sintética **Superaquecimento** continua explícita e independente dessa
incerteza; não interpreta uma temperatura medida no veículo.

## Perfil sintético implementado

- Lenta quente perto de 700 RPM com pequena oscilação determinística;
  partida fria com alvo maior. Inércia suaviza aceleração e retorno à lenta.
- Condução automática limitada a 5.000 RPM no modelo ilustrativo. Não é
  especificação de máxima real nem implementação de corte de ignição.
- Modo manual conserva 0–12.000 RPM, incluindo 9.000, para testar anomalias.
  A barra Linux usa escala 0–6.000; o número continua mostrando o valor completo.
- Alertas visuais independentes da escala: atenção a partir de 4.000 RPM,
  crítico a partir de 4.500 RPM, liberação com histerese de 150 RPM. Referências
  provisórias de bancada, não limites mecânicos qualificados. RPM ausente ou
  fonte vencida limpa o alerta; óleo/temperatura têm prioridade no rodapé ESP32.
- Temperatura automática opcional com aquecimento/resfriamento gradual, carga
  e velocidade; ambiente de referência 25 °C, alvos ilustrativos de 85–110 °C.
  Não é previsão térmica do bloco real. Manual conserva o valor exato; alterar
  a temperatura inicial reinicializa o estado térmico no modelo automático.

O modelo não foi calibrado em dinamômetro, no veículo ou no sensor. Não simula
combustão, falha mecânica, corte real, embreagem detalhada ou curva de torque
medida. Toda a integração permanece sintética, sem habilitar saídas físicas.
