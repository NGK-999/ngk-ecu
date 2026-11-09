# Revisão visual da tela ESP32

Pedido: funcionamento aprovado; melhorar distribuição e cores com preto,
vermelho e laranja, combinando referência FuelTech e organização Apple.
Responsável confirmou: somente ESP32; simulador Linux permanece como está.

Composição adotada pelo implementador: RPM/medidor horizontal no topo,
velocidade e marcha no centro, bloco/óleo alinhados na base, rodapé de
alerta/estado preservado. Menu com quatro cartões 2×2. Preto predominante,
superfícies neutras, branco nos valores, laranja na interação/identidade e
vermelho em alertas críticos. Fontes existentes e subconjunto mínimo para N/R.

Direção adicional do responsável: priorizar máxima fluidez percebida.
Acrescentar interpolação curta de RPM/velocidade, cadência racional próxima de
60 Hz e microanimação local dos ícones. Reduzir movimento cancela efeitos.
Validade, TTL e alarmes continuam derivados do dado bruto, com atualização
imediata; a animação não passa para logs, aquisição ou qualquer controle.
Preservar o benchmark anterior e medir o caminho de animação separadamente.

Alternativas consideradas: mera troca de paleta manteria a distribuição que
motivou o pedido; um mosaico denso ou efeitos de vidro consumiriam espaço e
recursos da tela 320×240. A composição simples mantém os dados grandes e os
agrupamentos claros, sem efeitos custosos ou cópia de marcas/ativos.

Referências primárias: manual oficial FuelTech FT450/550/600, página 20,
https://files.fueltech.com.br/manual/Portugues/FT450_FT550_FT600.pdf#page=20 ;
Apple HIG Layout, Color e Gauges em https://developer.apple.com/design/human-interface-guidelines/ .

1. [x] Tema e layout LVGL: preservar destinos, alvos ≥44 px, gesto/calibração,
   ajustes, fonte SIM e validade/alertas. Adequar testes de geometria aos
   contratos funcionais, retirando somente a composição anterior fixada.
2. [x] Fonte de marcha: gerar somente -NR01234 a partir da Barlow já fixada,
   conferir caracteres/limites com leitor real LVGL e reprodução offline.
3. [x] Renderizar o código LVGL em host para menu, painel, extremos, alertas,
   stale e telas secundárias. Conferir legibilidade/sobreposições e memória.
4. [x] Executar testes nativos/fontes/simulador e compilar normal/manutenção/
   benchmark. Revisar mudanças e instalar aplicação normal na placa já
   autorizada. Não alterar protocolo, modelo, sensores, SD ou permissões.
5. [x] Documentar capturas/limites/recursos, publicar código revisado e atualizar
   memória com leitura de volta. Medição de fluidez só se ensaio for executado.

Resultado: versão 0.4.2 instalada e verificada, barra contínua com movimento Q8,
modo Suave aplicado, números cerca de 30 Hz e borda até 60 Hz. Observação física:
600 apresentações parciais/10 s; p95 da rampa até 22 ms. Relatório completo em
[validacao.md](../../validacao.md#cockpit-preto-e-rampa-contínua--042).
Testes físicos não habilitam entradas/atuação e não validam operação no veículo.
