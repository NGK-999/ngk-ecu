# Cockpit NGK ECU — revisão de composição

Fonte: responsável rejeitou visual0.2.0 como insuficiente e deu liberdade para
pesquisar e escolher melhor solução. Confirmou toque funcional nesta etapa.
A decisão visual abaixo é proposta técnica executada sob essa autonomia,
não preferência adicional atribuída ao responsável.

## Direção

Composição contínua e assimétrica: rotação numérica no alto com barra graduada,
velocidade maior no quadrante inferior direito, temperatura/óleo à esquerda.
Fonte numérica condensada própria derivada de Barlow Condensed Bold/OFL em
64/72 px, glifos somente numéricos; branco sobre grafite. Ciano nas ações,
âmbar/vermelho nas faixas de RPM. Nenhum logotipo FuelTech ou sensor inventado.

Três abordagens consideradas: manter cartões iguais (rejeitada por hierarquia
fraca), mostradores circulares (menos área útil e maior desenho), painel plano
assimétrico (escolhido por leitura e aproveitamento da tela320 × 240). Inspiração:
[FuelTech](https://www.fueltech.com.br/products/ft450-injecao-eletronica),
[AiM MXm/MXq](https://aim-sportline.com/download/doc/eng/mxm-mxq/MXm-MXq_user_guide_eng.pdf),
[MoTeC D153](https://www.motec.com.au/products/D153).
Coordenadas e decisões de cor são próprias, não exigências dessas marcas.

## Navegação

Menu com Painel em destaque e três ações secundárias Sensores/Ajustes/Sistema.
Painel sem navegação fixa; manter duplo toque para menu. Ajustes abre índice
com seis categorias: Tela, Instrumentos, Toque/DEMO, Perfis SD, Wi-Fi e BLE.
Instrumentos contém unidades e escala RPM. Edição visual tem Salvar; ações
imediatas de SD/radio/toque não exibem Salvar global. Preservar rascunhos e
avisos assíncronos, sem introduzir novo formato de perfil ou API de rádio.

## Restrições e verificação

Alvos de 44 px, nada de scroll obrigatório, sombras/glass ou imagens no hot path.
Valores reais continuam --/SEM SINAL; demonstração explícita. Não habilitar
sensores/atuadores, reparticionar, formatar SD ou alterar pinagem/calibração.
Rádios off no boot; limitação de memória observada permanece explícita.

Renderizar código real em host antes de gravar; conferir0/99/100 e
800/999/1000/12000, ausência de recorte e retorno ao estado inicial sem rastro.
Verificar índice/volta/salvar, diálogos e teclados com pool LVGL 32 KiB. Repetir
12 testes nativos e testes de fonte, builds normal/manutenção/benchmark,
medir últimaDMA no alvo com mesma cena e rampa17 ms. Não prometer60FPS ópticos.
