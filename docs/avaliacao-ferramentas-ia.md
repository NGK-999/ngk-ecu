# Ferramentas de assistência ao desenvolvimento ESP32

Avaliação de 2026-11-09. Objetivo atual: ajudar a programar a NGK ECU e investigar
CPU, memória e fluidez. Não foi definida uma função de classificação TinyML ou
uma interação com API de IA no produto. A avaliação não modifica o firmware,
os ambientes PlatformIO, o alvo ESP32, as permissões USB ou a configuração MCP
global.

## Resultado dos quatro repositórios solicitados

| Repositório e revisão fixada | Uso nesta etapa | Licença observada |
| --- | --- | --- |
| [ESP32-AI-Agent-Skill](https://github.com/ezrover/ESP32-AI-Agent-Skill/tree/124d298e4ccaa737dbf6d26bbae47722e17f3097) — `124d298e4ccaa737dbf6d26bbae47722e17f3097` | Skill local e auditoria de sinais; instalação e limites em [ferramentas-esp32.md](ferramentas-esp32.md). | MIT |
| [esp-mcp](https://github.com/horw/esp-mcp/tree/28afddf9ff41ada117bb45fd49b526d3bf89749e) — `28afddf9ff41ada117bb45fd49b526d3bf89749e` | Fonte e runtime isolados; protocolo stdio e schemas verificados. Operações ESP-IDF não exercitadas. | Nenhuma licença explícita encontrada na árvore, README ou `pyproject.toml` inspecionados. Fonte mantida somente em `.tools`, sem vendorização publicada. |
| [tflite-esp-template](https://github.com/itemis/tflite-esp-template/tree/288e932169904cf8a5d35e5d8984d84ae2261190) — `288e932169904cf8a5d35e5d8984d84ae2261190` | Referência da arquitetura de inferência; sem instalar TensorFlow, submódulos ou componentes no painel. | [Apache-2.0](https://github.com/itemis/tflite-esp-template/blob/288e932169904cf8a5d35e5d8984d84ae2261190/LICENSE.md) |
| [ESP32_AI_Connect](https://github.com/AvantMaker/ESP32_AI_Connect/tree/33730e8edfa5dcfa9f7355a832eb837008f130d8) — `33730e8edfa5dcfa9f7355a832eb837008f130d8` | Biblioteca Arduino 0.5.16 avaliada como cliente de APIs; sem integração, credenciais ou chamadas de IA. | [MIT](https://github.com/AvantMaker/ESP32_AI_Connect/blob/33730e8edfa5dcfa9f7355a832eb837008f130d8/LICENSE) |

Usar essas referências não demonstra ganho de desempenho do painel. O MCP
executa tarefas no computador; TinyML acrescenta inferência; AI Connect
acrescenta comunicação HTTP/JSON. Melhoria de fluidez precisa ser conferida no
firmware próprio, com medições de apresentação, latência de toque e memória.

## esp-mcp: capacidades conferidas no código

O [README fixado](https://github.com/horw/esp-mcp/blob/28afddf9ff41ada117bb45fd49b526d3bf89749e/README.md)
classifica o projeto como prova de conceito para ESP-IDF. O
[servidor](https://github.com/horw/esp-mcp/blob/28afddf9ff41ada117bb45fd49b526d3bf89749e/main.py)
registra exatamente sete ferramentas:

| Ferramenta | Ação implementada |
| --- | --- |
| `build_esp_project` | Executa `idf.py build`; aceita caminho do IDF e defaults de sdkconfig. |
| `create_esp_project` | Cria diretório e chama `idf.py create-project`. |
| `setup_project_esp_target` | Executa `idf.py set-target`; pode alterar a configuração do alvo. |
| `flash_esp_project` | Executa `idf.py flash`, com porta opcional. |
| `list_esp_serial_ports` | Executa `python -m serial.tools.list_ports`; não implementa identificação do firmware. |
| `run_esp_idf_install` | Executa `install.sh` numa árvore ESP-IDF existente. |
| `run_pytest` | Executa pytest após carregar o ambiente IDF; o código não instala `pytest-embedded`. |

Não há ferramenta registrada de monitor serial, profiler, inspeção de heap,
análise de GUI ou documentação. O README menciona alguns desses itens como
evoluções, não como recursos entregues. A listagem de portas usa pyserial, e
pyserial não é declarado no `pyproject.toml` upstream; foi acrescentado somente
ao runtime isolado desta avaliação. Código conferido em
[esp_utils.py](https://github.com/horw/esp-mcp/blob/28afddf9ff41ada117bb45fd49b526d3bf89749e/esp_utils.py).

As funções operacionais montam comandos de shell, várias alteram o diretório
global do processo e escrevem arquivos `mcp-*.log`. Portanto esta revisão não
recebe configuração global nem chamadas concorrentes. Caminhos e argumentos
precisam de revisão antes de uso operacional; a interface não oferece isolamento
entre projetos. Os testes upstream invocam instalação, mudança de alvo e build;
não foram executados nesta avaliação.

### Compatibilidade com a NGK ECU

A NGK ECU usa PlatformIO, Arduino-ESP32 e seu SDK pré-compilado. O esp-mcp chama
`idf.py` diretamente; ele não traduz `platformio.ini` nem escolhe `ecu-normal`,
`ecu-maintenance` ou outros ambientes PlatformIO. O SDK Arduino disponível não
substitui uma instalação ESP-IDF completa com `export.sh` e `tools/idf.py`.

Na sessão avaliada, `IDF_PATH` não estava configurado e `idf.py` não estava no
PATH. Não foi baixado outro SDK ou alterado o framework/alvo do painel. A
instalação foi limitada ao servidor Python e suas dependências. Um futuro teste
de build por MCP deve usar projeto ESP-IDF separado e ambiente explicitamente
configurado; não executar `set-target` na árvore atual por inferência.

### Instalação local e execução limitada

Os três checkouts desta avaliação estão em `.tools/ai-tools/sources/`, com HEAD
destacado nos commits da tabela. O manifesto privado é
`.tools/ai-tools/sources.json`. Nada desse diretório integra a publicação.

O ambiente `.tools/ai-tools/esp-mcp-venv` usa Python **3.12.3**, separado da
`.venv` do projeto. Foram instalados os 26 pacotes fixados e com hashes do
`uv.lock` upstream, incluindo MCP **1.5.0**. O perfil suplementar fixa pytest
**8.3.5**, iniconfig **2.1.0**, packaging **24.2**, pluggy **1.5.0** e pyserial
**3.5**. O `pyproject.toml` pede pytest, mas essa dependência ainda não consta
do lock upstream; os complementos ficam em lock separado.

Locks privados: `.tools/ai-tools/esp-mcp-transport.lock` e
`.tools/ai-tools/esp-mcp-extra.lock`. A instalação usou hashes obrigatórios e
wheels, sem executar instaladores de SDK ou usar a `.venv` principal. São
versões escolhidas para reproduzir esta avaliação, não uma afirmação de que
sejam as versões atuais de cada dependência.

1. Repetir a verificação de transporte na instalação local preparada:

   ```sh
   sh scripts/esp-mcp-local --check
   ```

2. Se houver futuramente um projeto ESP-IDF separado e um SDK completo aprovado,
   iniciar o servidor opcional por stdio:

   ```sh
   IDF_PATH=/caminho/do/esp-idf sh scripts/esp-mcp-local --stdio
   ```

O launcher não instala dependências, não cadastra servidor em clientes MCP e
não chama ferramentas. `--stdio` inicia o servidor upstream com as sete
ferramentas; não é uma lista de permissões para executar seus efeitos. Sem SDK
completo configurado, esse modo termina com erro explícito antes de iniciá-lo.
O launcher também confere o commit e rejeita mudanças nos quatro arquivos
upstream usados pelo runtime/lock. Em outro checkout, é necessário preparar o
ambiente privado novamente; a cópia publicada não inclui esses runtimes.
O cliente ignora `PYTHONPATH`, `PYTHONHOME`, o user site e o diretório de chamada;
o servidor ignora essas variáveis e o user site, preservando sua própria pasta
auditada para importar `esp_utils.py`. O caminho IDF é convertido para absoluto
antes de qualquer mudança de diretório.

### Evidência de execução local

Em 2026-11-09:

- `pip check`: **No broken requirements found** no ambiente isolado.
- `sh -n scripts/esp-mcp-local`: passou.
- `--check`: inicialização MCP por stdio, protocolo **2024-11-05**, sete nomes e
  schemas anunciados, incluindo `project_path` obrigatório no build.
- **Zero chamadas de ferramentas**; nenhuma listagem de portas, instalação de
  IDF, alteração de alvo, compilação, flash, monitor, requisição de IA ou acesso
  a chave. Resultado sanitizado em `.tools/ai-tools/esp-mcp-validation.json`.
- `--stdio` sem `IDF_PATH`: recusado com exit code **2**, antes de iniciar o
  modo operacional.
- Controle de isolamento: um módulo `mcp.py` externo via `PYTHONPATH` causou
  falha antes da correção; com o cliente isolado, o mesmo controle não substitui
  o MCP pinado e a verificação de transporte passa.

Isso verifica importação e transporte, não a funcionalidade de build/flash nem
a compatibilidade operacional com um SDK IDF. Não é ensaio de placa, inspeção
visual, medição de CPU ou validação em veículo.

## TinyML: referência para uma etapa com modelo definido

O [template itemis](https://github.com/itemis/tflite-esp-template/blob/288e932169904cf8a5d35e5d8984d84ae2261190/README.md)
descreve coleta, preparação, treinamento, conversão e inferência local. Declara
Python 3.10 e ESP-IDF 4.4.2 como versões testadas. O
[requirements.txt](https://github.com/itemis/tflite-esp-template/blob/288e932169904cf8a5d35e5d8984d84ae2261190/requirements.txt)
não fixa versões de numpy, TensorFlow, Keras ou pandas; o script de componentes
inicializa submódulos e copia bibliotecas TFLite/ESP-NN. Não foi executado.

O [exemplo de inferência](https://github.com/itemis/tflite-esp-template/blob/288e932169904cf8a5d35e5d8984d84ae2261190/main/src/main_functions.cpp)
reserva **140 KiB** de arena estática, além do modelo e das outras estruturas.
Esse valor pertence ao exemplo, não constitui orçamento para a NGK ECU. O
painel precisa preservar memória de LVGL, buffers DMA e workers; não foi feito
ensaio de integração ou estimativa de CPU para um modelo inexistente.

A arquitetura é uma referência útil caso seja definida uma tarefa concreta de
inferência com entradas, dados e critério de avaliação. Não foi incorporada a
coleta em arquivos ou a atuação descrita pelo template. Nenhum sensor, modelo,
treinamento ou decisão automática foi inventado para preencher essa etapa.

## AI Connect: cliente de APIs executado na ESP32

O [manifesto da biblioteca](https://github.com/AvantMaker/ESP32_AI_Connect/blob/33730e8edfa5dcfa9f7355a832eb837008f130d8/library.properties)
declara Arduino/ESP32, versão **0.5.16** e dependência de ArduinoJson; o README
pede ArduinoJson 7 ou superior. Seu papel é fazer requisições de IA a serviços,
não ajudar o agente a compilar C++ nem reduzir a carga do painel.

A revisão chama [`WiFiClientSecure::setInsecure()` nos construtores](https://github.com/AvantMaker/ESP32_AI_Connect/blob/33730e8edfa5dcfa9f7355a832eb837008f130d8/src/ESP32_AI_Connect.cpp)
e habilita [debug serial, ferramentas, streaming e vários provedores por
padrão](https://github.com/AvantMaker/ESP32_AI_Connect/blob/33730e8edfa5dcfa9f7355a832eb837008f130d8/src/ESP32_AI_Connect_config.h).
O mesmo arquivo define buffers JSON de 5.120 e 2.048 bytes e timeout HTTP de
30 segundos. São custos/configurações do código, não uma medição de consumo
total ou de desempenho na placa deste projeto.

Sem uma interação de produto definida, não há endpoint, mensagem ou modelo a
integrar. A fonte foi consultada e fixada para referência; não foram instaladas
bibliotecas no firmware, solicitadas credenciais, ligadas novas conexões ou
feitas requisições. Uma integração futura precisaria definir a função, validar
TLS, evitar logs de conteúdo e manter qualquer operação de rede fora da UI e
da aquisição. Essa decisão fica separada da melhoria atual de CPU/fluidez.
