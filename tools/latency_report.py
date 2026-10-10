"""Produce Markdown latency table and SVG histogram from benchmark artifacts."""
import argparse,csv,json,html
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('prefix');a=p.parse_args();prefix=Path(a.prefix);data=json.loads(prefix.with_suffix('.json').read_text())
rows=['| Stage | p50 ns | p99 ns | p99.9 ns | Samples |','|---|---:|---:|---:|---:|']
for name,s in data['stages'].items():rows.append(f"| {name} | {s['p50_ns']} | {s['p99_ns']} | {s['p999_ns']} | {s['samples']} |")
prefix.with_suffix('.md').write_text('\n'.join(rows)+'\n')
counts=[0]*7;limits=[100,250,500,1000,2000,5000]
with prefix.with_suffix('.csv').open() as f:
    for row in csv.DictReader(f):
        if row['stage']=='tick_to_trade':
            value=int(row['lower_ns']);index=sum(value>=limit for limit in limits);counts[index]+=int(row['count'])
labels=['<100','100–249','250–499','500–999','1000–1999','2000–4999','5000+']
svg=['<svg xmlns="http://www.w3.org/2000/svg" width="800" height="375"><rect width="800" height="375" fill="#101827"/><g fill="#e5e7eb" font-family="monospace" font-size="14">',f'<text x="20" y="28">Tick-to-trade · {html.escape(data["platform"])} {html.escape(data["arch"])} · development measurement</text>']
maximum=max(counts) or 1
for i,(label,count) in enumerate(zip(labels,counts)):
    y=52+42*i;svg.append(f'<text x="20" y="{y+17}">{label} ns</text><rect x="160" y="{y}" width="{count/maximum*440}" height="25" fill="#38bdf8"/><text x="620" y="{y+17}">{count}</text>')
svg.append('</g></svg>');prefix.with_suffix('.svg').write_text(''.join(svg))
