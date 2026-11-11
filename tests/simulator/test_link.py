import json
import queue
import threading
import time
import unittest
from unittest.mock import patch
from types import SimpleNamespace
from ngk_simulator.link import SerialLink, _JsonLines, available_ports
from ngk_simulator.model import Sample

class MemorySerial:
    def __init__(self, project='NGK ECU', protocol=1):
        self.project, self.protocol = project, protocol
        self.incoming = queue.Queue()
        self.writes = []
        self.opened = False
        self.closed = False
        self.last_frame = 0
        self.seq = 0
        self.guard = threading.Lock()
    def open(self): self.opened = True
    def reset_input_buffer(self): pass
    def read(self, size):
        try: return self.incoming.get(timeout=.005)
        except queue.Empty: return b''
    def reply(self, data): self.incoming.put(json.dumps(data).encode()+b'\n')
    def write(self, data):
        with self.guard: self.writes.append(bytes(data))
        if data == b'info\n':
            self.reply({'project':self.project, 'simulation_protocol':self.protocol})
        elif data.startswith(b'sim-start '): self.reply({'kind':'sim','state':'ready','protocol':1,'session':int(data.split()[1])})
        elif data.startswith(b'SIM1 '):
            self.seq = int(data.split()[2]); self.last_frame = time.monotonic()
        elif data == b'sim-status\n':
            live = bool(self.seq and time.monotonic()-self.last_frame < .75)
            self.reply({'kind':'sim','state':'live' if live else 'stale','protocol':1,'seq':self.seq,'live':live,'faults':0})
        return len(data)
    def close(self): self.closed = True
    def frames(self):
        with self.guard: return [v for v in self.writes if v.startswith(b'SIM1 ')]

def until(predicate, timeout=2):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        if predicate(): return
        time.sleep(.005)
    raise AssertionError('condition not observed')

class LinkTests(unittest.TestCase):
    def test_no_initial_production_suspends_frames_but_keeps_status_and_close(self):
        device=MemorySerial();link=SerialLink(serial_factory=lambda:device,session_factory=lambda:123)
        self.addCleanup(link.close)
        link.connect('MEMORY')
        until(lambda:link.snapshot().state=='source_stale')
        until(lambda:device.writes.count(b'sim-status\n')>=2)
        self.assertEqual(device.frames(),[])
        self.assertEqual(link.snapshot().sequence,0)
        link.close()
        self.assertTrue(device.closed)
        self.assertIn(b'sim-stop 123\n',device.writes)
    def test_expired_producer_stops_frames_and_same_value_resumes_same_session(self):
        device=MemorySerial();link=SerialLink(serial_factory=lambda:device,session_factory=lambda:123)
        self.addCleanup(link.close)
        sample=Sample(rpm=2450)
        link.set_sample(sample)
        link.connect('MEMORY')
        until(lambda:len(device.frames())>=2)
        until(lambda:link.snapshot().state=='source_stale')
        count=len(device.frames())
        until(lambda:device.writes.count(b'sim-status\n')>=2)
        self.assertEqual(len(device.frames()),count)
        link.set_sample(sample)
        until(lambda:len(device.frames())>count)
        until(lambda:link.snapshot().state=='streaming')
        self.assertEqual(link.snapshot().state,'streaming')
        frames=device.frames()
        self.assertEqual([int(frame.split()[2]) for frame in frames],list(range(1,len(frames)+1)))
        self.assertTrue(all(frame.split()[1]==b'123' for frame in frames))
        self.assertEqual(device.writes.count(b'sim-start 123\n'),1)
        link.set_sample(sample,paused=True)
        until(lambda:link.snapshot().state=='paused')
        count=len(device.frames())
        until(lambda:device.writes.count(b'sim-status\n')>=3)
        self.assertEqual(link.snapshot().state,'paused')
        self.assertEqual(len(device.frames()),count)
        for rpm in (1000,2000,3000):
            link.set_sample(Sample(rpm=rpm),paused=True)
        link.set_sample(Sample(rpm=3500),paused=False)
        until(lambda:len(device.frames())>count)
        self.assertTrue(all(frame.split()[3]==b'3500' for frame in device.frames()[count:]))
    def test_identical_fresh_samples_keep_streaming_beyond_producer_timeout(self):
        device=MemorySerial();link=SerialLink(serial_factory=lambda:device)
        self.addCleanup(link.close)
        sample=Sample(rpm=2450)
        link.set_sample(sample)
        link.connect('MEMORY')
        until(lambda:link.snapshot().state=='streaming')
        start=time.monotonic()
        while time.monotonic()-start<.4:
            link.set_sample(sample)
            time.sleep(.02)
            self.assertEqual(link.snapshot().state,'streaming')
        self.assertGreaterEqual(len(device.frames()),5)
    def test_producer_timeout_requires_a_positive_finite_number(self):
        for value in (0,-1,float('nan'),float('inf'),True,None,'0.25'):
            with self.subTest(value=value),self.assertRaises(ValueError):
                SerialLink(producer_timeout=value)
        self.assertIsInstance(SerialLink(producer_timeout=.1),SerialLink)
    def test_usb_ports_are_offered_before_legacy_serial_ports_without_opening(self):
        ports=[SimpleNamespace(device='/dev/ttyS0'),SimpleNamespace(device='/dev/ttyUSB0'),SimpleNamespace(device='/dev/ttyACM0')]
        with patch('ngk_simulator.link.list_ports.comports',return_value=ports):
            self.assertEqual(available_ports(),['/dev/ttyACM0','/dev/ttyUSB0','/dev/ttyS0'])
    def test_constructor_and_offline_sample_never_open_a_port(self):
        opened=[]
        link=SerialLink(serial_factory=lambda: opened.append(True))
        link.set_sample(Sample(rpm=2500))
        self.assertEqual(link.snapshot().state,'offline')
        link.close()
        self.assertEqual(opened,[])
    def test_explicit_handshake_stream_pause_resume_stop_are_bounded(self):
        device=MemorySerial();link=SerialLink(serial_factory=lambda:device,session_factory=lambda:123)
        self.addCleanup(link.close)
        link.set_sample(Sample(rpm=2450,speed_kph=32.5,throttle_pct=40,gear=2))
        link.connect('MEMORY')
        until(lambda:len(device.frames())>=3)
        self.assertTrue(device.opened)
        self.assertEqual(device.writes[0],b'info\n')
        self.assertIn(b'sim-start 123\n',device.writes)
        self.assertEqual(device.frames()[0],b'SIM1 123 1 2450 325 850 400 2 0 0\n')
        link.set_sample(Sample(rpm=3000),paused=True)
        until(lambda:link.snapshot().state=='paused')
        count=len(device.frames());time.sleep(.16)
        self.assertEqual(count,len(device.frames()))
        link.set_sample(Sample(rpm=3500),paused=False)
        until(lambda:len(device.frames())>count)
        self.assertGreater(int(device.frames()[-1].split()[2]),int(device.frames()[0].split()[2]))
        link.close()
        self.assertTrue(device.closed)
        self.assertIn(b'sim-stop 123\n',device.writes)
        self.assertEqual(link.snapshot().state,'offline')
        self.assertFalse(any(b'FORMAT' in v or b'wifi-' in v for v in device.writes))
    def test_another_device_or_old_firmware_never_receives_sim_start(self):
        for project,protocol in [('OTHER',1),('NGK ECU',0),('NGK ECU',True)]:
            with self.subTest(project=project,protocol=protocol):
                device=MemorySerial(project,protocol);link=SerialLink(serial_factory=lambda:device)
                link.connect('MEMORY');until(lambda:link.snapshot().state=='error')
                link.close()
                self.assertFalse(any(v.startswith(b'sim-start') for v in device.writes))
                self.assertEqual(device.frames(),[])
    def test_open_failure_is_reported_without_escaping_the_worker(self):
        class Denied(MemorySerial):
            def open(self): raise PermissionError(13,'synthetic denial')
        device=Denied();link=SerialLink(serial_factory=lambda:device)
        link.connect('MEMORY');until(lambda:link.snapshot().state=='error')
        self.assertIn('acesso',link.snapshot().detail.lower())
        link.close()
    def test_cancel_during_start_does_not_leave_device_simulation_enabled(self):
        class Delayed(MemorySerial):
            def __init__(self):
                super().__init__(); self.active=False; self.started=threading.Event(); self.timer=None
            def write(self,data):
                if data == b'sim-start\n' or data.startswith(b'sim-start '):
                    self.writes.append(bytes(data)); self.active=True; self.started.set()
                    token=int(data.split()[1]) if data.startswith(b'sim-start ') else 123
                    self.timer=threading.Timer(.15,lambda:self.reply({'kind':'sim','state':'ready','protocol':1,'session':token}))
                    self.timer.daemon=True;self.timer.start();return len(data)
                if data.startswith(b'sim-stop '): self.active=False
                return super().write(data)
        device=Delayed();link=SerialLink(serial_factory=lambda:device)
        self.addCleanup(link.close)
        link.connect('MEMORY');self.assertTrue(device.started.wait(1))
        link.close()
        if device.timer:device.timer.cancel()
        self.assertFalse(device.active, 'cancelling before ready must end the device session')
    def test_ready_rejects_boolean_protocol_and_wrong_session_echo(self):
        class BadReady(MemorySerial):
            def __init__(self,field,value):super().__init__();self.field,self.value=field,value
            def reply(self,data):
                if data.get('state')=='ready':data[self.field]=self.value
                super().reply(data)
        for field,value in [('protocol',True),('session',456)]:
            with self.subTest(field=field):
                device=BadReady(field,value)
                link=SerialLink(serial_factory=lambda:device,session_factory=lambda:123)
                link.connect('MEMORY');until(lambda:link.snapshot().state=='error')
                link.close();self.assertEqual(device.frames(),[])
    def test_framer_discards_oversize_line_through_lf_then_recovers(self):
        parser=_JsonLines(128)
        self.assertEqual(parser.feed(b'x'*10000+b'{"kind":"sim"}\n'),[])
        self.assertEqual(parser.feed(b'{"kind":"sim",'),[])
        self.assertEqual(parser.feed(b'"live":false}\r\n'),[{'kind':'sim','live':False}])
        self.assertEqual(parser.feed(b'\xff\nnotjson\n[]\n'),[])
        self.assertEqual(parser.feed(b'{"ok":1}\n'),[{'ok':1}])

if __name__=='__main__': unittest.main()
