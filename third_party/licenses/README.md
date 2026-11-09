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
