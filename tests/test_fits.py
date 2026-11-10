"""Independent synthetic bin-1/bin-2 reconstruction and acquisition metadata checks."""
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

import numpy as np
from astropy.io import fits

path = Path(__file__).resolve().parents[1]/'tools/fits_export.py'
spec = importlib.util.spec_from_file_location('fits_export', path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def saved_trace(directory, raw, binning):
    """Synthetic application buffer logs with the normal acquisition sequence."""
    tx, rx, rows = bytearray(), bytearray(), []
    sequence = 0
    def record(ep, request, data):
        nonlocal sequence
        sequence += 1
        stream = tx if ep == '08' else rx
        offset = len(stream); stream.extend(data)
        rows.append([sequence, 1700000000+sequence/1000, sequence/1000, ep, request, len(data), 0, offset, len(data)])
    def exchange(command, result):
        record('08', 6, bytes(command))
        record('82', 8, bytes([0xa5, 0, command[1], 0, result, 0, 0, 0]))
    height = 1052 if binning == 1 else 532
    exchange([0xa5, 1, 0, 0, 0, 0], 5)
    exchange([0xa5, 11, 0, 0, height>>8, height&255], 1)
    exchange([0xa5, 3, 6, 0, 100, 3 if binning == 1 else 7], 1)
    exchange([0xa5, 2, 0, 0, 0, 0], 2)
    exchange([0xa5, 4, 0, 0, 0, 0], 1)
    for i in range(0, len(raw), 1024):record('82', 1024, raw[i:i+1024])
    exchange([0xa5, 5, 0, 0, 0, 0], 1)
    exchange([0xa5, 2, 0, 0, 0, 0], 0)
    (directory/'usb-out-requests.bin').write_bytes(tx)
    (directory/'usb-in.bin').write_bytes(rx)
    with (directory/'usb-transfers.tsv').open('w') as f:
        writer = csv.writer(f, delimiter='\t')
        writer.writerow(['sequence','end_unix_seconds','end_monotonic_seconds','endpoint_hex','requested','actual','result','stream_offset','stored_bytes'])
        writer.writerows(rows)


with tempfile.TemporaryDirectory(prefix='starshoot-fits-') as temporary:
    for binning in (1, 2):
        directory = Path(temporary)/str(binning);directory.mkdir()
        raw_height = 1052 if binning == 1 else 532
        values = (np.arange(1434*raw_height, dtype=np.uint32).reshape(raw_height,1434)*37+19)%65536
        top,left = (12,31) if binning == 1 else (6,30)
        values[top, left:left+4*binning:binning] = [0,32767,32768,65535]
        raw_data = values.astype('<u2').tobytes()
        raw = directory/'synthetic.raw';raw.write_bytes(raw_data)
        before = hashlib.sha256(raw_data).hexdigest()
        expected = values[top:top+1040//binning,left:left+(1392//binning)*binning:binning].astype(np.uint16)
        if binning == 2:expected &= np.uint16(0xfffe)
        saved_trace(directory, raw_data, binning)
        if binning == 2:
            (directory/'geometry.json').write_text(json.dumps({'bin':2,'roi':[0,0,1392,1040],
                'raw_width':1434,'raw_height':532,'raw_bytes':1525776}))
        out = directory/'active.fits'
        print(f'FITS bin {binning}: actual-mode inference, every pixel and unsigned storage', flush=True)
        result = module.export_fits(raw,out,exposure_ms=100,image_type='LIGHT',trace_dir=directory)
        with fits.open(out,uint=True,memmap=False) as h:
            h.verify('exception');data=h[0].data;header=h[0].header
            assert data.dtype == np.uint16 and np.array_equal(data,expected)
            assert data.shape == (1040//binning,1392//binning)
            assert header['XBINNING']==header['YBINNING']==binning
            assert header['RAWHEIGH']==raw_height and header['READSPD']==5
            assert header['CROPX0']==left and header['CROPY0']==top
            assert header['COLSTEP']==binning and bool(header['LSBMASK'])==(binning==2)
            assert header['BITPIX']==16 and header['BZERO']==32768 and header['BSCALE']==1
        with fits.open(out,do_not_scale_image_data=True) as h:
            assert h[0].data[0,:4].tolist()==([-32768,-1,0,32767] if binning==1 else [-32768,-2,0,32766])
        assert result['binning']==binning and result['readout_speed']==5
        assert out.stat().st_size%2880==0 and hashlib.sha256(raw.read_bytes()).hexdigest()==before
        if binning==1:
            # Original no-trace bin-1 API remains valid without a camera.
            module.export_fits(raw,directory/'legacy.fits',exposure_ms=100,image_type='DARK')
        try:module.export_fits(raw,directory/'mismatch.fits',exposure_ms=100,image_type='LIGHT',trace_dir=directory,binning=3-binning)
        except ValueError:pass
        else:raise AssertionError('Contradictory binning metadata must be rejected')
        print(f'PASS bin {binning}: crop, counts, recorded mode/window, FITS structure and raw preservation',flush=True)
print('All synthetic FITS checks passed. No camera opened.',flush=True)
