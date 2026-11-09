# Interface automobilística, conexões e fluidez

## Pedido

Referências visuais fornecidas: painéis FuelTech com fundo preto, números
brancos grandes, faixas de RPM amarelas/vermelhas e grade de instrumentos.
Refinar menus/configurações preservando simplicidade durante a condução.
Prioridade: fluidez. Incluir Wi-Fi e Bluetooth agora, conforme resposta explícita.
Investigar com REA e explicar o uso real do SD e da flash de 4 MiB.

Preservar menu inicial, modo painel sem dock e retorno por duplo toque.
Motor 1300 carburado, injeção monoponto futura. Não criar saídas de atuação,
pressão numérica de óleo ou leituras reais sem sensores. Sem logos da referência.

## Abordagens e decisão técnica desta revisão

Manter instrumentos vetoriais e dígitos críticos na flash, com DMA em RAM
interna e atualizações parciais. Recursos opcionais/perfis podem ficar no SD.
Carregar fontes grandes pelo SD a cada desenho aumenta dependência de I/O;
framebuffer integral e animações de tela inteira consomem RAM/banda sem atender
à prioridade. Não aumentar SPI além dos 40 MHz verificados nem alterar partições.

Primeiro medir a base com uma cena sintética identificada como DEMO; depois
repetir a mesma carga no visual novo, com rádios desligados e ligados.
Medir apresentações concluídas após a última DMA, p95 de tempo, intervalos do
touch e memória. Não apresentar essas métricas como latência óptica ou quadros
completos/s. Não transformar testes sintéticos em validação de sensores.

## Interface e configurações

Menu inicial mantém Painel, Sensores, Ajustes e Sistema. Painel: faixa RPM
superior segmentada, valores grandes de velocidade/RPM, bloco e óleo abaixo.
Fundo preto/grafite, texto branco, destaque âmbar e vermelho para estados de
alerta. Sem sinal produz barra neutra e valores indisponíveis.

Ajustes por páginas com botões grandes: tela/brilho/cadência, unidades,
escala visual de RPM, calibração, demonstração, perfis no SD e conexões.
Não encher o painel de opções. Limites de RPM nesta fase são referências
visuais, sem corte de giro ou proteção do motor. DEMO é desligado a cada boot.

Configurações de tela validadas e versionadas; gravação somente ao aplicar,
no worker. Perfis no SD não incluem senhas, vínculos ou calibração de outra
placa. Falha/ausência de SD não impede UI nem formata o cartão.

## Wi-Fi e BLE

Inicialmente desligados; ativação explícita pela UI. Trabalho assíncrono em
worker, filas limitadas, snapshots na UI. Wi-Fi 2,4 GHz, busca com quantidade
limitada e entrada local de credenciais, sem senha no chat, logs ou perfil SD.
BLE periférico com descoberta temporária e canal restrito a metadados de
bancada. Se houver controle/comando BLE, exigir conexão autenticada; jamais
encaminhar formatação ou atuação por rádio. Vínculos e credenciais só em NVS
privado, sem alegar criptografia de flash que não foi configurada.

Verificar memória antes de habilitar recursos e indicar indisponibilidade
claramente; usar rádio não justifica travar desenho ou aquisição futura.

## Limites de SD e REA

SD não expande RAM, PSRAM ou capacidade de execução direta da aplicação.
Pode armazenar perfis, recursos opcionais, documentação e pacotes de atualização;
executar firmware continua exigindo flash interna. Não mover dígitos críticos
para o SD só para reduzir tamanho. Auditoria inicial: imagem 748.560 bytes em
slot de 1.966.080 bytes, portanto não há urgência para reparticionar.

REA 6.8.0 conseguiu seções ELF via objdump e notas Xtensa, sem mudar artefato.
Seu leitor nativo rejeita Xtensa e outro fluxo exige provider ausente.
Usar ferramentas oficiais de tamanho/MAP para complementar, sem prometer
decompilação ou otimização automática inexistentes.
