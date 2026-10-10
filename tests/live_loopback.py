import socket,struct,subprocess,sys,tempfile,time
from pathlib import Path
exe,root=sys.argv[1],Path(sys.argv[2]);events=[]
with (root/'tests/fixtures/synthetic.itch').open('rb') as f:
    while n:=f.read(2):events.append(f.read(int.from_bytes(n,'big')))
with tempfile.TemporaryDirectory() as d:
    d=Path(d)
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:
        probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
    p=subprocess.Popen([exe,'maker',str(port),str(d/'orders'),str(d/'control.sock'),str(len(events))],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:
        for _ in range(100):
            if (d/'control.sock').exists():break
            if p.poll() is not None:raise RuntimeError(p.communicate())
            time.sleep(.01)
        time.sleep(.05)
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sender:
            sequence=1
            for start in range(0,len(events),10):
                batch=events[start:start+10];packet=b'TEST      '+struct.pack('>QH',sequence,len(batch))+b''.join(struct.pack('>H',len(b))+b for b in batch)
                sender.sendto(packet,('127.0.0.1',port));sequence+=len(batch);time.sleep(.001)
        out,err=p.communicate(timeout=30);assert p.returncode==0,(out,err);assert f'"messages":{len(events)}' in out,out
    finally:
        if p.poll() is None:p.kill();p.wait()
print('MoldUDP64 UDP loopback passed')
