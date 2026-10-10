import json, sys
baseline, current = (json.load(open(p)) for p in sys.argv[1:])
assert baseline['stage'] == current['stage']
if current['p99_ns'] > baseline['p99_ns'] * 1.10:
    raise SystemExit('p99 regression exceeds 10%')
