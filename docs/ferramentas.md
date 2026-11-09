# Ferramentas de investigação

## REA — Reverse Engineer Anything

Fonte oficial: [morluto/rea](https://github.com/morluto/rea).
Pacote publicado: `rea-agents`. Versão escolhida: **6.8.0**, fixada no projeto.
Instalação local em `tools/rea`, sem modificar configuração global do Codex,
Node ou Python. Runtime **Node 24.22.1**, Linux x64, mantido em `.tools/` e
ignorado pelo Git. Arquivo oficial conferido contra `SHASUMS256.txt` do Node:
`6c172a9ab6718f7b801fb5a7bd486af2ee5e32758086a04b84fcce2aa77095e6`.
Isso evita mudar o Node 24.21.0 existente, que não atende ao mínimo do REA.

Verificar a instalação atual a partir da raiz deste repositório:

```sh
sh tools/rea/run --version
sh tools/rea/run --help
```

O script é específico para este runtime Linux x64. A instalação de dependências
usou `--ignore-scripts --no-audit --no-fund`; provedores externos e integração
MCP global não foram configurados. `package-lock.json` registra o pacote e
suas dependências. A CLI pode ser chamada nesta sessão sem reiniciar o agente.
Não executar `rea setup` sem revisar o destino: o setup de Codex desta versão
altera configuração pessoal, não apenas este projeto.

Importante distinguir versão publicada e desenvolvimento: a **6.8.0 não possui
os leitores ESP dedicados** descritos no `main` do projeto. Seu conjunto de
arquiteturas também não inclui Xtensa, usado no ESP32 clássico desta família.
Não se espera descompilação nativa do firmware desta placa só com essa instalação.
O fluxo genérico de firmware depende de ferramentas externas, como Binwalk ou
Unblob, que não são instaladas automaticamente e não foram instaladas aqui.

Os leitores de
[imagens ESP no desenvolvimento](https://github.com/morluto/rea/blob/main/docs/esp-image.md)
e [dumps de flash](https://github.com/morluto/rea/blob/main/docs/esp-flash-image.md)
não são drivers de USB/serial nem um mapeador automático dos GPIOs. Mesmo nessa
documentação, inventário de segmentos não equivale a descompilação Xtensa.
A análise desta revisão usou o ELF compilado do próprio projeto; o backup
extraído da placa permanece privado e não foi usado como fonte do produto.

Referência da versão instalada:
[release rea-agents-6.8.0](https://github.com/morluto/rea/tree/rea-agents-6.8.0),
commit `160d821ac9a0310afcb8d1fc427862dd6508add5`.

## REA aplicado ao projeto

REA 6.8.0 executou `inspect-with-objdump` com `section_headers` e
`inspect-elf-notes` no ELF da base instrumentada própria. Resultado completo,
exit code 0 e `artifact_changed=false`; ELF32 little-endian, máquina Xtensa.
SHA-256 do ELF analisado:
`beae4c6e760726d96b6ef8d84cfb696c82e6d94199e7991e179152b6c58f1299`.
O tamanho do ELF com símbolos de debug não é o tamanho ocupado na flash.

```sh
sh tools/rea/run inspect-with-objdump \
  firmware/ecu/.pio/build/ecu-normal/firmware.elf section_headers \
  --format json \
  --filter-output evidence_id,subject.digest,provider,parameters.operation,raw_result.exit_code,raw_result.artifact_changed,raw_result.completion_status
```

O fluxo `inspect-artifact` recusou a arquitetura; `inspect-binary-layout`
exigia `pwntools`, não instalado. Não houve descompilação Xtensa. A inspeção
foi complementada pelo map e ferramentas Xtensa do próprio toolchain: as cinco
fontes completas somavam 174.994 bytes constantes. Isso orientou a redução da
fonte dos instrumentos, com testes de equivalência dos glifos. Instalar REA
não acelera o firmware; sua utilidade aqui foi inspecionar e fundamentar a mudança.

## esptool

Ferramenta oficial Espressif instalada em `.venv`, com Python 3.12,
versão **5.6.0**. Dependências fixadas em `tools/requirements-hardware.lock`.
O mesmo lock agora inclui **PlatformIO 6.1.18**. O cache da plataforma e dos
compiladores fica em `.tools/platformio`, selecionado por `PLATFORMIO_CORE_DIR`,
sem mudar a configuração global. O conjunto usa `espressif32@6.9.0` e
Arduino-ESP32 2.0.17 para reproduzir os drivers da referência.

```sh
.venv/bin/python -m esptool version
```

O sistema não tinha `ensurepip` para Python 3.12. Foi usado o
[zipapp oficial do pip](https://pip.pypa.io/en/stable/installation/#standalone-zip-application)
apenas dentro da `.venv`, sem instalar pacotes no Python do sistema.
Para reproduzir neste Linux com Python 3.12 instalado:

```sh
python3.12 -m venv --without-pip .venv
mkdir -p .tools
curl --fail --location https://bootstrap.pypa.io/pip/pip.pyz -o .tools/pip.pyz
.venv/bin/python .tools/pip.pyz install -r tools/requirements-hardware.lock
```

## Reproduzir o REA em outra máquina Linux x64

1. Obter `node-v24.22.1-linux-x64.tar.xz` em
   [nodejs.org](https://nodejs.org/dist/v24.22.1/).
2. Conferir o SHA-256 com o arquivo `SHASUMS256.txt` dessa distribuição.
3. Extrair em `.tools/`, preservando o diretório `node-v24.22.1-linux-x64`.
4. Executar na raiz do projeto:

```sh
.tools/node-v24.22.1-linux-x64/bin/node \
  .tools/node-v24.22.1-linux-x64/lib/node_modules/npm/bin/npm-cli.js \
  ci --prefix tools/rea --ignore-scripts --no-audit --no-fund
```

## Privacidade dos artefatos

Dumps, partições NVS, relatórios completos e dados do cartão podem conter
credenciais e identificadores. Guardar somente localmente, em diretórios
ignorados. Publicar apenas conclusões sanitizadas e hashes apropriados.

## Verificações separadas

O estado inicial de falta de acesso serial foi resolvido pelo operador.
A identificação USB, backup, manutenção SD e versão anterior estão registrados
em [validacao.md](validacao.md). A revisão atual separa testes locais,
inspeção visual, ensaio de desempenho na placa e operação no veículo.

REA também repetiu `section_headers` no ELF final da revisão0.2.0:
exit0,complete,artifact_changed=false; SHA-256
`bea2ea9fccae4286641a0da865973496180948600237ca021435f3dec60d713c`.
