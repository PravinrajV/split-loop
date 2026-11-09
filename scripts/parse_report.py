#!/usr/bin/env python3
"""Parse actual QNX serial evidence; never fabricate missing samples or pass cases."""
import csv,hashlib,json,math,re,sys
from datetime import datetime
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]  # stage root (scripts/ -> ..)
WARNING='Not a medical device; not certified; not a patient-treatment product.'
REASONS=['ACCEPT','CLAMP_JERK','REJECT_KEEP_OUT','REJECT_STALE','REJECT_REPLAY','REJECT_EXPIRED','REJECT_MALFORMED','FALLBACK_NO_ADVICE']
HEAD=['tick','mono_ns','lateness_ns','command_ns','advice_age_ns','ask_ns','x','y','vx','vy','ax','ay','jx','jy','sensor_x','sensor_y','requested_x','requested_y','reason','collision','jerk_breach']
def clean(x):
 try:
  f=float(x)
  return (int(x) if re.fullmatch(r'-?\d+',x) else f) if math.isfinite(f) else None
 except (ValueError,TypeError):return x

def pct(values):
 v=sorted(x for x in values if isinstance(x,(int,float)) and math.isfinite(x))
 return {'sample_count':len(v),**{k:(v[min(len(v)-1,max(0,math.ceil(p*len(v))-1))] if v else None) for k,p in [('p50',.5),('p95',.95),('p99',.99),('max',1)]}}

def parse(path,write=True):
 path=Path(path);text=path.read_text(errors='replace');runs=[];current=None
 for line in text.splitlines():
  if line.startswith('RUN_BEGIN,'):
   a=line.split(',');current={'kind':a[1],'fault':a[2],'mode':int(a[3]),'slot':int(a[4]),'occupied':int(a[5]),'samples':[]}
  elif current is not None and line.startswith('S,') and not line.startswith('S,tick,'):
   a=line.split(',')[1:]
   if len(a)==len(HEAD):current['samples'].append({k:clean(v) for k,v in zip(HEAD,a)})
  elif current is not None and line.startswith('RESULT,'):
   current['result']={k:clean(v) for k,v in (p.split('=',1) for p in line.split(',')[1:] if '=' in p)}
  elif current is not None and line=='RUN_END':runs.append(current);current=None
 for i,r in enumerate(runs):
  rows=r['samples'];res=r.get('result',{});r['id']=f'{path.stem}-r{i:02}-{r["kind"]}-{r["fault"]}-{r["mode"]}'
  r['cycle_s']=res.get('cycle_s') if res.get('finish')==1 and res.get('cycle_s',-1)>0 else None
  r['finish']={1:'DOCK',2:'HOME',3:'FAIL'}.get(res.get('finish'),'INCOMPLETE')
  r['timing']={'wake_lateness_ns':pct([s['lateness_ns'] for s in rows]),'command_completion_ns':pct([s['command_ns'] for s in rows]),'synchronous_ask_ns':pct([s['ask_ns'] for s in rows]),'advice_age_ns':pct([s['advice_age_ns'] for s in rows if isinstance(s['advice_age_ns'],(int,float)) and s['advice_age_ns']>=0]),'miss_count':res.get('misses'), 'run_length_s':((rows[-1]['mono_ns']-rows[0]['mono_ns'])/1e9+.01) if rows else None}
  r['reasons_observed']=sorted({s['reason'] for s in rows})
  r['max_abs_velocity']=max((abs(s[k]) for s in rows for k in ['vx','vy']),default=None)
  r['max_abs_acceleration']=max((abs(s[k]) for s in rows for k in ['ax','ay']),default=None)
  r['max_abs_jerk']=max((abs(s[k]) for s in rows for k in ['jx','jy']),default=None)
  valid=bool(rows) and res.get('samples')==len(rows) and res.get('fifo')==1 and res.get('collision')==0 and res.get('jerk_breach')==0 and r['max_abs_velocity']<=150.000001 and r['max_abs_acceleration']<=400.000001 and r['max_abs_jerk']<=1500.000001
  reason={'keepout':'REJECT_KEEP_OUT','jerk':'CLAMP_JERK','stale':'REJECT_STALE','replay':'REJECT_REPLAY','expired':'REJECT_EXPIRED','malformed':'REJECT_MALFORMED','overload':'FALLBACK_NO_ADVICE','hang':'FALLBACK_NO_ADVICE'}.get(r['fault'])
  expected='DOCK with finite cycle and zero breaches' if r['fault']=='none' else ('HOME/DOCK, zero breaches'+(f', {reason}' if reason else ''))
  passed=valid and r['finish'] in ('HOME','DOCK')
  if r['fault']=='none':passed=passed and r['cycle_s'] is not None
  if reason:passed=passed and reason in r['reasons_observed']
  if r['fault'] in ('crash','hang'):passed=passed and res.get('restarts',0)>=1
  if r['mode']==1 and r['fault']=='hang':
   expected+='; synchronous ask >=10ms and at least one missed period'
   passed=passed and (r['timing']['synchronous_ask_ns']['max'] or 0)>=10000000 and res.get('misses',0)>0
  r['matrix']={'expected':expected,'observed':{'finish':r['finish'],'cycle_s':r['cycle_s'],'reasons':r['reasons_observed'],'miss_count':res.get('misses'),'restarts':res.get('restarts')},'pass':bool(passed)}
  r['raw_csv']=r['id']+'.csv'
  if write:
   with (path.parent/r['raw_csv']).open('w') as f:
    f.write('# '+WARNING+' QEMU measurements, not WCET or a hardware real-time guarantee.\n')
    w=csv.DictWriter(f,fieldnames=HEAD);w.writeheader();w.writerows(rows)
 baselines={(r['slot'],r['occupied']):r for r in runs if r['kind']=='baseline' and r['fault']=='none' and r['cycle_s'] is not None}
 comparisons=[]
 for r in runs:
  base=baselines.get((r['slot'],r['occupied']))
  if r['kind']=='planner' and base:
   win=r['matrix']['pass'] and r['cycle_s'] is not None and r['cycle_s']<base['cycle_s']
   comparisons.append({'run_id':r['id'],'baseline_id':base['id'],'baseline_s':base['cycle_s'],'planner_s':r['cycle_s'],'improvement_percent':(100*(base['cycle_s']-r['cycle_s'])/base['cycle_s']) if r['cycle_s'] is not None else None,'legal_cycle_time_win':bool(win)})
 policy_lines=[l for l in text.splitlines() if l.startswith('POLICY_')]
 policy_ok=any('POLICY_RESULT,control_ok=1,advisor_denied=1' in l for l in policy_lines) and any(l.startswith('POLICY_OPEN,control') and 'mode=0666,fstat_ok=1' in l for l in policy_lines) and any(l.startswith('POLICY_OPEN,advisor') and 'fd=-1,errno=1' in l for l in policy_lines)
 setup_path=path.with_name(path.stem+'-setup.json')
 manifest=json.loads(setup_path.read_text()) if setup_path.exists() else {'capture':'Historical log: no contemporaneous build manifest','hashes':{}}
 hashes=manifest['hashes']
 report={'disclaimer':WARNING,'generated_at_est':datetime.now().astimezone().isoformat(),'source_log':path.name,'setup':{'qemu':'10.0.13 (Debian)','acceleration':'TCG -cpu max','vcpus':1,'ram_mib':512,'period_ms':10,'qnx':'8.0.0; exact boot build in source_log','host':'Debian 13 (not a supported QNX host)','plant':'fixed 10ms second-order integration, mass=1, viscous coefficient=1.2; real CLOCK_MONOTONIC cycle clock','sensor_noise':'deterministic sinusoid: position amplitude .005mm, velocity .002mm/s; separately recorded from truth','timing_scope':'QEMU observations only, not WCET and not a hardware real-time guarantee','percentiles':'nearest rank','command_completion':'actual wake to final joint read, includes synchronous ask when naive','miss_definition':'wake lateness >=10ms OR command completed after next nominal deadline','telemetry':'4096 fixed records, no blocking IO on controller output; separate lossy 10Hz presentation process','baseline':'Safe-via sequential jerk-limited trapezoid; v80 a200 j600, Y180 then X390 then Y250','image':'images/split-loop/output/ifs.bin + disk-qemu','artifact_capture':manifest['capture'],'hashes':hashes},'runs':runs,'comparisons':comparisons,'policy':{'pass':policy_ok,'evidence':policy_lines},'matrix_pass':bool(runs) and all(r['matrix']['pass'] for r in runs) and policy_ok,'ai_label':'Not used; native profiler, no trained model'}
 report['judge_summary']=next((c for c in comparisons if next(r for r in runs if r['id']==c['run_id'])['fault']=='none'),None)
 if write:
  out=path.with_suffix('.json');out.write_text(json.dumps(report,indent=2,allow_nan=False))
  matrix={'disclaimer':WARNING,'source':path.name,'pass':report['matrix_pass'],'cases':[{'id':r['id'],**r['matrix']} for r in runs]+[{'id':'OS-device-denial','expected':'control open succeeds with mode0666; advisor denied by enforcing OS policy','observed':policy_lines,'pass':policy_ok}]}
  path.with_name(path.stem+'-matrix.json').write_text(json.dumps(matrix,indent=2))
 return report
if __name__=='__main__':
 report=parse(sys.argv[1]);print(json.dumps({'disclaimer':WARNING,'runs':len(report['runs']),'pass':report['matrix_pass'],'comparisons':report['comparisons'],'policy':report['policy']},indent=2));sys.exit(0 if report['matrix_pass'] else 1)
