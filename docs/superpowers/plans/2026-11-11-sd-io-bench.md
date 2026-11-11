# SD I/O com painel ativo Implementation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development ou superpowers:executing-plans. Escopo autorizado: medir o SD com tela animando; escolhas abaixo são de implementação.

**Goal:** Medir latência e vazão do cartão real, incluindo sincronização, junto da cadência/toque do painel.

**Architecture:** Build de diagnóstico temporário `ecu-sd-benchmark`, com UI normal e aquisição/atuação desabilitadas. Trabalho SD no worker existente; arquivo próprio exclusivo e limitado. Cliente físico explícito mantém SIM20Hz e mede UI por10s dentro de uma fase SD15s. Restaurar imagem normal0.4.9 ao concluir.

**Tech Stack:** C++17/Arduino2.0.17/PlatformIO, LVGL8.4, POSIX VFS/FAT no SD já montado, Python3.12/.venv.

**Spec:** Recomendação anterior de medir piores pausas do cartão com animação ativa, aprovada pelo usuário com “sim faça”. Não implementar logs de sensores, temas ou mapas nesta tarefa.

## Global Constraints

- Sem formato, repartição, alteração de pinos/frequência/ACL por ferramentas, GPIO ou atuação.
- Um arquivo próprio por execução: criação exclusiva, nome fixado pelo firmware com nonce, tamanho máximo1MiB; apagar somente arquivo criado por esta execução. Nunca sobrescrever arquivo existente.
- Conteúdo artificial determinístico, sem amostras/serial bruto/identificadores/credenciais em arquivos públicos ou memória.
- Mesmo worker é dono do SD; UI não faz I/O. Build normal sem comando/buffer de diagnóstico.
- Timeout global60s por trabalho, pausas cooperativas por bloco, snapshot limitado; uma chamada individual do driver pode exceder o prazo e será relatada, sem prometer preempção.
- Guardar e restaurar aplicativo normal somente em0x10000, hash verificado, preservando NVS/calibração/partições.
- Acesso USB depende da ACL temporária concedida pelo operador; preparar software enquanto indisponível.

## Review Focus

- Criação exclusiva falhou: não remover nem truncar arquivo preexistente.
- I/O curto, erro de seek/sync/close e corrupção: resultado reprovado, limpeza própria tentada e estado explícito.
- Fila cheia/repetição: recusar; resultados e id correlacionados, sem trocar SD durante operação.
- UI medição deve caber inteiramente na fase SD ativa, com SIM viva e sem captura simultânea.
- Histogramas são limitados e p95 é limite superior; distinguir sync do driver de durabilidade garantida após energia cair.

## Task 1 — Motor de ensaio portátil

- [x] Criar `firmware/ecu/include/sd_io_benchmark.hpp` e `tests/native/test_sd_io_benchmark.cpp`; testes primeiro, sem hardware.
- [x] Namespace `ecu::sdbench`: `enum class Mode { Write512, Write4096, Batch4096, Read4096 }`; `Metrics {uint32_t operations,maxUs,p95UpperUs; uint64_t bytes,totalUs;}`; `Result {bool okay,created,removed; uint32_t error,setupMs,durationMs; Metrics read,write,sync;}`.
- [x] `template<class Backend> Result run(Backend&,Mode,uint32_t durationMs=15000)`; backend possui `createExclusive()`, `write(const uint8_t*,size_t)->size_t`, `read(uint8_t*,size_t)->size_t`, `seek(uint32_t)->bool`, `sync()->bool`, `close()->bool`, `removeOwn()->bool`, `nowUs()->uint64_t`, `yield()`, `measurementStarted()`, `measurementFinished()`.
- [x] Buffer4KiB limitado em run; preparar1MiB determinístico com blocos4KiB, sincronizar, seek0, então chamar measurementStarted. Medir fase15s, seek0 no limite1MiB. Read valida cada byte; writes usam o mesmo padrão por offset. Write512/Write4096 sincronizam cada bloco; Batch4096 sincroniza cada64KiB e ao fim. Separar estatísticas de setup, read/write e sync. Timeout60s inclui setup; erro interrompe. Nada é alocado dinamicamente pelo motor.
- [x] Métricas usam histograma64bins de1ms (último overflow usa máximo observado como limite superior); sem guardar duração por operação. Cobrir todas as falhas, limpeza, limite de tamanho e timeout em transporte de arquivo em memória.

## Task 2 — Integração de diagnóstico e cliente

- [x] `NGK_SD_IO_BENCH` somente no novo ambiente. Versão diagnóstica `0.4.9-sd-io-bench`, protocolo1 eminfo. Normal continua0.4.9-layout-preview.
- [x] `sd-io-bench <sectors> <id> <mode1..4>` aceita somente SIM viva, perfil/SD prontos, rádios off e geometria atual igual; pedido/fila únicos e snapshot com id/state/result. `sd-io-status` consulta, sem disparar escrita.
- [x] Adaptador POSIX em `firmware/ecu/src/sd_io_service.cpp`, cabeçalho `sd_io_service.hpp`: `begin()`, `request(sectors,id,mode)`, `snapshot()`, `workerPoll(mounted,sectors,spi,identity)`. Criação O_CREAT|O_EXCL, fsync com erro observado, /sdcard; nomes próprios. Sem montagem/formatação pelo serviço.
- [x] `tools/benchmark_sd.py` só abre USB com `--execute-physical-bench`; identifica versão/build/flags/cartão esperado, mantém SIM20Hz, mede baseline e quatro modos; confirma fase ativa durante UI10s, integra resultados e guarda só agregados. Restaura SIM/radios e não altera configurações persistentes.

## Task 3 — Evidência e entrega

- [x] Revisões independentes do motor/integração; testes nativos ASan/UBSan, builds normal/diagnóstico/manutenção e auditoria de pinos.
- [ ] ACL, identificação da placa/cartão e backup normal. Gravar diagnóstico, executar baseline/casos, conferir arquivo removido e integridade, restaurar normal e verificar hash/estado.
- [ ] Se a USB permanecer inacessível, registrar claramente o bloqueio e preservar ferramentas prontas; não inventar medições.
- [ ] Documentação com agregados, limites e recomendação baseada na medição; publicação na branch atual e AI Memory com leitura de volta.

## Decisões após revisão prévia

- Comparação de relógios no próprio dispositivo: SD publica início/fim da fase;
  main publica timestamps que encerram a janela UI. Cliente prova inclusão integral.
- Escrita medida usa gerações diferentes do preparo; leitura final confere conteúdo.
- CID conferido antes/depois somente em RAM no diagnóstico, sem publicar identidade.
- Stack diagnóstico12KiB, normal8KiB; medir high-water e manter buffers fora do build normal.
- Cliente exige modo Suave já aplicado e rádios desligados; não altera NVS.
- Vazão efetiva usa bytes/tempo de parede da fase, incluindo sync e pausas cooperativas.
- USB está presente sem ACL; pedido assíncrono de liberação enviado ao operador.

## Revisão e critérios adicionais do cliente

- Revisão C++ aprovou escopo e qualidade, sem falha material; testes nativos 18,
  builds normal/manutenção/diagnóstico e auditoria passaram.
- Revisão cliente exigiu drenagem também após ACK perdido/exceção inesperada,
  estado explícito de cleanup desconhecido, operações efetivas positivas e
  fase SD de pelo menos 15 s. Correções e regressões concluídas pelo autor, incluindo testes unittest canônicos.
- Fonte alvo 20 Hz: orçamento do ensaio, escolhido pelo controlador, de 19–21 Hz
  médios e lacuna máxima 100 ms. Fora dele não aprovar comparação.
- Antes da base, status SD deve estar idle ou terminado com sucesso/limpeza;
  um trabalho anterior ativo contaminaria a base.

## Checkpoint final de software

18 testes nativos e 86 testes do runner canônico passaram. Revisões C++/cliente
aprovaram escopo/qualidade após correções. Ctrl-C é cancelamento explícito do
cliente: não espera o worker, registra resultado incompleto/cleanup desconhecido;
consultar estado antes de reset/retirada do cartão. Limite documentado.

Sem acesso USB até este checkpoint; nenhuma medição física ou flash realizada.
Resta executar bancada após liberação do operador e então registrar resultados.
