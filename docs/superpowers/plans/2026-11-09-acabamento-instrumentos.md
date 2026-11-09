# Acabamento dos instrumentos — plano de implementação

> Execução autorizada pelo responsável com “faça”, após a proposta de três
> refinamentos. Aplicar writing-plans, implementação com tarefas independentes
> delegadas e verification-before-completion; revisão independente ao final.

**Objetivo:** ícones consistentes, alinhamento estável e faixa de alerta mais
legível, somente na ESP32. Base: `7999615`, normal `0.4.2-cockpit`.

**Arquitetura:** preservar LVGL 8.4, desenho parcial/DMA, buffers estáticos e
cadência da rampa. Ícones Lucide convertidos offline em alpha4 constante;
números continuam com largura do conteúdo, ancorados pela direita, evitando
invalidar caixas inteiras. Faixa inferior de 22 px, ícone de 20 px e texto14.

**Especificação aprovada:** três prioridades da pesquisa anterior: padronizar
ícones/botões; fixar referências dos números/unidades; destacar avisos com texto
curto e símbolo. Tema preto/laranja/vermelho, barra sólida, menu2×2 e gesto
continuam. A escolha das dimensões e conversão é do implementador.

## Restrições e revisão

- Não modificar simulador Linux, protocolo SIM, prioridades/limiares, aquisição,
  atuadores, GPIOs, calibração, partições ou permissões.
- Não adicionar runtime de SVG, Flow, fonte inteira, canvas ou framebuffer.
- Cobrir máximo/zero/ausência, Fahrenheit/mph, todos os 10 alertas + estado normal, TTL/modal e
  alinhamento após mudar quantidade de dígitos, sem clipping real dos textos.
- Validar licenças/proveniência, flags de toque dos ícones e tintas alpha4.
- Medição física separada de testes host; eventos DMA não são FPS ópticos.

## Tarefas

1. [x] Criar cinco ícones pinados e gerador offline, licença e teste com decoder
   LVGL real. Arquivos novos em tools/icons, third_party/icons, ui_icons.h/c.
2. [x] Observar teste host falhar para âncoras/faixa; implementar em ui.cpp e
   ui_layout.hpp, testar textos/estados/cores/limites. Versão0.4.3-cockpit.
3. [x] Executar testes nativos/fontes/simulador, reprodução dos ícones, render
   LVGL host e três builds; revisar o diff, corrigir problemas concretos.
4. [x] Identificar placa, instalar somente app normal, comparar bench/motion,
   restaurar simulador aberto; atualizar imagens/docs, publicar e ler memória.

EEZ/SquareLine permanecem ferramentas possíveis de protótipo; instalar um editor
não faz parte das três mudanças aprovadas nem substitui os testes do código.
A implementação aproveita o renderer local e as ferramentas existentes.

Resultado: implementação, testes, revisão, instalação e medição concluídos.
Evidências e limites em [validação](../../validacao.md#acabamento-dos-instrumentos--043).
