# Base de bancada da NGK ECU

## Requisitos vinculantes

Projeto próprio para motor VW Fusca 1300 carburado: RPM, temperatura do bloco,
velocidade e aviso de óleo, com futura injeção monoponto. A referência externa
serve somente à identidade visual. Nenhum domínio de produção industrial,
turno, produto ou nome da referência pertence à interface deste firmware.
Substituição de firmware e manutenção do SD foram autorizadas pelo responsável.

## Decisões técnicas desta entrega

Firmware de bancada, sem GPIO de sensores/atuadores do motor habilitado.
Inicialmente todos os instrumentos indicam ausência de dados; nunca simular
uma leitura como se fosse real. A apresentação deve dizer NGK ECU e BANCADA.
Menu inicial com botões grandes Painel, Sensores, Ajustes e Sistema. O modo
Painel ocupa a tela com instrumentos grandes, sem navegação fixa; duplo toque
retorna ao menu. Telas secundárias têm botão Menu. Essa alteração foi pedida
explicitamente pelo responsável após a primeira instalação de bancada.
Aplicar o tema grafite/azul e Montserrat em LVGL, usando somente os drivers
de tela/touch compatíveis e uma aplicação de domínio nova.

Uma tarefa de armazenamento separada cuida do SD. LVGL pertence à tarefa
principal. Não habilitar Wi-Fi/BLE. Firmware normal não inclui formatação.
Manutenção separada identifica capacidade e exige comando único explícito,
com geometria esperada, antes de apagar. Não listar nem ler arquivos do usuário.

Chip confirmado ESP32-D0WD-V3 rev3.1, flash4MiB. Perfil ST7789 configurado
produziu imagem legível confirmada pelo responsável; isso não identifica sufixo
do controlador. Não assumir que o cartão atual tem32GB: a primeira consulta
reportou1898MiB. Essa divergência bloqueia formato até esclarecer o alvo.

## Validação

Testes nativos de políticas e layout, sem hardware. Compilar normal e manutenção.
Gravar somente após build e revisão. Verificar o firmware instalado, SD e
metadados pela serial, sem registrar dados brutos. Inspeção da tela e toque deve
ser confirmada separadamente pelo responsável. Sem validação em motor/veículo.
