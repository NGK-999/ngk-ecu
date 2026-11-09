# Subconjunto Lucide para NGK ECU

Originais sem alteração do repositório oficial
[lucide-icons/lucide](https://github.com/lucide-icons/lucide), commit
[`3fef7aca71a50acc54bd2f24f0cc4747c22aa6d2`](https://github.com/lucide-icons/lucide/tree/3fef7aca71a50acc54bd2f24f0cc4747c22aa6d2).
`manifest.json` registra URL imutável e SHA-256 de cada arquivo original, inclusive
`LICENSE`. A licença upstream integral foi preservada: ISC, com os avisos MIT
dos derivados de Feather. As permissões e os avisos se aplicam aos assets derivados.

| Uso | SVG oficial | Símbolo LVGL | Tamanho | Pixels em flash |
| --- | --- | --- | --- | --- |
| Painel | gauge | `ngk_icon_gauge_32` | 32 × 32 | 512 bytes |
| Sensores | activity | `ngk_icon_activity_32` | 32 × 32 | 512 bytes |
| Ajustes | sliders-horizontal | `ngk_icon_sliders_32` | 32 × 32 | 512 bytes |
| Sistema | cpu | `ngk_icon_cpu_32` | 32 × 32 | 512 bytes |
| Alerta | triangle-exclamation-point | `ngk_icon_alert_20` | 20 × 20 | 200 bytes |

O nome solicitado `triangle-alert` é alias oficial de
[`triangle-exclamation-point`](https://lucide.dev/icons/triangle-alert) neste
upstream; o desenho continua sendo um triângulo com exclamação. Os SVGs mantêm
viewBox 24 × 24, traço 2, pontas e junções redondas. Apenas a rasterização e a
quantização para alpha de 4 bits foram aplicadas.

Os cinco planos somam **2248 bytes**, além de cinco descritores `lv_img_dsc_t`
constantes (60 bytes no ESP32 de 32 bits). A compilação isolada pelo toolchain
Xtensa local confirmou `.rodata = 2308`, `.data = 0`, `.bss = 0` e `.text = 0`.
Permanecem na flash mapeada: nenhuma
fonte inteira, renderizador SVG embarcado, conversão ou alocação própria em runtime.
Qt/PySide6 são somente ferramentas do host. LVGL fornece seu próprio desenho e
buffers; os widgets devem ser criados na montagem da tela, sem recriação no fluxo
de atualização de sensores.

## Reprodução offline

Use Python 3.12 e a `.venv` local do projeto com as versões de
`tools/icons/requirements.lock` (iguais às ferramentas Qt existentes). Os SVGs e
a licença já estão versionados; o gerador não possui caminho de download.

```sh
.venv/bin/python tools/icons/generate.py
.venv/bin/python tools/icons/generate.py --check
.venv/bin/python tools/icons/generate.py --check --preview artifacts/icons-alpha4.png
.venv/bin/python -m unittest discover -s tests/icons -v
```

O gerador exige PySide6/Qt 6.12.0, renderiza a 4× sobre transparência, faz média
inteira de blocos 4×4 e quantiza com arredondamento ao alpha 0–15. Em cada byte,
o pixel da esquerda ocupa o nibble alto; cada linha usa `ceil(largura/2)` bytes,
sem paleta nem cabeçalho no plano de pixels. O teste nativo lê cada pixel pelo
próprio `lv_img_buf_get_px_alpha` do LVGL 8.4 instalado localmente, inclusive uma
amostra de largura ímpar para validar alinhamento de linha. Os descritores usam
o comprimento real do plano; algumas macros de tamanho do LVGL superestimam
esse comprimento e não representam o stride usado pelo decoder.

Exemplo de consumo:

```c
#include "ui_icons.h"
lv_img_set_src(image, &ngk_icon_gauge_32);
lv_obj_set_style_img_recolor(image, lv_color_hex(0xff9824), LV_PART_MAIN);
lv_obj_set_style_img_recolor_opa(image, LV_OPA_COVER, LV_PART_MAIN);
```

Os testes são locais e sintéticos. A folha PNG verifica pixels em tamanho nativo
(linha superior) e ampliados 3× (linha inferior); não comprova legibilidade,
cores ou fluidez no LCD físico.
