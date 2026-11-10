"""Independent synthetic ROI FITS checks, optionally through the real CLI parser."""
import argparse,hashlib,importlib.util,json,subprocess,tempfile
from pathlib import Path
import numpy as np
from astropy.io import fits
p=argparse.ArgumentParser();p.add_argument('--cli');args=p.parse_args()
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('fits_export',root/'tools/fits_export.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
with tempfile.TemporaryDirectory(prefix='starshoot-roi-') as tmp:
 d=Path(tmp)
 for i,roi in enumerate([(440,264,512,512),(73,101,512,512)]):
  x,y,w,h=roi;yy,xx=np.indices((h+12,1434),dtype=np.uint32);a=(((yy+y)*137+xx*31)^0xabcd).astype(np.uint16)
  raw=d/f'{i}.raw';out=d/f'{i}.fits';raw.write_bytes(a.astype('<u2').tobytes());before=hashlib.sha256(raw.read_bytes()).hexdigest()
  m.export_fits(raw,out,exposure_ms=100,image_type='LIGHT',roi=roi)
  with fits.open(out,uint=True,memmap=False) as f:
   f.verify('exception');assert f[0].data.shape==(h,w);assert np.array_equal(f[0].data,a[12:12+h,31+x:31+x+w]);assert f[0].header['ROI_X0']==x and f[0].header['ROI_Y0']==y
  assert hashlib.sha256(raw.read_bytes()).hexdigest()==before
  print('PASS: ROI',roi,'all FITS pixels, dimensions, origins and raw preservation',flush=True)
 for i,roi in enumerate([(440,264,512,512),(74,102,512,512)]):
  x,y,w,h=roi;yy,xx=np.indices((h//2+12,1434),dtype=np.uint32);a=((yy*137+xx*31)^0xabcd).astype(np.uint16)
  raw=d/f'bin2-{i}.raw';out=d/f'bin2-{i}.fits';raw.write_bytes(a.astype('<u2').tobytes());before=raw.read_bytes()
  m.export_fits(raw,out,exposure_ms=100,image_type='LIGHT',roi=roi,binning=2)
  with fits.open(out,uint=True,memmap=False) as f:
   f.verify('exception');assert f[0].data.shape==(h//2,w//2) and np.array_equal(f[0].data,a[6:6+h//2,30+x:30+x+w:2]&0xfffe)
   assert f[0].header['ROI_X0']==x and f[0].header['ROI_Y0']==y and f[0].header['XBINNING']==2
  assert raw.read_bytes()==before
  print('PASS bin2 ROI FITS:',roi,flush=True)
 if args.cli:
  capture=d/'capture';subprocess.run([args.cli,str(capture)],check=True)
  m.export_fits(capture/'light.raw',capture/'active.fits',exposure_ms=100,image_type='LIGHT',trace_dir=capture)
  metadata=json.loads((capture/'geometry.json').read_text());assert metadata['roi']==[73,101,512,512]
  raw=np.fromfile(capture/'light.raw',dtype='<u2').reshape(524,1434)
  with fits.open(capture/'active.fits',uint=True,memmap=False) as f:
   f.verify('exception');assert np.array_equal(f[0].data,raw[12:,104:616]);assert f[0].header['ROI_Y0']==101
  print('PASS: unchanged CLI acquisition flow with --roi, synthetic transport, recorded commands/metadata and automatic FITS crop; no camera opened',flush=True)

  capture2=d/'capture-bin2';subprocess.run([args.cli,str(capture2),'2'],check=True)
  m.export_fits(capture2/'light.raw',capture2/'active.fits',exposure_ms=100,image_type='LIGHT',trace_dir=capture2)
  raw=np.fromfile(capture2/'light.raw',dtype='<u2').reshape(268,1434)
  with fits.open(capture2/'active.fits',uint=True,memmap=False) as f:
   assert f[0].data.shape==(256,256) and np.array_equal(f[0].data,raw[6:262,104:616:2]&0xfffe)
   assert f[0].header['ROI_Y0']==102 and f[0].header['YBINNING']==2
  print('PASS bin2 ROI normal CLI to automatic FITS flow',flush=True)
