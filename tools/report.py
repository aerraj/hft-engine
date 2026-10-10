"""Decode deterministic binary logs and report execution/PnL; standard library only."""
import argparse, bisect, csv, json, math, statistics, struct
from pathlib import Path

def read(path):
    orders=[];fills=[];samples=[]
    with open(path,'rb') as f:
        while header:=f.read(11):
            if len(header)!=11:raise ValueError('truncated log header')
            time,kind,n=struct.unpack('>QcH',header);b=f.read(n)
            if len(b)!=n:raise ValueError('truncated log record')
            if kind==b'O' and b[:1]==b'O':
                if n!=49:raise ValueError('invalid OUCH enter length')
                orders.append(dict(time=time,id=int(b[1:15],36),side=chr(b[15]),qty=int.from_bytes(b[16:20],'big'),price=int.from_bytes(b[28:32],'big')))
            elif kind==b'F':
                id,qty,price,maker,side,loc,pos,cash,realized=struct.unpack('>QI q B c H q q q'.replace(' ',''),b)
                fills.append(dict(time=time,id=id,qty=qty,price=price,maker=bool(maker),side=side.decode(),locate=loc,position=pos,cash=cash,realized=realized))
            elif kind==b'M':
                loc,bid,ask,pos,cash,realized,basis,last_trade=struct.unpack('>Hqqqqqqq',b);mark=(bid+ask)//2 if bid and ask else last_trade
                samples.append(dict(time=time,locate=loc,bid=bid,ask=ask,position=pos,cash=cash,realized=realized,basis=basis,mark=mark,equity=cash+pos*mark if mark else None))
    return orders,fills,samples

def report(path,horizon=1_000_000,fee=0):
    orders,fills,samples=read(path);marked=[s for s in samples if s['mark']];times=[s['time'] for s in marked]
    selection=[]
    for f in fills:
        i=bisect.bisect_left(times,f['time']+horizon)
        if i<len(marked):selection.append((marked[i]['mark']-f['price'])*(1 if f['side']=='B' else -1))
    # Time-normalized one-second PnL increments; do not annualize a short synthetic run.
    seconds={}
    for s in marked:seconds[s['time']//1_000_000_000]=s['equity']
    values=[]
    if seconds:
        last=next(iter(seconds.values()))
        for t in range(min(seconds),max(seconds)+1):last=seconds.get(t,last);values.append(last)
    changes=[b-a for a,b in zip(values,values[1:])];sd=statistics.stdev(changes) if len(changes)>1 else 0
    qty=sum(f['qty'] for f in fills);requested=sum(o['qty'] for o in orders);final=marked[-1] if marked else None
    result=dict(orders=len(orders),fills=len(fills),filled_qty=qty,requested_qty=requested,fill_rate=qty/requested if requested else 0,
                final_inventory=final['position'] if final else 0,max_abs_inventory=max((abs(s['position']) for s in samples),default=0),
                gross_pnl_ticks=final['equity'] if final else None,fees_ticks=qty*fee,net_pnl_ticks=final['equity']-qty*fee if final else None,
                realized_ticks=samples[-1]['realized'] if samples else 0,
                pnl_increment_sharpe=statistics.mean(changes)/sd if sd else None,sharpe_observations=len(changes),
                adverse_selection_cost_ticks=-statistics.mean(selection) if selection else None,adverse_selection_horizon_ns=horizon,
                dataset='synthetic unless caller supplies historical data',annualized_sharpe=None)
    return result,orders,fills,samples
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('log');p.add_argument('--out',required=True);p.add_argument('--fee-ticks',type=float,default=0);a=p.parse_args()
    result,orders,fills,samples=report(a.log,fee=a.fee_ticks);prefix=Path(a.out);prefix.parent.mkdir(parents=True,exist_ok=True);prefix.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n')
    for name,rows in [('orders',orders),('fills',fills),('inventory',samples)]:
        if rows:
            with open(str(prefix)+'.'+name+'.csv','w') as f:w=csv.DictWriter(f,fieldnames=rows[0].keys());w.writeheader();w.writerows(rows)
    print(json.dumps(result,indent=2))
