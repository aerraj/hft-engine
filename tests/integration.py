import csv, importlib.util, json, struct, subprocess, sys, tempfile
from pathlib import Path
exe,root=sys.argv[1],Path(sys.argv[2])
spec=importlib.util.spec_from_file_location('report',root/'tools/report.py');report=importlib.util.module_from_spec(spec);spec.loader.exec_module(report)
def expected(path):
    orders={};serial=0;snapshots={}
    with open(path,'rb') as f:
        count=0
        while length:=f.read(2):
            b=f.read(int.from_bytes(length,'big'));count+=1;t=chr(b[0]);loc=int.from_bytes(b[1:3],'big')
            if t in 'AF':
                id,side,qty,symbol,price=struct.unpack('>QcI8sI',b[11:36]);serial+=1;orders[id]=[loc,side.decode(),price,qty,serial]
            elif t in 'XEC':
                id,qty=struct.unpack('>QI',b[11:23]);orders[id][3]-=qty
                if not orders[id][3]:del orders[id]
            elif t=='D':del orders[int.from_bytes(b[11:19],'big')]
            elif t=='U':
                old,new,qty,price=struct.unpack('>QQII',b[11:]);loc,side,_,_,_=orders.pop(old);serial+=1;orders[new]=[loc,side,price,qty,serial]
            if count%1000==0:snapshots[count]={id:row.copy() for id,row in orders.items()}
    snapshots[count]=orders
    return count,snapshots
def compare(path,ref):
    with open(path) as f:rows=list(csv.DictReader(f))
    assert len(rows)==len(ref),(path,len(rows),len(ref))
    for r in rows:
        id=int(r['id']);loc,side,price,qty,serial=ref[id]
        assert [int(r['locate']),r['side'],int(r['price']),int(r['qty'])]==[loc,side,price,qty]
        ahead=sum(v[3] for v in ref.values() if v[0]==loc and v[1]==side and v[2]==price and v[4]<serial)
        assert int(r['queue_ahead'])==ahead
with tempfile.TemporaryDirectory() as d:
    d=Path(d);fixture=root/'tests/fixtures/synthetic.itch';count,snapshots=expected(fixture)
    for strategy in ('maker','taker'):
        outputs=[]
        for run in (1,2):
            prefix=d/f'{strategy}{run}';subprocess.run([exe,strategy,str(fixture),str(prefix),'--snapshot-every','1000','--metrics'],check=True);outputs.append(prefix)
            summary=json.loads(prefix.with_suffix('.json').read_text());assert summary['messages']==count
            assert summary['orders']>0 and summary['fills']>0,summary
            compare(str(prefix)+'.book.csv',snapshots[count])
            for n,ref in snapshots.items():
                if n%1000==0:compare(str(prefix)+f'.snapshot.{n}.csv',ref)
            result,_,_,_=report.report(str(prefix)+'.orders');assert result['filled_qty']==summary['fill_qty']
        assert outputs[0].with_suffix('.orders').read_bytes()==outputs[1].with_suffix('.orders').read_bytes()
        assert outputs[0].with_suffix('.json').read_bytes()==outputs[1].with_suffix('.json').read_bytes()
    malformed=d/'truncated.itch';malformed.write_bytes(fixture.read_bytes()[:-1]);assert subprocess.run([exe,'maker',str(malformed),str(d/'bad')],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode!=0
print('Golden book snapshots, both strategy fills, deterministic logs and truncated-input rejection passed')
