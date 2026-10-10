import copy,json,subprocess,sys,tempfile
from pathlib import Path
root=Path(sys.argv[1]);baseline=json.loads((root/'docs/bench/linux/current.json').read_text());baseline['cpu_model']='fixture CPU'
with tempfile.TemporaryDirectory() as d:
    d=Path(d);a=d/'baseline.json';a.write_text(json.dumps(baseline))
    def check(current,passes):
        b=d/'current.json';b.write_text(json.dumps(current));r=subprocess.run([sys.executable,str(root/'tools/check_regression.py'),str(a),str(b)],capture_output=True)
        assert (r.returncode==0)==passes,(r.stdout,r.stderr)
    check(baseline,True)
    candidate=copy.deepcopy(baseline);candidate['stages']['book']['p99_ns']*=1.09;check(candidate,True)
    candidate['stages']['book']['p99_ns']=baseline['stages']['book']['p99_ns']*1.11;check(candidate,False)
    candidate=copy.deepcopy(baseline);candidate['cpu_model']='different CPU';check(candidate,False)
    candidate=copy.deepcopy(baseline);candidate['stages']={};check(candidate,False)
    candidate=copy.deepcopy(baseline);candidate['hot_allocations']=1;check(candidate,False)
    candidate=copy.deepcopy(baseline);del candidate['compiler'];check(candidate,False)
print('Regression gate accepts 9%, rejects 11%, incompatible CPU, missing metadata/stages and allocations')
