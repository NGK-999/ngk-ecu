# NGK ECU

Responda em português brasileiro. Preserve alterações e arquivos existentes.
Este repositório é independente do monitor de sinais musculares InterfaceNeural.

## Escopo

V1: painel ESP32 com tela touch para RPM, temperatura do bloco e velocidade de
uma gaiola com motor boxer VW Fusca. Ainda não há controle de injeção/ignição.
Não introduzir saídas de atuação sem uma nova etapa de projeto e validação.

## Hardware e dados

- Confirmar revisão da placa/esquema e conflitos antes de atribuir GPIOs.
- Testes devem usar fontes sintéticas; não abrir serial nem formatar SD em testes.
- A autorização de formato vale somente para o cartão identificado; não escolher
  disco por tamanho nem formatar automaticamente em falha de montagem.
- Não alterar grupos/permissões, Python/Node do sistema ou configuração global.
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
