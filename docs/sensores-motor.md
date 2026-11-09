# Sensores e sinais — Fusca 1300 carburado

## Configuração informada

Motor boxer VW Fusca 1300, carburado, ano desconhecido, instalado em gaiola.
Injeção monoponto será uma etapa futura. O pedido é aproveitar todos os
sensores/sinais originais efetivamente presentes no motor. Não existe ainda
inventário físico do motor ou confirmação de distribuidor, carburador e gerador.

Não tratar esta lista como diagrama universal de todos os motores 1300.
Trocas de peças e adaptações da gaiola precisam ser registradas antes da ligação.

## Inventário inicial a confirmar no motor

| Item | Como entra no escopo | Limitação |
| --- | --- | --- |
| Interruptor de pressão do óleo, se presente | Estado elétrico do circuito e indicação de alerta | É uma chave, não um transdutor que mede pressão em bar |
| Distribuidor/ignição existentes | Fonte candidata de pulsos para RPM | Confirmar platinado/eletrônico e pulsos por volta; não presumir sensor Hall |
| Dínamo ou alternador existentes | Identificar circuito de aviso de carga; integração depende do circuito real | Aviso de carga é um sinal elétrico, não sensor de pressão/temperatura |
| Outros componentes originais encontrados | Completar inventário com foto, identificação e esquema correspondente | Não classificar automaticamente solenoide/afogador como sensor |

O circuito do aviso de óleo deve preservar a indicação original e considerar
falhas de fio. Uma entrada aberta pode significar chave aberta **ou fio rompido**;
não apresentar isso sozinho como prova de pressão normal. Sem transdutor
adicional e calibração, não exibir um número de pressão em bar.

Não substituir a lâmpada/circuito de excitação de carga por um GPIO. Definir
entrada de alta impedância e proteção conforme a configuração real.
Pulsos de ignição só podem chegar ao ESP32 após condicionamento adequado.

## Medições pedidas que exigem solução própria

| Medição | Decisão necessária |
| --- | --- |
| Temperatura do bloco | Confirmar se já há adaptação; caso contrário escolher sensor adicional e ponto de instalação |
| Velocidade da gaiola | Identificar eventual sensor existente; caso contrário sensor adicional de roda/eixo ou outra abordagem a decidir |
| Pressão de óleo numérica, se desejada depois | Transdutor adicional apropriado, preservando o aviso de baixa pressão |

Não presumir termômetro eletrônico original de bloco nem sensor eletrônico de
velocidade no conjunto informado. RPM é uma medição a construir a partir do
sinal disponível, não prova de que havia um conta-giros/sensor dedicado de fábrica.
Sensores de tanque/carroceria não entram automaticamente no inventário do motor.

## Injeção monoponto futura

Reservar uma fase própria para selecionar corpo monoponto, injetor, bomba,
regulagem de combustível, sensores e estratégia de sincronismo. Avaliar
MAP/TPS, temperatura do ar/motor, lambda e referência de rotação conforme o
projeto escolhido; não alegar que já existem no motor carburado.
Nenhum mapa, pressão de combustível ou acionamento será definido por suposição.

## Fundamentação e limites das fontes

O [manual original VW Brasil 1982, p. 24–25](https://www.volkspage.net/technik/manuaisecatalogos/02/manualdoproprietario_vwfusca1982.pdf#page=15),
preservado como digitalização, descreve avisos de pressão de óleo e dínamo/
arrefecimento; a correia também move a ventoinha. Isso não torna o aviso de carga
um termômetro. É uma referência de época, não confirmação do ano deste motor.

O [catálogo oficial VW Classic Parts](https://www.volkswagen-classic-parts.com/catalog/en_de/part/021919081D)
exemplifica um interruptor de óleo de um pino. Código e limiar desse catálogo
não foram adotados para o exemplar do responsável sem identificar a peça.

O [diagrama original VWoA 1971/72](https://www.vw1302.de/docs/WiringDiagram71-72.pdf)
mostra que havia diferenças de mercado, inclusive sensor de diagnóstico de
ignição em certas variantes. Não concluir que todo carburado tem a mesma lista.
O [catálogo original Type 1, volume 2](https://www.thesamba.com/vw/archives/manuals/parts_list_type_1/Type%201%20Parts%20List%20Vol%202.pdf)
documenta cabo de velocímetro, reforçando a distinção entre instrumentação
mecânica do veículo e sensores elétricos do motor. Essas fontes não identificam
as peças efetivamente instaladas na gaiola.

## Critério para encerrar o inventário

Registrar peça instalada, função, faixa/estado elétrico, circuito de entrada,
calibração e diagnóstico de falhas de cada canal. Ano desconhecido não impede
um levantamento físico; impede afirmar uma lista universal de fábrica.
