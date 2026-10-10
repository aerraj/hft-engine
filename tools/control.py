"""Send a control request; 'queued' means it will apply at a loop boundary."""
import argparse, socket, tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('socket');p.add_argument('command',nargs='+');a=p.parse_args()
with tempfile.TemporaryDirectory() as d:
    with socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM) as s:
        s.bind(str(Path(d)/'client.sock'));s.settimeout(2)
        s.sendto(' '.join(a.command).encode(),a.socket)
        print(s.recv(4096).decode())
