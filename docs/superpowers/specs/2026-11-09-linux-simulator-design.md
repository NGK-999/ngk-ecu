# Simulador Linux NGK ECU

Aplicativo nativo Python3.12/QtWidgets PySide6-Essentials6.12.0, tema coerente
com o cockpit. Funciona offline e, por conexão explícita na interface, envia
dados sintéticos ao ESP32. Sem atuação física, alteração de GPIO, formatoSD,
registro de amostras ou conexão automática ao abrir. Geração de dados e GUI
no processo local; serial em thread com snapshot mais recente, sem fila crescente.

## Controles

Motor ligado, acelerador, freio, embreagem e marchaR/N/1–4. Modelo de condução
ilustrativo, sem afirmar calibração de Fusca. Modo manual permite RPM0–12000,
velocidade0–250km/h, bloco-40–200C. Presets e falhas: ausênciaRPM/velocidade,
bloco aberto/curto(mutualmente exclusivos), óleo baixo, superaquecimento,
TPSinválido, marchainválida. PerdaUSB interrompe frames para testar stale.
Limpar falhas não altera controles. Nenhuma amostra é persistida.

## Contrato Python

`simulator/ngk_simulator/model.py`: `Fault(IntFlag)` NONE0,NO_RPM1,NO_SPEED2,
TEMP_OPEN4,TEMP_SHORT8,LOW_OIL16,OVERHEAT32,TPS_INVALID64,GEAR_INVALID128.
`Controls` dataclass mutável: engine_on=False, throttle_pct=0.0,brake_pct=0.0,
clutch=False,gear=0,block_c=85.0,manual=False,manual_rpm=0.0,
manual_speed_kph=0.0,faults=Fault.NONE. Gear-1=R,0=N,1..4.
`Sample` frozen dataclass: rpm,speed_kph,block_c,throttle_pct floats;gear int;
oil int(0normal,1baixo,2semleitura);faults Fault. Todos campos têm defaults
consistentes com Controls. `VehicleModel.step(controls,dt)->Sample` e reset().
Valores finitos e limites, dt negativo/inválido rejeitado; clamp passos longos.
`protocol.py`: encode_frame(session:int,sequence:int,sample:Sample)->bytes
terminadoLF; valida limites; sequence/session1..UINT32_MAX. Semhardware/Qt.

`link.py` (integrador): `SerialLink.connect(port)`,disconnect(),close(),
set_sample(sample,paused=False),snapshot()->LinkStatus. LinkStatus frozen:
state str offline/connecting/streaming/paused/error,detail str PT,port str,
device_live bool,sequence int. `available_ports()->list[str]` não abre portas.
Construtor opcional serial_factory para testes em memória. Sem reconexão
automática; erro recuperado por novo clique Conectar. Conexão nunca na GUIthread.

`window.py` (GUI): `SimulatorWindow(link=None)`, `main()`. Inicialmente offline.
TimerGUI33ms e snapshots de worker; controles com objectNames previsíveis.
Janela1120×760,min980×680, tema grafite/ciano, leitura grande e painel de falhas.

## Contrato USB protocolo 1

115200baud ASCII LF, linhas<=95bytes. `info` precisa comprovar projectNGK ECU e
simulation_protocol=1 antes de enviar comando de simulação. `sim-start TOKEN` usa nonce escolhido pelo cliente antes da escrita, permitindo
cancelar com sim-stop mesmo antes do ready. `sim-start` sem argumento permanece
como alternativa de diagnóstico que cria nonce no ESP32. Retorna
JSON `{"kind":"sim","state":"ready","protocol":1,"session":N}`. TokenRAM
não nulo por sessão, não autenticação de atuação. `sim-stop TOKEN` exige sessão
corrente; `sim-status` retorna JSON kind sim,state live/stale/off,protocol1,
live bool,seq uint32,faults int sem RPM/amostras. Firmware manutenção não aceita
simulação. App não envia comandos FORMAT/rede ou de atuação.

Frame: `SIM1 TOKEN SEQ RPM SPEED10 TEMP10 TPS10 GEAR OIL FAULTS`.
SPEED10=km/h×10(0..2500),TEMP10=C×10(-400..2000),TPS10=percentual×10(0..1000).
RPM0..12000,GEAR-1..4,OIL0..2,FAULTS0..255; aberto+curto simultâneos inválidos.
Inteiros somente, sem overflow, campos extras ou trailinggarbage; seq crescente.
Frames20 Hz. Firmware TTL 750 ms: após perder frames, instrumentos ficam--,
SIM SEM LINK, sem reaproveitar dado antigo. Frames válidos da mesma sessão
podem retomar. Stop/reboot limpa sessão; stop não persiste dados.

## Firmware API

`include/simulation.hpp` puroC++ namespaceecu::simulation:
Fault enum e Frame{session,sequence,rpm,speed10,temp10,tps10,gear,oil,faults};
`parseFrame(constchar*,Frame&)` deixa output intacto em erro.
`State.start(token,now)`,stop(token)->bool,accept(Frame,now)->bool,
snapshot(now)->Snapshot{enabled,live,frame}. Modelo testável wrapuint32/TTL.
`include/simulation_service.hpp` /src/simulation_service.cpp: beginSession(token),
endSession(token),submit(Frame),snapshot(),endAnySession() para donoUI/main
único; usa millis mas semSerial/LVGL/SD. Semworker novo.

Integrador adiciona sim-start/stop/status e info ao main, sourceSIM USB ao ui.
RPM/velocidade/bloco/óleo e TPS/marcha aparecem no modo SIM USB com validade;
falha usa texto e cor, jamais ausência=0. Fonte USB tem prioridade sobreDEMO;
benchmark/DEMO recusados durante sessão, saída local disponível emToque/DEMO.

## Verificação

Unitários sem hardware: modelo, parser/limites/sessões/TTL e protocolo golden
Python/C++; worker com transporte em memória, GUI offscreenQtTest. Conferir
GUI visível no Linux e screenshot local; bancada física explicitamente separada,
sem publicar rawserial ou leituras. Compilar firmware, instalar normal, conferir
SIM USB, stale 750 ms, stop, reabertura e regressões. Ao final, app aberto offline,
operador escolhe quando conectar. Publicar fontes/documentação revisadas.
