#!/usr/bin/env python3
"""Export the recovered Mono III active crop without changing raw data.

Uses only the Python standard library. Unsigned 16-bit counts are represented
by FITS BITPIX=16, BSCALE=1, BZERO=32768, in big-endian storage order.
"""
import argparse
import array
import csv
import datetime
import hashlib
import json
from pathlib import Path
import sys

RAW_WIDTH, RAW_HEIGHT = 1434, 1052
WIDTH, HEIGHT, LEFT, TOP = 1392, 1040, 31, 12
RAW_BYTES = RAW_WIDTH * RAW_HEIGHT * 2
FULL_ROI = (0, 0, WIDTH, HEIGHT)


def validate_roi(roi):
    if len(roi) != 4 or any(type(n) is not int for n in roi):
        raise ValueError('ROI requires integer X Y WIDTH HEIGHT')
    x, y, width, height = roi
    if not (0 <= x < WIDTH and 0 <= y < HEIGHT and 1 <= width <= WIDTH-x and 1 <= height <= HEIGHT-y):
        raise ValueError('ROI is outside the active sensor')
    return tuple(roi)


def image_geometry(binning, roi=None):
    if binning not in (1, 2):
        raise ValueError('Require binning 1 or 2')
    if roi is not None:
        x, y, width, height = validate_roi(roi)
        if binning == 2 and any(n % 2 for n in (x, y, width, height)):
            raise ValueError('Bin-2 ROI coordinates and dimensions must be even')
        return {'raw_width': RAW_WIDTH, 'raw_height': height//binning+TOP,
                'width': width//binning, 'height': height//binning, 'raw_x0': (LEFT//binning)*binning+x,
                'raw_y0': TOP//binning, 'column_step': binning, 'binning': binning}
    return {'raw_width': RAW_WIDTH, 'raw_height': 1052 if binning == 1 else 532,
            'width': WIDTH//binning, 'height': HEIGHT//binning,
            'raw_x0': 31 if binning == 1 else 30, 'raw_y0': 12//binning,
            'column_step': binning, 'binning': binning}


def recorded_geometry(directory):
    p = Path(directory)/'geometry.json'
    if not p.exists():
        return None
    metadata = json.loads(p.read_text())
    binning = metadata['bin']
    roi = validate_roi(metadata['roi'])
    g = image_geometry(binning, roi)
    if metadata.get('raw_width') != g['raw_width'] or metadata.get('raw_height') != g['raw_height'] or metadata.get('raw_bytes') != g['raw_width']*g['raw_height']*2:
        raise ValueError('Inconsistent acquisition geometry metadata')
    return binning, roi


def active_pixels(raw, binning=1, roi=None):
    g = image_geometry(binning, roi)
    expected = g['raw_width'] * g['raw_height'] * 2
    if len(raw) != expected:
        raise ValueError(f'Expected exactly {expected} raw bytes, got {len(raw)}')
    values = array.array('H')
    values.frombytes(raw)
    if sys.byteorder != 'little':
        values.byteswap()
    active = array.array('H')
    for y in range(g['raw_y0'], g['raw_y0'] + g['height']):
        start = y*RAW_WIDTH + g['raw_x0']
        row = values[start:start+g['width']*binning:binning]
        active.extend((v & 0xfffe for v in row) if binning == 2 else row)
    return active


def transfer_metadata(directory, exposure_ms, roi=None):
    directory = Path(directory)
    rows = list(csv.DictReader((directory/'usb-transfers.tsv').open(), delimiter='\t'))
    tx = (directory/'usb-out-requests.bin').read_bytes()
    rx = (directory/'usb-in.bin').read_bytes()
    decoded, pending = [], None
    for row in rows:
        if int(row['result']) != 0:
            raise ValueError('Cannot derive acquisition metadata from a failed transfer')
        if row['endpoint_hex'] == '08':
            off = int(row['stream_offset'])
            request = tx[off:off+int(row['stored_bytes'])]
            if len(request) != 6 or request[0] != 0xa5:
                raise ValueError('Unexpected command request in transfer evidence')
            pending = (request, row)
        elif row['requested'] == '8':
            off = int(row['stream_offset'])
            reply = rx[off:off+int(row['actual'])]
            if pending is None or len(reply) != 8 or reply[0] != 0xa5 or reply[2] != pending[0][1]:
                raise ValueError('Unmatched command reply in transfer evidence')
            request, before = pending
            decoded.append({'request': request.hex(' '), 'reply': reply.hex(' '),
                            'opcode': request[1], 'result0': reply[4], 'result1': reply[6],
                            'out_unix': float(before['end_unix_seconds']),
                            'out_monotonic': float(before['end_monotonic_seconds']),
                            'reply_monotonic': float(row['end_monotonic_seconds'])})
            pending = None
    if pending:
        raise ValueError('Command without a reply in evidence')
    starts = [d for d in decoded if d['opcode'] == 3]
    if len(starts) != 1:
        raise ValueError('Require exactly one exposure per raw file')
    start = starts[0]
    command = bytes.fromhex(start['request'])
    ticks = int.from_bytes(command[3:5], 'big')
    encoded_ms = ticks * (100 if command[2] & 1 else 1)
    if encoded_ms != exposure_ms:
        raise ValueError('Requested exposure disagrees with encoded command')
    if command[2] & 0xf0 or command[5] not in (3, 7):
        raise ValueError('Unsupported exposure flags or acquisition mode')
    binning = 1 if command[5] == 3 else 2
    g = image_geometry(binning, roi)
    speed = (-((command[2] >> 1) & 7)) & 7
    speed = speed or 8
    windows = [d for d in decoded if d['opcode'] == 11]
    y = roi[1]//binning if roi is not None else 0
    expected_window = bytes([0xa5, 11, y >> 8, y & 255, g['raw_height'] >> 8, g['raw_height'] & 255])
    if len(windows) != 1 or bytes.fromhex(windows[0]['request']) != expected_window:
        raise ValueError('FITS reconstruction requires the corresponding acquisition window')
    after = [d for d in decoded if d['opcode'] == 2 and d['out_monotonic'] > start['out_monotonic']]
    ready = next((d for d in after if d['result0'] in (0, 2)), None)
    busy = [d for d in after if d['result0'] == 1 and (ready is None or d['reply_monotonic'] < ready['reply_monotonic'])]
    return {'binning': binning, 'readout_speed': speed, 'date_obs': datetime.datetime.fromtimestamp(start['out_unix'], datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%S.%f'),
            'timestamp_basis': 'Host command-03 bulk OUT completion, not measured shutter-open time',
            'host_ready_elapsed_ms': (ready['reply_monotonic']-start['out_monotonic'])*1000 if ready else None,
            'host_last_busy_elapsed_ms': (busy[-1]['reply_monotonic']-start['out_monotonic'])*1000 if busy else None,
            'command_exchanges': decoded}


def card(key, value, comment=''):
    if len(key) > 8:
        raise ValueError('FITS keyword exceeds eight characters')
    if isinstance(value, bool):
        text = ('T' if value else 'F').rjust(20)
    elif isinstance(value, str):
        text = "'" + value.replace("'", "''") + "'"
    elif isinstance(value, int):
        text = str(value).rjust(20)
    else:
        text = format(value, '.10G').rjust(20)
    line = f'{key:<8}= {text}'
    if comment:
        line += ' / ' + comment
    if len(line) > 80:
        raise ValueError(f'FITS card too long: {key}')
    return line.ljust(80).encode('ascii')


def export_fits(raw_path, output_path, *, exposure_ms, image_type, trace_dir=None, binning=None, roi=None):
    raw_path, output_path = Path(raw_path), Path(output_path)
    if image_type not in ('DARK', 'LIGHT') or not 1 <= exposure_ms <= 60000:
        raise ValueError('Require DARK/LIGHT and a supported requested exposure')
    recorded = recorded_geometry(trace_dir) if trace_dir else None
    if recorded is not None:
        recorded_bin, recorded_roi = recorded
        if binning is not None and binning != recorded_bin:
            raise ValueError('Explicit binning disagrees with acquisition metadata')
        if roi is not None and validate_roi(roi) != recorded_roi:
            raise ValueError('Requested ROI differs from acquisition metadata')
        binning = recorded_bin
        roi = recorded_roi
    if roi is not None:
        roi = validate_roi(roi)
    raw = raw_path.read_bytes()
    sha = hashlib.sha256(raw).hexdigest()
    trace = transfer_metadata(trace_dir, exposure_ms, roi) if trace_dir else {}
    if binning is not None and 'binning' in trace and binning != trace['binning']:
        raise ValueError('Explicit binning disagrees with recorded exposure command')
    binning = trace.get('binning', 1 if binning is None else binning)
    g = image_geometry(binning, roi)
    active = active_pixels(raw, binning, roi)
    fields = [('SIMPLE', True, 'Conforms to FITS standard'),
              ('BITPIX', 16, 'Signed storage; physical counts via BZERO'),
              ('NAXIS', 2, ''), ('NAXIS1', g['width'], ''), ('NAXIS2', g['height'], ''),
              ('BSCALE', 1, ''), ('BZERO', 32768, 'Unsigned 16-bit physical counts'),
              ('BUNIT', 'count', 'Uncalibrated digital counts'),
              ('INSTRUME', 'Orion StarShoot Mono III', ''),
              ('IMAGETYP', image_type, 'Acquisition setup supplied by operator'),
              ('EXPTIME', exposure_ms/1000, 'Requested seconds; not precision calibrated'),
              ('EXPMS', exposure_ms, 'Requested milliseconds'),
              ('TIMESYS', 'UTC', ''), ('TIMEBASE', 'HOST' if trace else 'UNSET', 'Timestamp basis'),
              ('XBINNING', binning, ''), ('YBINNING', binning, ''),
              ('PROFILE', 5, ''),
              ('RAWWIDTH', g['raw_width'], ''), ('RAWHEIGH', g['raw_height'], ''),
              ('CROPX0', g['raw_x0'], 'Zero-based raw column offset'),
              ('CROPY0', g['raw_y0'], 'Zero-based raw row offset'),
              ('COLSTEP', g['column_step'], 'Raw sample column increment'),
              ('LSBMASK', binning == 2, 'Bit 0 cleared in bin-2 reconstruction'),
              ('ROWORDER', 'RAW', 'No vertical flip or row reordering'),
              ('RAWFILE', raw_path.name, ''), ('RAWSHA', sha, '')]
    x, y, _, _ = roi or FULL_ROI
    fields.extend([('ROI_X0', x, 'Zero-based origin in active sensor'),
                   ('ROI_Y0', y, 'Zero-based origin in active sensor')])
    if 'readout_speed' in trace:
        fields.append(('READSPD', trace['readout_speed'], 'From recorded exposure command'))
    if trace.get('date_obs'):
        fields.append(('DATE-OBS', trace['date_obs'], 'Host command-03 OUT completion'))
    header = b''.join(card(*f) for f in fields)
    header += b'HISTORY Uncalibrated counts; no bias, dark or flat subtraction.'.ljust(80)
    header += b'HISTORY Orientation, ADC significance and optical margins unverified.'.ljust(80)
    if binning == 2:
        header += b'HISTORY Bin 2 selects alternate columns and clears count bit 0.'.ljust(80)
    header += b'END'.ljust(80)
    header += b' ' * (-len(header) % 2880)
    # XOR maps uint16 to the two's-complement storage of (value - 32768).
    storage = array.array('H', (v ^ 0x8000 for v in active))
    if sys.byteorder == 'little':
        storage.byteswap()
    data = storage.tobytes()
    with output_path.open('xb') as f:
        f.write(header)
        f.write(data)
        f.write(b'\0' * (-len(data) % 2880))
    if hashlib.sha256(raw_path.read_bytes()).hexdigest() != sha:
        raise RuntimeError('Raw file changed during export')
    result = {'fits_file': output_path.name, 'fits_sha256': hashlib.sha256(output_path.read_bytes()).hexdigest(),
              'raw_file': raw_path.name, 'raw_sha256': sha, 'raw_bytes': len(raw),
              'width': g['width'], 'height': g['height'], 'crop_x0': g['raw_x0'], 'crop_y0': g['raw_y0'],
              'binning': binning, 'raw_width': g['raw_width'], 'raw_height': g['raw_height'],
              'raw_column_step': binning, 'clear_bit0': binning == 2,
              'requested_exposure_ms': exposure_ms, 'image_type': image_type,
              'unsigned_storage': {'BITPIX': 16, 'BSCALE': 1, 'BZERO': 32768},
              'calibration': 'None; active reconstruction only; original raw data retained', **trace}
    result['roi'] = list(roi or FULL_ROI)
    with output_path.with_suffix('.fits.json').open('x') as f:
        json.dump(result, f, indent=2)
        f.write('\n')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('raw', type=Path)
    p.add_argument('fits', type=Path)
    p.add_argument('--exposure-ms', type=int, required=True)
    p.add_argument('--image-type', choices=['DARK', 'LIGHT'], required=True)
    p.add_argument('--trace-dir', type=Path)
    p.add_argument('--bin', dest='binning', type=int, choices=(1, 2), help='Required for bin-2 export without transfer logs; checked against logs when supplied')
    p.add_argument('--roi', type=int, nargs=4, metavar=('X', 'Y', 'WIDTH', 'HEIGHT'))
    a = p.parse_args()
    print('Exporting active image using recorded or supplied binning; retaining raw file', flush=True)
    result = export_fits(a.raw, a.fits, exposure_ms=a.exposure_ms,
                         image_type=a.image_type, trace_dir=a.trace_dir, binning=a.binning, roi=a.roi)
    print(f"Saved {a.fits}; raw SHA-256 unchanged: {result['raw_sha256']}", flush=True)


if __name__ == '__main__':
    main()
