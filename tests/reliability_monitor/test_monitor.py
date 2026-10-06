"""Run the read-only PowerShell monitor against a deterministic local API."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[2]
POWERSHELL = shutil.which('pwsh') or shutil.which('powershell')
IDENTITY = dict(board_id='m3', project='main-deck-m3', source_sha='a'*40,
                source_dirty=False, image_elf_sha256='b'*64)
STATUS = dict(IDENTITY, uptime_ms=1000,
    controller=dict(present=True, midi_in=True, midi_out=True, usb_audio=True),
    deck1=dict(playing=True, master_tempo=True, loop_active=True, pitch_percent=5, position_ms=100),
    deck2=dict(playing=True, master_tempo=True, loop_active=True, pitch_percent=-5, position_ms=100),
    diagnostics=dict(deck_sample_rate1=44100, deck_sample_rate2=48000, output_sample_rate=48000,
        output_late_count=0, output_late_max_us=0, pcm_underrun1=0, pcm_underrun2=0,
        usb_headphones=dict(dropped_blocks=0, overflow_frames=0, underflow_frames=0,
                            ring_state=0, data_loss=False), internal_free=50000, psram_free=6000000),
    service_log=dict(dropped=0, queue_depth=0))
FIRMWARE = dict(IDENTITY, running_version='M3-dev-gaaaaaaaaaaaa', running_slot='ota_0', state='idle')


class MonitorTest(unittest.TestCase):
    def run_monitor(self, change=None, initial=None, mode='TimingSoak', library_count=191):
        self.assertIsNotNone(POWERSHELL, 'PowerShell is required for the monitor gate')
        requests=[]
        class Handler(BaseHTTPRequestHandler):
            status_count=0
            def do_GET(self):
                requests.append(('GET', self.path))
                if self.path=='/api/status':
                    Handler.status_count+=1
                    body=copy.deepcopy(STATUS)
                    body['uptime_ms']+=Handler.status_count*100
                    if initial: initial(body)
                    if change and Handler.status_count>1: change(body)
                elif self.path=='/api/firmware': body=FIRMWARE
                elif self.path=='/api/library': body={'generation':1, 'tracks':[{'track_key':i+1} for i in range(library_count)]}
                elif self.path=='/api/diagnostic-log': body={'records':[]}
                elif self.path=='/api/resources': body=dict(allocation_failures=0,critical_allocation_failures=0,
                    stack_min_bytes=[2048]*9,stack_sample_ms=[1000]*9)
                else: raise AssertionError(self.path)
                data=json.dumps(body).encode()
                self.send_response(200);self.send_header('Content-Length',str(len(data)))
                self.end_headers();self.wfile.write(data)
            def do_POST(self):
                requests.append(('POST',self.path));self.send_error(500)
            def log_message(self,*args): pass
        server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            with tempfile.TemporaryDirectory() as temp:
                command=[POWERSHELL,'-NoProfile','-ExecutionPolicy','Bypass','-File',
                    str(ROOT/'tools/monitor_p4_reliability.ps1'),'-BaseUrl',
                    f'http://127.0.0.1:{server.server_port}','-DurationSeconds','1',
                    '-PollIntervalMs','100','-LibraryEvery','1','-FirmwareEvery','1',
                    '-ExpectedSourceSha','a'*40,'-ExpectedElfSha256','b'*64,
                    '-Mode',mode,'-OutputDirectory',temp]
                result=subprocess.run(command,text=True,capture_output=True,timeout=40)
                record=json.loads((Path(temp)/'result.json').read_text())
                self.assertTrue(all(method=='GET' for method,path in requests))
                self.assertEqual(result.returncode,record['exit_code'],result.stdout+result.stderr)
                return record
        finally:
            server.shutdown();server.server_close();thread.join(3)

    def test_same_image_and_isolated_late_preserve_pass(self):
        record=self.run_monitor(lambda s:s['diagnostics'].update(output_late_count=1,output_late_max_us=11024))
        self.assertEqual(record['result'],'pass',record)
        self.assertEqual(record['identity'],IDENTITY)
        self.assertEqual(record['delta']['output_late'],1)
        self.assertEqual(record['physical_operator_acceptance'],'NOT RUN')

    def test_strict_audio_loss_is_a_hard_failure(self):
        record=self.run_monitor(lambda s:s['diagnostics']['usb_headphones'].update(dropped_blocks=1))
        self.assertEqual(record['result'],'fail',record)
        self.assertIn('UAC dropped-block counter increased',record['hard_failures'])

    def test_monitoring_threshold_is_investigate(self):
        record=self.run_monitor(lambda s:s['diagnostics'].update(output_late_count=3,output_late_max_us=16000))
        self.assertEqual(record['result'],'investigate',record)

    def test_workload_requires_master_tempo(self):
        record=self.run_monitor(lambda s:s['deck1'].update(master_tempo=False))
        self.assertEqual(record['result'],'fail',record)

    def test_observe_accepts_installed_compact_status_but_soak_requires_workload_telemetry(self):
        def compact(s):
            for deck in ('deck1','deck2'):
                s[deck].pop('master_tempo')
                s[deck].pop('loop_active')
        observed = self.run_monitor(initial=compact, mode='Observe')
        self.assertEqual(observed['result'],'pass',observed)
        self.assertEqual(observed['library_tracks'],191)
        qualified = self.run_monitor(initial=compact)
        self.assertEqual(qualified['result'],'fail',qualified)
        self.assertIn('Required telemetry is missing: deck1.loop_active',qualified['hard_failures'])

    def test_empty_and_single_row_catalogs_keep_their_actual_counts(self):
        for count in (0,1):
            with self.subTest(count=count):
                record=self.run_monitor(mode='Observe',library_count=count)
                self.assertEqual(record['result'],'pass',record)
                self.assertEqual(record['library_tracks'],count)

    def test_wrong_missing_or_changed_identity_cannot_pass(self):
        for change,initial in [
            (lambda s:s.update(source_sha='c'*40),None),
            (lambda s:s.update(uptime_ms=1),None),
            (None,lambda s:s.update(board_id='jc4880')),
            (None,lambda s:s['diagnostics'].pop('pcm_underrun1'))]:
            with self.subTest(initial=bool(initial)):
                record=self.run_monitor(change,initial)
                self.assertEqual(record['result'],'fail',record)


if __name__=='__main__': unittest.main()
