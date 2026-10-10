"""Fail closed for incompatible baseline or >10% p99 regression in any stage."""
import argparse,json
p=argparse.ArgumentParser();p.add_argument('baseline');p.add_argument('current');a=p.parse_args();b,c=(json.load(open(p)) for p in (a.baseline,a.current))
for key in ('schema','dataset','platform','arch','compiler','pinned','timer','dense_index','build_type','native'):
    if b.get(key)!=c.get(key):raise SystemExit(f'incompatible benchmark metadata: {key}')
if c['hot_allocations'] or not c['encoded_orders']:raise SystemExit('invalid hot path audit or no outgoing orders')
for name,stage in b['stages'].items():
    current=c['stages'].get(name,{})
    if not stage.get('samples') or not current.get('samples'):raise SystemExit(f'missing samples: {name}')
    if current['p99_ns']>stage['p99_ns']*1.1:raise SystemExit(f'{name} p99 regression exceeds 10%')
