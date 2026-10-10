"""Select on session i, evaluate once on held-out session i+1; books reset per session."""
import argparse,json,subprocess,tempfile
from pathlib import Path
from report import report
p=argparse.ArgumentParser();p.add_argument('binary');p.add_argument('strategy',choices=['maker','taker']);p.add_argument('files',nargs='+');p.add_argument('--out',required=True);a=p.parse_args()
if len(a.files)<2:raise SystemExit('provide at least two chronologically ordered independent full sessions')
folds=[]
with tempfile.TemporaryDirectory() as d:
    d=Path(d)
    def run(file,value,name):
        prefix=d/name;flag='--half-spread' if a.strategy=='maker' else '--threshold-bps'
        subprocess.run([a.binary,a.strategy,file,str(prefix),flag,str(value)],check=True,stdout=subprocess.DEVNULL)
        return report(str(prefix)+'.orders')[0]
    grid=[1,2,3] if a.strategy=='maker' else [1000,2000,3000]
    for i in range(len(a.files)-1):
        trials=[(value,run(a.files[i],value,f'train-{i}-{value}')) for value in grid]
        value,training=max(trials,key=lambda x: x[1]['net_pnl_ticks'] if x[1]['net_pnl_ticks'] is not None else float('-inf'))
        held_out=run(a.files[i+1],value,f'test-{i}')
        folds.append(dict(training=a.files[i],held_out=a.files[i+1],selected=value,training_result=training,held_out_result=held_out))
Path(a.out).write_text(json.dumps(dict(strategy=a.strategy,folds=folds,selection='training-only grid; no future session used'),indent=2)+'\n')
