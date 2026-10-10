"""Download public NASDAQ ITCH data, checksum, decompress; never redistribute raw data."""
import argparse,gzip,hashlib,json,shutil,urllib.request
from pathlib import Path
BASE='https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/'
p=argparse.ArgumentParser();p.add_argument('--file',default='12302019.NASDAQ_ITCH50.gz');p.add_argument('--out',required=True);a=p.parse_args()
if '/' in a.file or not a.file.endswith('.gz'):raise SystemExit('expected an ITCH .gz basename')
dest=Path(a.out);dest.parent.mkdir(parents=True,exist_ok=True);compressed=Path(str(dest)+'.gz');url=BASE+a.file
urllib.request.urlretrieve(url,compressed)
md5=hashlib.md5();sha=hashlib.sha256()
with compressed.open('rb') as f:
    while b:=f.read(1024*1024):md5.update(b);sha.update(b)
with urllib.request.urlopen(url+'.md5sum') as response:expected=response.read().decode().split()[0]
if md5.hexdigest()!=expected:raise SystemExit('exchange MD5 checksum mismatch')
with gzip.open(compressed,'rb') as source,dest.open('wb') as target:shutil.copyfileobj(source,target,1024*1024)
Path(str(dest)+'.manifest.json').write_text(json.dumps(dict(source=url,md5=md5.hexdigest(),compressed_sha256=sha.hexdigest()),indent=2))
print(dest)
