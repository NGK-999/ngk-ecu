# Simulação de motor e alertas — plano

Objetivo: melhorar a demonstração sintética para um boxer 1300 carburado e
mostrar rotação alta no Linux e na ESP32, sem habilitar atuação.

Decisões de implementação: manter Python/Qt e o protocolo SIM1 existentes;
modelo automático limitado a uma faixa ilustrativa e modo manual até 12.000 RPM
para testar anomalias. A escala do instrumento não define o limite do motor.
Alertas de bancada: atenção em 4.000 RPM, crítico em 4.500 RPM, histerese de
150 RPM. Estes números não são especificação de segurança do motor instalado.

Temperatura pretendida pelo responsável: superfície do bloco. Posição exata,
fixação, sensor e calibração ainda não definidos. O modelo térmico é uma
animação física simplificada, não uma tabela de temperaturas normais do Fusca.
200 °C permanece disponível somente como extremo de entrada manual; não é
alvo da condução automática nem declaração de temperatura aceitável.

Preferir modelo determinístico com oscilação suave a ruído aleatório por frame,
para manter transientes reproduzíveis e testes úteis. Preservar modo manual
exato, validade por canal, prioridade de óleo/temperatura e limpeza ao vencer TTL.

1. [x] Modelo Python: escrever testes de lenta variável, motor desligado,
   teto automático/manual 9.000, inércia e aquecimento/resfriamento; observar
   falha antes de implementar em `model.py`. Validar sem serial física.
2. [x] Classificadores de alerta Python/C++: testes de limiar, histerese,
   dado inválido e TTL; ligar cores/texto no Qt e no LVGL. Nenhum novo bit ou
   comando de atuação. `simulation_view.hpp` mantém compatibilidade de chamada.
3. [x] Controles Qt: opção temperatura automática e cenários lenta fria,
   lenta quente e giro alto. Manual conserva 9.000 RPM para teste. Conferir
   janela mínima com testes Qt em memória e inspeção de captura local.
4. [x] Verificar `sh scripts/test-simulator.sh`, `sh scripts/test-native.sh` e
   compilar os três ambientes PlatformIO. Revisar validade, prioridades,
   extremos e transições antes de publicar. Bancada física é etapa separada.
5. [x] Documentar fontes primárias, limites do modelo e resultado observado;
   instalar versão normal na placa autorizada, reabrir app atualizado e
   atualizar memória com leitura de volta. Não formatar SD nem alterar NVS.
