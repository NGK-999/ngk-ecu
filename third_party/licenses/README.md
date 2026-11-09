# Fonte NGK Digits 48

`firmware/ecu/src/ngk_digits_48.c` é uma versão modificada da fonte Montserrat
Medium distribuída no LVGL 8.4.0. O nome da versão modificada é **NGK Digits 48**.
Ela contém somente `-.0123456789`, sem kerning. A ferramenta copia os pixels
de 4 bpp e as métricas originais; não rasteriza novamente nem altera os avanços.

O aviso original no arquivo Montserrat-Medium.ttf distribuído pelo LVGL é:

> Copyright 2011 The Montserrat Project Authors (https://github.com/JulietaUla/Montserrat)

Os dados da fonte e esta versão modificada continuam sob a **SIL Open Font
License 1.1**. O texto completo consta em [Montserrat-OFL.txt](Montserrat-OFL.txt),
obtido da [distribuição oficial Google Fonts](https://github.com/google/fonts/blob/main/ofl/montserrat/OFL.txt)
em 2026-11-09. O arquivo oficial atual contém também o aviso de copyright 2024
dos autores; isso não substitui o aviso 2011 da fonte original usada aqui.

As estruturas/código LVGL e sua distribuição têm copyright (c) 2021 LVGL Kft e
licença **MIT**, preservada integralmente em [LVGL-MIT.txt](LVGL-MIT.txt).
Nenhum glifo Font Awesome foi selecionado do arquivo C original.

## Proveniência

- C original: [lv_font_montserrat_48.c, LVGL v8.4.0](https://github.com/lvgl/lvgl/blob/v8.4.0/src/font/lv_font_montserrat_48.c).
  SHA-256: `f2c8ef9e9ffc81f72ff05f03344f744c3b808be6f32aeed6912ecd95242097a7`.
- TTF original: [Montserrat-Medium.ttf, LVGL v8.4.0](https://github.com/lvgl/lvgl/blob/v8.4.0/scripts/built_in_font/Montserrat-Medium.ttf).
  SHA-256: `421f26b23e2be6b98373d32acd3cb2897b154d4bf0a77d26534ce476e4cbed53`.
- Pixels concatenados na ordem `-.0123456789`: 4.429 bytes;
  SHA-256: `3079274f1be2760180cceb322dbfa8182ed1cae79f29968f8c889dab23712825`.
- `Montserrat-OFL.txt`: SHA-256 `8b7141c03fa4f8d44e6345d5d4931709290f0f67875e452e95ac1fd3a027802e`.
- `LVGL-MIT.txt`: SHA-256 `6a916130a36e83a79bb0dca6c6a7bfe60a67af7d118605e2b7b23bd5ad5c4eb0`.

## Reprodução local

Execute na raiz do repositório, com a dependência LVGL 8.4.0 instalada localmente:

```sh
.venv/bin/python tools/subset_lvgl_font.py firmware/ecu/.pio/libdeps/ecu-normal/lvgl/src/font/lv_font_montserrat_48.c firmware/ecu/src/ngk_digits_48.c
.venv/bin/python tools/subset_lvgl_font.py firmware/ecu/.pio/libdeps/ecu-normal/lvgl/src/font/lv_font_montserrat_48.c firmware/ecu/src/ngk_digits_48.c --check
.venv/bin/python -m unittest discover -s tests/font -v
```

A ferramenta usa somente a biblioteca padrão do Python. Recusa compressão,
bpp diferente de 4 e mapas ASCII incompatíveis. Os testes usam uma fonte
sintética e, quando o LVGL local está disponível, comparam os pixels/métricas
originais. Com GCC disponível, compilam e executam os callbacks reais LVGL
para conferir o mapa esparso e a ausência dos caracteres não selecionados.

Uma compilação isolada com o compilador Xtensa local mediu 4.629 bytes de dados
constantes e 8 bytes de cache em BSS, contra 96.713 e 8 bytes da fonte completa.
A diferença de 92.084 bytes refere-se aos objetos de fonte; o MAP do firmware
integrado determina a economia final na flash. Essa redução não mede FPS.

## NGK Cockpit 64 e 72 — Barlow Condensed Bold

`firmware/ecu/src/ngk_cockpit_64.c` e `ngk_cockpit_72.c` são subconjuntos
numéricos rasterizados da **Barlow Condensed Bold**, peso 700. O nome das
versões derivadas é **NGK Cockpit 64/72**. Contêm somente `-.0123456789`,
4 bpp, sem compressão e sem kerning.

Copyright 2017 The Barlow Project Authors (https://github.com/jpt/barlow).
Os dados derivados permanecem sob **SIL OFL 1.1**, integralmente preservada em
[Barlow-Condensed-OFL.txt](Barlow-Condensed-OFL.txt). O TTF original não é
incluído no repositório: o gerador baixa a cópia oficial fixada, confere SHA-256
e mantém o arquivo apenas no cache ignorado `.tools/font-sources/`.

- Fonte oficial: [Google Fonts, commit fixado](https://github.com/google/fonts/tree/5bb28733cec313b63fbd415b574b5ae35c8aa8b8/ofl/barlowcondensed).
- TTF `BarlowCondensed-Bold.ttf`: SHA-256
  `e476562ec9c1e16cf16475895b511f08c804f438cc9a9f80a44ea50a0eeb5b65`.
- Licença copiada: SHA-256
  `186d750eb496a4c17a76385f82be6aea2ac1cf2de074a811d63786cf374ea73f`.
- Rasterizador: [lv_font_conv oficial](https://github.com/lvgl/lv_font_conv),
  pacote `1.5.3` e integridade fixados em `tools/fonts/package-lock.json`;
  execução com Node 24.22.1 isolado em `.tools`. O conversor usa licença MIT;
  os dados rasterizados da fonte continuam sob OFL.

O cmap FULL original do conversor contém offset zero no buraco `/`, que o
leitor do LVGL 8.4 interpreta como o primeiro glifo. O gerador normaliza
**apenas o mapa Unicode** para SPARSE_TINY, com os doze caracteres explícitos.
Bitmap, avanços, caixas, offsets e métricas verticais permanecem idênticos.
O ensaio real falhou antes dessa normalização e passou depois, inclusive para
a rejeição de `/` e dos demais caracteres ausentes.

As métricas globais já são compactas: no tamanho nominal 64, `line_height=48`
e `base_line=1`; no 72, `line_height=53` e `base_line=1`. Não foi necessário
trim vertical. Cinco dígitos no tamanho 64 precisam de no máximo 155 px;
três no tamanho 72, de no máximo 105 px, com espaçamento zero. Os dígitos são
proporcionais; não presumir avanço tabular. Objetos Xtensa isolados mediram
6.322 e 7.723 bytes constantes, mais 8 bytes BSS por fonte. Esses números não
substituem o tamanho do firmware linkado nem medem desempenho de desenho.

### Reprodução e testes sem hardware

```sh
.venv/bin/python tools/fonts/generate_cockpit.py --prepare
.venv/bin/python tools/fonts/generate_cockpit.py --check
.venv/bin/python -m unittest discover -s tests/font -v
```

`--prepare` é a etapa explícita com rede: baixa o TTF verificado e instala o
conversor com `npm ci --ignore-scripts` no diretório local de ferramentas.
`--check` e os testes não baixam arquivos nem instalam dependências. Sem
`--check`, o gerador reproduz os dois C no diretório de fontes da aplicação;
`--output-dir` permite escolher outro destino.

Os testes compilam os callbacks reais LVGL 8.4 para verificar exatamente os
doze caracteres e a ausência de margem vertical vazia. Quando os caches locais
do conversor/TTF estão preparados, também regeneram os arquivos, verificam
reprodução exata e comparam os 24 bitmaps/métricas com a saída oficial compilada
separadamente. Essa comparação é pulada explicitamente se os caches faltarem;
nenhum teste pede rede. GCC e a dependência LVGL local são necessários para
os ensaios de execução C.

## NGK Gear 48 — subconjunto para marcha

`firmware/ecu/src/ngk_gear_48.c` deriva da mesma Barlow Condensed Bold fixada
acima, com o nome **NGK Gear 48** e os oito caracteres `-NR01234`. Permanece sob
SIL OFL 1.1, com o mesmo copyright dos autores. Usa 4 bpp, sem compressão ou
kerning, mapa Unicode esparso nativo e altura efetiva de 34 px. Não inclui
fontes, ícones ou logotipos da Apple ou FuelTech.

O mesmo `tools/fonts/generate_cockpit.py` reproduz e confere os três arquivos;
não há novo download ou dependência. O ensaio `test_gear_font.py` regenera o C
e usa os callbacks reais do LVGL para conferir N/R/dígitos, limites da célula
e ausência dos demais caracteres, inclusive os buracos do mapa Unicode.

## Ícones de navegação e alerta

A revisão 0.4.3 usa cinco ícones do Lucide, com licença upstream integral
ISC e avisos MIT preservados em [third_party/icons/lucide/LICENSE](../icons/lucide/LICENSE).
Proveniência, commit, hashes e reprodução dos recursos alpha4 estão em
[README dos ícones](../icons/lucide/README.md). Os SVGs originais são somente
entradas do gerador no host; não há renderizador SVG no ESP32.
