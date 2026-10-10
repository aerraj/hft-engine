"""Deterministic synthetic multi-symbol ITCH day (not historical exchange data)."""
import argparse, struct, random
from pathlib import Path
SIZES=dict(zip('SRHYL VWKJhAFECXDUPQBIN'.replace(' ',''),[12,39,25,20,26,35,12,28,35,21,36,40,31,36,23,19,35,44,40,19,50,20]))
def generate(path,cycles=1000,seed=7):
    rng=random.Random(seed);events=[];now=34_200_000_000_000;next_id=1
    def packet(t,loc=1):
        nonlocal now
        now+=1_000_000
        b=bytearray(SIZES[t]);b[0]=ord(t);struct.pack_into('>H',b,1,loc);b[5:11]=now.to_bytes(6,'big');return b
    def emit(b):events.append(bytes(b))
    def add(side,qty,price,loc=1,attributed=False):
        nonlocal next_id
        i=next_id;next_id+=1;b=packet('F' if attributed else 'A',loc);struct.pack_into('>QcI8sI',b,11,i,side,qty,b'TEST    ' if loc==1 else b'OTHER   ',price);emit(b);return i
    def remove(t,i,q=0,loc=1):
        b=packet(t,loc);struct.pack_into('>Q',b,11,i)
        if t!='D':struct.pack_into('>I',b,19,q)
        if t in 'EC':struct.pack_into('>Q',b,23,i)
        if t=='C':b[31]=ord('Y');struct.pack_into('>I',b,32,100000)
        emit(b)
    for code in 'OS':b=packet('S',0);b[11]=ord(code);emit(b)
    for loc,symbol in [(1,b'TEST    '),(2,b'OTHER   ')]:
        b=packet('R',loc);b[11:19]=symbol;b[19]=ord('Q');struct.pack_into('>I',b,21,100);b[25]=ord('N');emit(b)
        b=packet('H',loc);b[11:19]=symbol;b[19]=ord('T');emit(b)
    for t in 'YL VKJhIN'.replace(' ',''):emit(packet(t,2))
    b=packet('S',0);b[11]=ord('Q');emit(b)
    # Cover all displayed order mutation types on the other symbol.
    oid=add(b'B',20,100000,2,True);remove('X',oid,3,2);remove('C',oid,2,2)
    b=packet('U',2);replacement=next_id;next_id+=1;struct.pack_into('>QQII',b,11,oid,replacement,15,99999);emit(b);remove('D',replacement,loc=2)
    b=packet('P',2);struct.pack_into('>QcI8sIQ',b,11,0,b'B',3,b'OTHER   ',100001,99);emit(b)
    b=packet('Q',2);struct.pack_into('>Q8sIQc',b,11,100,b'OTHER   ',100001,100,b'C');emit(b)
    b=packet('B',2);struct.pack_into('>Q',b,11,99);emit(b)
    for cycle in range(cycles):
        mid=100000+rng.randrange(-20,21);bid=mid-2;ask=mid+2
        # Resting historical orders arrive first, then our maker quotes, then later orders.
        bid_old=add(b'B',40 if cycle%2==0 else 10,bid)
        ask_old=add(b'S',10 if cycle%2==0 else 40,ask)
        bid_new=add(b'B',20,bid);ask_new=add(b'S',20,ask)
        remove('E',bid_old,40 if cycle%2==0 else 10)
        remove('E',ask_old,10 if cycle%2==0 else 40)
        # Execute later historical orders. Our simulated quotes can fill ahead of them.
        remove('E',bid_new,10);remove('E',ask_new,10)
        remove('D',bid_new);remove('D',ask_new)
    for code in 'MEC':b=packet('S',0);b[11]=ord(code);emit(b)
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    with path.open('wb') as f:
        for b in events:f.write(struct.pack('>H',len(b))+b)
    return len(events)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--out',default=str(Path(__file__).resolve().parents[1]/'tests/fixtures/synthetic.itch'));p.add_argument('--cycles',type=int,default=1000);p.add_argument('--seed',type=int,default=7);a=p.parse_args();print(generate(a.out,a.cycles,a.seed))
