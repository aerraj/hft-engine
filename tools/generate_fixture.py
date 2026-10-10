"""Generate deterministic single-symbol ITCH A/X fixture; no external data."""
import struct
from pathlib import Path
p = Path(__file__).resolve().parents[1] / 'tests/fixtures/synthetic.itch'
p.parent.mkdir(parents=True, exist_ok=True)
def add(i, side, qty, price):
    return b'A' + bytes(10) + struct.pack('>QcI8sI', i, side, qty, b'TEST    ', price)
def cancel(i, qty):
    return b'X' + bytes(10) + struct.pack('>QI', i, qty)
with p.open('wb') as f:
    events = [add(1,b'B',100,100000), add(2,b'S',10,100002)]
    for i in range(3,1003):
        events += [add(i,b'B',1,99999),cancel(i,1)]
    for e in events:
        f.write(struct.pack('>H',len(e))+e)
print(p)
