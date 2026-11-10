"""Offline INDI XML integration against the compile-time synthetic backend only."""
import base64
import csv
import io
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time
import xml.etree.ElementTree as ET
import zlib

import numpy as np
from astropy.io import fits

DEVICE='OpenStarShoot Mono III'
exe=Path(sys.argv[1]).resolve()
if not exe.name.endswith('_mock'):
    raise SystemExit('This test accepts only the synthetic-backend executable; never a physical driver.')
output=os.environ.get('OPENSTARSHOOT_TEST_OUTPUT')
temporary=None
if output:
    root=Path(output);root.mkdir(parents=True,exist_ok=False)
else:
    temporary=tempfile.TemporaryDirectory(prefix='openstarshoot-indi-');root=Path(temporary.name)
(root/'home').mkdir()
env=os.environ.copy();env['HOME']=str(root/'home');env['OPENSTARSHOOT_RAW_DIRECTORY']=str(root/'raw')
errors=(root/'driver-stderr.txt').open('wb')
p=subprocess.Popen([str(exe)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=errors,env=env)
events=queue.Queue();definitions={};reader_errors=[]

def reader():
    parser=ET.XMLPullParser(events=['end']);parser.feed('<stream>')
    try:
        with (root/'driver-output.xml').open('wb') as log:
            for b in iter(p.stdout.readline,b''):
                log.write(b);log.flush()
                # INDI emits an XML declaration before each top-level vector.
                if b.strip().startswith(b'<?xml'):continue
                parser.feed(b.decode('utf-8'))
                if hasattr(parser,'flush'):parser.flush()
                for _,e in parser.read_events():
                    if e.tag.startswith(('def','set')) and e.tag.endswith('Vector') or e.tag in ('message','delProperty','pingRequest'):
                        copy=ET.fromstring(ET.tostring(e));events.put(copy)
                        if e.tag.startswith('def'):definitions[e.get('name')]=copy
                        e.clear()
    except Exception as exc:reader_errors.append(str(exc))
thread=threading.Thread(target=reader,daemon=True);thread.start()
def send(s):p.stdin.write((s+'\n').encode());p.stdin.flush()
def wait(predicate,description,timeout=12):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        if reader_errors:raise AssertionError(reader_errors)
        if p.poll() is not None:raise AssertionError('Driver exited before '+description)
        try:e=events.get(timeout=0.1)
        except queue.Empty:continue
        if e.tag=='pingRequest':
            send('<pingReply uid="'+e.get('uid')+'"/>');continue
        if predicate(e):return e
    raise AssertionError('Timed out: '+description)
def connect(on):
    send(f'<newSwitchVector device="{DEVICE}" name="CONNECTION"><oneSwitch name="CONNECT">{"On" if on else "Off"}</oneSwitch><oneSwitch name="DISCONNECT">{"Off" if on else "On"}</oneSwitch></newSwitchVector>')
    wait(lambda e:e.tag=='setSwitchVector' and e.get('name')=='CONNECTION' and e.get('state')==('Ok' if on else 'Idle') and any(x.get('name')==('CONNECT' if on else 'DISCONNECT') and (x.text or '').strip()=='On' for x in e),'connection '+str(on))
def exposure(index,binning=1,roi=(0,0,1392,1040)):
    x0,y0,w,h=roi
    rh=h//binning+12
    send(f'<newNumberVector device="{DEVICE}" name="CCD_EXPOSURE"><oneNumber name="CCD_EXPOSURE_VALUE">0.1</oneNumber></newNumberVector>')
    e=wait(lambda e:e.tag=='setBLOBVector' and e.get('name')=='CCD1','FITS delivery')
    wait(lambda v:v.tag=='setNumberVector' and v.get('name')=='CCD_EXPOSURE' and v.get('state')=='Ok','exposure completion')
    b=e.find('oneBLOB');assert b is not None
    data=base64.b64decode(''.join(b.itertext()))
    if b.get('format','').endswith('.z'):data=zlib.decompress(data)
    path=root/f'image-{index}.fits';path.write_bytes(data)
    directories=sorted((root/'raw').glob('frame-*'),key=lambda d:d.stat().st_mtime_ns)
    assert len(directories)==index
    d=directories[-1];raw=(d/'frame.raw').read_bytes();assert len(raw)==1434*rh*2
    values=np.frombuffer(raw,dtype='<u2').reshape(rh,1434)
    y,x=np.indices((rh,1434),dtype=np.uint32);expected=((y*137+x*31)^0xabcd).astype(np.uint16)
    assert np.array_equal(values,expected)
    with fits.open(io.BytesIO(data),uint=True,memmap=False) as hdus:
        hdus.verify('exception');h=hdus[0]
        assert h.data.shape==(roi[3]//binning,roi[2]//binning) and h.data.dtype==np.uint16
        expected_active=(values[6:6+roi[3]//2,30+x0:30+x0+w:2]&0xfffe) if binning==2 else values[12:rh,31+x0:31+x0+w]
        assert np.array_equal(h.data,expected_active)
        assert h.header['XBINNING']==binning and h.header['YBINNING']==binning
        assert h.header['BITPIX']==16 and h.header['BZERO']==32768
        assert abs(h.header['EXPTIME']-0.1)<1e-6
    rows=list(csv.DictReader((d/'usb-transfers.tsv').open(),delimiter='\t'));rx=(d/'usb-in.bin').read_bytes()
    image=b''.join(rx[int(r['stream_offset']):int(r['stream_offset'])+int(r['actual'])] for r in rows if int(r['requested'])==1024)
    assert image==raw and 'final_state=0' in (d/'capture.txt').read_text()
    tx=(d/"usb-out-requests.bin").read_bytes()
    commands=[tx[i:i+6] for i in range(0,len(tx),6)]
    assert [c for c in commands if c[1]==3]==[bytes((0xa5,3,6,0,100,7 if binning==2 else 3))]
    assert [c for c in commands if c[1]==11]==[bytes((0xa5,11,(y0//binning)>>8,(y0//binning)&255,rh>>8,rh&255))]
    assert all(int(r['result'])==0 for r in rows)
    print(f'PASS exposure {index}: bin {binning}, frame {roi}, every-pixel FITS/raw/log match and idle cleanup',flush=True)
def numbers(name,items,state='Ok',expected=None):
    send(f'<newNumberVector device="{DEVICE}" name="{name}">'+''.join(f'<oneNumber name="{key}">{value}</oneNumber>' for key,value in items.items())+'</newNumberVector>')
    e=wait(lambda e:e.tag=='setNumberVector' and e.get('name')==name and e.get('state')==state,f'{name} response')
    assert e.get('state')==state,ET.tostring(e)
    if expected is not None:
        assert {v.get('name'):float(v.text) for v in e}==expected,ET.tostring(e)
def binning(value,state='Ok',expected=None):numbers('CCD_BINNING',{'HOR_BIN':value,'VER_BIN':value},state,expected)
def frame(roi,state='Ok',expected=None):numbers('CCD_FRAME',dict(zip(('X','Y','WIDTH','HEIGHT'),roi)),state,expected)
try:
    print('INDI offline test: real adapter, synthetic transport, native reconstruction; no camera access',flush=True)
    send('<getProperties version="1.7"/>')
    wait(lambda e:e.tag=='defSwitchVector' and e.get('name')=='CONNECTION','connection property')
    connect(True)
    wait(lambda e:e.tag=='defBLOBVector' and e.get('name')=='CCD1','image property')
    assert definitions['CCD_FRAME'].get('perm')=='rw'
    assert definitions['CCD_BINNING'].get('perm')=='rw'
    assert all(float(v.get('min'))==1 and float(v.get('max'))==2 for v in definitions['CCD_BINNING'])
    assert 'CCD_TEMPERATURE' not in definitions and 'CCD_ABORT_EXPOSURE' not in definitions
    exposure(1)
    binning(2);exposure(2,2)
    full={'X':0.,'Y':0.,'WIDTH':1392.,'HEIGHT':1040.}
    frame((73,101,512,512),'Alert',full)
    exposure(3,2)  # rejected ROI leaves the previous bin-2 full frame intact
    binning(1);exposure(4)
    frame((440,264,512,512));exposure(5,1,(440,264,512,512))
    binning(2);exposure(6,2,(440,264,512,512))
    frame((74,102,512,512));exposure(7,2,(74,102,512,512))
    binning(1)
    frame((73,101,512,512));exposure(8,1,(73,101,512,512))
    frame((1391,1039,512,512),'Alert',{'X':73.,'Y':101.,'WIDTH':512.,'HEIGHT':512.})
    numbers('CCD_BINNING',{'HOR_BIN':2,'VER_BIN':1},'Alert',{'HOR_BIN':1.,'VER_BIN':1.})
    connect(False);connect(True)
    wait(lambda e:e.tag=='defBLOBVector' and e.get('name')=='CCD1','reconnected image property')
    exposure(9);connect(False)
    print('PASS: frame/binning properties, full-frame 1x1/2x2, center/offset ROI, unsupported combinations retain state, reconnect defaults',flush=True)
    print('Evidence:',root,flush=True)
finally:
    p.terminate()
    try:p.wait(timeout=4)
    except subprocess.TimeoutExpired:p.kill();p.wait()
    thread.join(timeout=2);errors.close()
    if temporary:temporary.cleanup()
