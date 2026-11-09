# NGK ECU

Responda em português brasileiro. Preserve alterações e arquivos existentes.
Este repositório é independente do monitor de sinais musculares InterfaceNeural.

## Escopo

V1: painel ESP32 com tela touch para RPM, temperatura do bloco, velocidade e
todos os sensores/sinais originais efetivamente presentes no motor boxer VW
Fusca **1300 carburado**, ano desconhecido, instalado em uma gaiola.
Consultar `docs/sensores-motor.md`: não inventar sensores de fábrica nem
converter interruptor binário em pressão numérica. Futuro: injeção **monoponto**.
Ainda não há controle de injeção/ignição.
Não introduzir saídas de atuação sem uma nova etapa de projeto e validação.

## Hardware e dados

- Confirmar revisão da placa/esquema e conflitos antes de atribuir GPIOs.
- Testes devem usar fontes sintéticas; não abrir serial nem formatar SD em testes.
- A autorização de formato vale somente para o cartão identificado; não escolher
  disco por tamanho nem formatar automaticamente em falha de montagem.
- O responsável autorizou substituir o firmware e preparar/formatar o SD pela
  própria placa. Confirmar alvo e preservar backup local se a leitura for possível;
  não exigir leitor externo nem uma nova autorização genérica para essa operação.
- Acesso temporário à porta serial foi solicitado. Orientar ACL somente para o
  usuário/dispositivo em questão; não usar chmod 777/666 ou alterar grupos.
  Não alterar Python/Node do sistema ou configuração global de agentes.
- Não publicar firmware extraído, NVS, credenciais, MAC, dados brutos ou fotos
  com conteúdo alheio ao hardware. Artefatos de análise ficam locais e ignorados.

## Validação

Documentar separadamente sistema, execução local, inspeção visual, bancada e
veículo. Não apresentar teste sintético como validação de hardware. Nunca
ligar tensão automotiva ou ignição diretamente ao GPIO.

## Ferramentas e memória

Instalar ferramentas isoladamente neste projeto. Se Python for necessário,
usar Python 3.12 com `.venv`. REA usa runtime local compatível e dependência fixada.
Consultar e atualizar AI Memory com `workspace` e `project` explícitos de
`.ai-memory.toml`; ler de volta páginas gravadas. Não registrar dados brutos,
segredos, caminhos pessoais ou identificadores de hardware.

Publicação deste novo repositório e documentação inicial foi solicitada.
Não publicar dumps ou resultados experimentais não revisados junto com código.

## Interface

Referência de tema e interação em `docs/interface.md`, baseada na orientação e
nos arquivos karl-panel indicados pelo responsável. Preferir cliques, alvos
grandes e paginação. Manter aquisição independente do LVGL, SD e animações.

Pedido posterior explícito: abrir um menu com botões grandes e entrar num modo
painel de carro, com velocidade/RPM grandes e temperatura/óleo visíveis. Sem
barra fixa de navegação no modo painel. Duplo toque no painel retorna ao menu.
Essa exceção de gesto foi solicitada pelo responsável e não deve ser removida
por diretrizes antigas de navegação exclusivamente por cliques simples.

## Simulador Linux

`simulator/ngk_simulator/`: modelo e codec puros em `model.py`/`protocol.py`,
Qt somente em `window.py`, serial somente no worker `link.py` com transporte
injetável e snapshot mais recente. App começa offline; abrir não conecta a USB.
`sh scripts/test-simulator.sh` executa Qt offscreen e transporte em memória;
nenhum teste automatizado abre porta física. BancadaUSB é etapa separada.

Fonte USB é sempre identificada como SIM USB no firmware; TTL750ms retira
valores/alertas antigos. Nenhum frame/controlador do simulador habilita GPIO,
sensor físico, atuador, injeção ou ignição. Não persistir amostras/serial bruto.
Atalho é do usuário local; dependências ficam na .venv, sem instalação no sistema.
