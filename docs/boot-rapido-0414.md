# Carregamento inicial do perfil — 0.4.14-fastboot

Em 2026-11-12, o responsável pediu uma inicialização mais rápida após desligar e
ligar o painel. No ensaio por reset RTS, a revisão final aplicou o perfil visual
aproximadamente **435 ms antes**, enquanto o primeiro quadro DMA terminou apenas
**7 ms antes**. São marcos diferentes; o ensaio não mediu alimentação→imagem.

## Alteração limitada

Antes, `pollServices()` impunha intervalo de 500 ms inclusive enquanto aguardava
o primeiro perfil pronto. Agora consulta os snapshots enquanto
`settingsLoaded=false`; depois de carregar o primeiro perfil, mantém o intervalo
de 500 ms. A leitura de NVS continua no worker, sem acesso novo a SD/NVS pela UI.

O painel inicial só é reconstruído quando esse primeiro perfil muda a unidade
de velocidade ou temperatura. Unidades iguais preservam os objetos existentes;
mph/°F salvos continuam atualizando as legendas. As guardas de página e calibração
permanecem: o carregamento não redireciona Menu/Ajustes e não remove a calibração
obrigatória. Boot no painel, duplo toque para o menu e dados indisponíveis sem
fonte continuam conforme [a revisão 0.4.13](inicializacao-painel-0413.md).

Não houve alteração de GPIOs, frequências, SDK, bootloader ou atrasos do LCD.
As esperas de reset e saída de sleep da LovyanGFX foram preservadas.

## Método e resultados de bancada

Foram usados binários temporários instrumentados, com sete marcos de
`esp_timer_get_time()` e um comando curto de consulta. Cada variante teve três
execuções, reiniciadas por RTS, sem cortar a alimentação. As consultas dos marcos
foram feitas após aguardar 1,2 s desde a primeira resposta `info`, para permitir
que as fases de inicialização terminassem. Essa espera do condutor não é um
tempo interno de boot. Todos os casos encerraram a porta.

O perfil físico ensaiado foi o padrão métrico dessa unidade. Os intervalos
abaixo partem do início de `ui::begin()`, depois da entrada na aplicação; não
incluem ROM/bootloader nem medição óptica do LCD. Valores são medianas das três
execuções:

| Marco | Base 0.4.13 | Tentativa simples descartada | Revisão final |
| --- | ---: | ---: | ---: |
| Primeiro quadro DMA concluído | 356,688 ms | 432,973 ms | 349,707 ms |
| Perfil aplicado | 784,704 ms | 359,747 ms | 349,875 ms |
| Primeiro quadro marcado após aplicação do perfil | 896,669 ms | 476,053 ms | não observado |

Na tentativa simples, antecipar o perfil ainda reconstruía o painel mesmo com
unidades iguais: o primeiro DMA regrediu 76,285 ms. Essa tentativa foi descartada.
O refinamento evita a reconstrução desnecessária. Em relação à base, antecipou
a aplicação do perfil em 434,829 ms e o primeiro DMA em 6,981 ms nesta amostra.

Na revisão final, as três leituras do marcador de quadro após o perfil ficaram
`null`: com as unidades métricas já corretas, não houve novo redraw marcado.
Isso não representa duração zero nem fornece um tempo de quadro após o perfil.
O perfil foi aplicado cerca de 0,168 ms depois da conclusão do primeiro DMA.
O intervalo entre UI pronta e perfil aplicado caiu de 497,044 para 66,092 ms.

## Execução local e artefato final

Passaram 22 executáveis nativos ASan/UBSan, 102 testes do simulador em memória,
três testes do driver de prévia, onze cenas do LVGL host e o build normal.
A regressão do perfil imperial chegando 10 ms após o boot foi observada falhando
antes da correção. Esses testes não abrem serial nem validam a placa.

A instrumentação temporária foi removida das fontes de produção. Artefato normal
`0.4.14-fastboot`: RAM estática 96.848 B, flash do linker 1.417.961 B,
binário 1.424.544 B, SHA-256
`26509ebe3f0fd032ab750bcf2d136bc5009825f28f007c1258cbc070ff93f224`.

O normal final foi instalado somente em app `0x10000`, após preservar e validar
o backup da 0.4.13. Escrita e `verify-flash` aprovados; o artefato entregue não
contém o probe temporário. Captura após reinício, sem comandos de navegação:
153600 B e CRC32 `eaa0e4ed`, com imagem inspecionada mostrando o painel com
`--`/`SEM SINAL` e layout preservado. Estado final: SD montado, perfil persistido,
SIM/rádios desligados, sensores/atuadores false e porta fechada.

## Limites

Três resets por variante não equivalem a desligar/ligar a alimentação, medir o
touch a frio ou validar todos os perfis salvos. Não foram medidos tempo total
desde alimentação, FPS óptico, CPU livre, sensores ou veículo. Não houve novos
ensaios de cargas SD, Wi-Fi ou BLE. O teste host confere as unidades imperiais,
mas seu desempenho não foi medido fisicamente. O avanço de aproximadamente
435 ms pertence à aplicação do perfil; não é uma redução equivalente no
aparecimento da tela.

[Agregados revisados e estado de entrega](measurements/boot-speed-2026-11-12.json).
