# Ensaio do SD com painel animado

Preparado a pedido do responsável em 2026-11-11. **Medição física pendente:**
o Linux retirou a ACL temporária da porta; nenhum firmware foi gravado nem
arquivo criado no cartão nesta preparação. Resultados de remount anteriores
não representam leitura/escrita contínua.

## O que o ensaio compara

| Caso | Operações da fase medida |
| --- | --- |
| Base | Animação SIM, sem trabalho SD |
| Escrita 512 | Blocos de 512 B, sincronização a cada bloco |
| Escrita 4096 | Blocos de 4 KiB, sincronização a cada bloco |
| Lotes 4096 | Blocos de 4 KiB, sincronização a cada 64 KiB e ao fim |
| Leitura 4096 | Blocos de 4 KiB, com verificação do padrão |

Cada fase SD dura 15 s; a janela de medição da UI dura 10 s inteiramente dentro
dela. Os timestamps são do mesmo dispositivo, registrados antes/depois do
intervalo da UI. O cliente reprova sobreposição incompleta, alteração de página
ou interrupção. Fonte USB sintética 20 Hz, rampa/números e toque continuam ativos.
O ensaio mede apresentações parciais e consultas de toque, sem medição óptica.
O cliente exige operações e bytes efetivos na modalidade, fase SD de pelo
menos 15 s e fonte com média de 19–21 Hz e maior intervalo de 100 ms. Esses
limites são critérios deste ensaio, sem constituir garantia de tempo real.
Antes da base, recusa um trabalho SD anterior ainda ativo ou sem limpeza
confirmada. ACK perdido ou erro de execução após o envio dispara consulta limitada ao
estado do trabalho; limpeza desconhecida é registrada como falha.

Ctrl-C interrompe o cliente e tenta encerrar SIM/porta, mas não aguarda a
conclusão do worker SD; o resultado fica reprovado e a limpeza não confirmada.
Nesse caso, consultar `sd-io-status` até o trabalho terminar antes de restaurar
firmware ou retirar o cartão. Comunicação indisponível impede confirmar a
limpeza; não interpretar a interrupção do cliente como término do trabalho.

## Preservação dos arquivos

O build temporário `ecu-sd-benchmark` mantém a UI normal e adiciona somente o
diagnóstico. O build `ecu-normal` não contém comando, fila ou buffer desse
ensaio. A tarefa SD do diagnóstico usa stack 12 KiB, contra 8 KiB no normal;
o relatório registra a margem mínima observada. Essa diferença deve ser
considerada ao interpretar memória do firmware de diagnóstico.

O worker SD cria um arquivo próprio `/sdcard/ngk-io-<id>-<nonce>.tmp` com
`O_CREAT|O_EXCL`: arquivo existente não é aberto para sobrescrita. A fixture
ocupa no máximo 1 MiB; o total de bytes transferidos pode ser maior, pois o
ensaio volta ao início desse mesmo arquivo. Exige pelo menos 2 MiB livres.
Não formata, reparte, altera frequência/pinos ou acessa arquivos do usuário.

O preparo escreve um padrão artificial dependente do endereço. A escrita
medida usa gerações diferentes; uma leitura final verifica o conteúdo inteiro,
inclusive a passagem parcial mais recente. Ao terminar, fecha e remove somente
o caminho criado por essa execução. Falhas de escrita curta, leitura, seek,
sync, fechamento, remoção, corrupção ou timeout impedem aprovação.
Uma interrupção de energia pode deixar o arquivo temporário: ele não é
removido automaticamente numa execução posterior.

CID é conferido antes/depois somente em RAM e nunca transmitido ou gravado no
relatório. A capacidade esperada é uma conferência adicional; tamanho sozinho
não identifica um cartão. Não trocar o cartão durante a bancada.

## Executar na bancada autorizada

1. Conferir NGK ECU normal, cartão informado, flags de aquisição/atuação falsas,
   configuração Suave e movimento habilitado. Fechar a conexão de outro cliente.
2. Preservar o aplicativo normal em backup privado antes da gravação. Compilar
   `ecu-sd-benchmark` e gravar somente o aplicativo em 0x10000, conferindo o hash.
3. Executar o cliente explícito na raiz do projeto:

   ```sh
   .venv/bin/python tools/benchmark_sd.py --port /dev/ttyUSB0 \
     --expected-sd-mib 30255 --output local/sd-io-bench/physical.json \
     --execute-physical-bench
   ```

4. Conferir resultados, verificação de conteúdo, continuidade do cartão,
   remoção do arquivo e SIM encerrada. Qualquer falha permanece no relatório.
5. Restaurar o aplicativo normal preservado, conferir escrita/versão/SD/perfil
   e deixar a aplicação do operador desconectada.

O cliente não escolhe disco do Linux nem formata cartão; identifica a ESP32
conectada e valida o diagnóstico/protocolo. Rádios precisam estar desligados
e a configuração visual já aplicada. Não usa `ui-smooth`, export/import de
perfil ou gravação de NVS. `--help` e testes não abrem porta física.

## Como interpretar

Latências de read/write e fsync são separadas. P95 é um limite superior obtido
por histograma fixo de 1 ms; o último intervalo usa o máximo observado.
Vazão efetiva divide bytes pelo tempo da fase, incluindo seek, sync e pausas
cooperativas. Preparo, verificação final e remoção ficam fora dessa vazão.

O orçamento global é 60 s por trabalho, com conferência entre operações. Uma
chamada bloqueante do driver pode exceder o prazo antes que o controle volte;
não há promessa de preempção de uma operação FAT/SPI. Sincronização bem-sucedida
é a resposta do driver, sem provar durabilidade contra perda abrupta de energia.
O SDK mapeia criação exclusiva e fsync para operações FAT correspondentes:
[implementação oficial da Espressif](https://github.com/espressif/esp-idf/blob/v4.4.7/components/fatfs/vfs/vfs_fat.c).

O relatório guarda apenas agregados, sem amostras, dados serial brutos, tokens,
nomes de arquivos existentes, CID, credenciais ou identificadores do hardware.
O ensaio usa dados artificiais; não valida aquisição real, motor ou veículo.

## Validação de software

O motor portátil e o parser possuem testes nativos com arquivo/relógio em
memória, incluindo colisão, falhas de I/O/limpeza, corrupção, gerações, deadline
e relógio regressivo. O cliente usa porta e relógio falsos nos testes Python.
Builds e resultados desta preparação ficam em [validacao.md](validacao.md).
A recomendação de tamanho de lote depende da futura medição física.
