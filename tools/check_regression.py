"""Fail closed for incompatible baseline or >10% p99 regression in any stage."""
import argparse,json
p=argparse.ArgumentParser();p.add_argument('baseline');p.add_argument('current');a=p.parse_args();b,c=(json.load(open(p)) for p in (a.baseline,a.current))
for key in ('schema','dataset','platform','arch','compiler','pinned','timer','dense_index','build_type','native','cpu_model'):
    if key not in b or key not in c:raise SystemExit(f'missing benchmark metadata: {key}')
    if b[key]!=c[key]:raise SystemExit(f'incompatible benchmark metadata: {key}')
if c['hot_allocations'] or not c['encoded_orders']:raise SystemExit('invalid hot path audit or no outgoing orders')
expected={'parse','book','signal','risk','encode','tick_to_trade','total_event'}
if set(b['stages'])!=expected or set(c['stages'])!=expected:raise SystemExit('missing stage distribution')
for name,stage in b['stages'].items():
    current=c['stages'].get(name,{})
    if current.get('out_of_range',0):raise SystemExit(f'histogram overflow: {name}')
    if not stage.get('samples') or not current.get('samples'):raise SystemExit(f'missing samples: {name}')
    if current['p99_ns']>stage['p99_ns']*1.1:raise SystemExit(f'{name} p99 regression exceeds 10%')
