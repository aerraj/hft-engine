"""Run independent repetitions; report median percentiles, retain every raw run."""
import argparse,json,statistics,subprocess,hashlib,platform
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('binary');p.add_argument('--out',required=True);p.add_argument('--core',type=int);p.add_argument('--dense',choices=['0','1'],default='1');p.add_argument('--repetitions',type=int,default=20);p.add_argument('--samples',type=int,default=100000);a=p.parse_args()
if a.repetitions<1:raise SystemExit('repetitions must be positive')
prefix=Path(a.out);prefix.parent.mkdir(parents=True,exist_ok=True);runs=[]
for i in range(a.repetitions):
    name=f'{prefix}.run{i+1:02}';command=[a.binary,'--out',name,'--samples',str(a.samples),'--dense',a.dense]
    if a.core is not None:command+=['--core',str(a.core)]
    subprocess.run(command,check=True);runs.append(json.loads(Path(name+'.json').read_text()))
source=hashlib.sha256();repo=Path(__file__).resolve().parents[1]
for file in sorted(list((repo/'include').rglob('*.hpp'))+list((repo/'bench').glob('*.cpp'))):source.update(file.relative_to(repo).as_posix().encode());source.update(file.read_bytes())
result=runs[0].copy();result['source_sha256']=source.hexdigest();result['host']=platform.platform();
try:
    cpuinfo=Path('/proc/cpuinfo').read_text();result['cpu_model']=next(line.split(':',1)[1].strip() for line in cpuinfo.splitlines() if line.startswith('model name'))
except (OSError,StopIteration):result['cpu_model']=platform.processor() or 'unavailable'
result['repetitions']=a.repetitions;result['aggregation']='median of run percentiles; counts per run'
for key in ('messages_per_second','instrumented_messages_per_second'):
    result[key]=statistics.median(r[key] for r in runs)
for stage in result['stages']:
    result['stages'][stage]={key:statistics.median(r['stages'][stage][key] for r in runs) for key in runs[0]['stages'][stage]}
result['stage_run_ranges']={name:{key:{'min':min(r['stages'][name][key] for r in runs),'max':max(r['stages'][name][key] for r in runs)} for key in ('p50_ns','p99_ns','p999_ns')} for name in result['stages']}
for name in result['stages']:
    for key in ('max_ns','out_of_range'):result['stages'][name][key]=max(r['stages'][name][key] for r in runs)
result['aggregation']='median of run percentiles; maximum across runs for max and overflow; counts per run'
result['hot_allocations']=max(r['hot_allocations'] for r in runs)
prefix.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n')
# Histogram from representative run only; do not combine counts with aggregated percentiles.
prefix.with_suffix('.csv').write_bytes(Path(f'{prefix}.run01.csv').read_bytes())
