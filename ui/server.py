#!/usr/bin/env python3
"""Local presentation/controller bridge. Motion and fault logic remain on QNX."""
from http.server import ThreadingHTTPServer,BaseHTTPRequestHandler
from pathlib import Path
import importlib.util,json,os,signal,subprocess,sys,threading,time,urllib.parse
ROOT=Path(__file__).resolve().parents[1];REPORTS=ROOT/'reports';PORT=int(os.environ.get('SL_UI_PORT','8765'))
spec=importlib.util.spec_from_file_location('sl_report',REPORTS/'parse_report.py');reporter=importlib.util.module_from_spec(spec);spec.loader.exec_module(reporter)
WARNING=reporter.WARNING;FAULTS={'none','keepout','jerk','stale','replay','expired','malformed','overload','hang','crash','direct'}
lock=threading.RLock();state={'status':'idle','job':None,'report':'acceptance.json','error':None,'cancelled':False};process=None

def worker(config):
 global process
 name='operator-'+str(time.time_ns());cmd=REPORTS/(name+'.commands');events=REPORTS/(name+'.events');events.write_text('')
 slot,mask,mode,fault=config['slot'],config['occupied'],config['mode'],config['fault']
 cmd.write_text(f'set +m; /proc/boot/sl run baseline none 0 {slot} {mask}; echo CALIBRATION_DONE; /proc/boot/sl run planner {fault} {mode} {slot} {mask} & echo TRANSFER_STARTED\n')
 env=os.environ.copy();env.update(SL_INTERACTIVE='1',SL_EVENTS_FILE=str(events))
 with lock:state.update(job=name,status='running',config=config,events=str(events),cancelled=False,error=None)
 try:
  with (REPORTS/(name+'-driver.log')).open('w') as f:
   with lock:process=subprocess.Popen([sys.executable,str(ROOT/'scripts/run-target.py'),name,str(cmd)],cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
   rc=process.wait()
  with lock:
   if state['cancelled']:state['status']='cancelled';return
  report=reporter.parse(REPORTS/(name+'.log'))
  with lock:state.update(status='complete' if rc==0 and report['matrix_pass'] else 'failed',report=name+'.json',error=None if rc==0 and report['matrix_pass'] else 'Target evidence did not meet every expected result; inspect the exported matrix.')
 except Exception as e:
  with lock:state.update(status='failed',error=str(e))
 finally:
  with lock:process=None

def snapshot():
 with lock:s=dict(state)
 s.pop('events',None);s['disclaimer']=WARNING;s['phase']='idle';s['telemetry']=[]
 if s.get('job'):
  path=REPORTS/(s['job']+'.log')
  if path.exists():
   text=path.read_text(errors='replace');s['phase']='transfer' if '\nTRANSFER_STARTED' in text else 'baseline calibration'
   if s['phase']=='transfer':text=text[text.index('\nTRANSFER_STARTED'):]
   for l in text.splitlines():
    if l.startswith('T,'):
     a=l.split(',')
     if len(a)>=8:
      try:s['telemetry'].append(dict(tick=int(a[1]),x=reporter.clean(a[2]),y=reporter.clean(a[3]),requested_x=reporter.clean(a[4]),requested_y=reporter.clean(a[5]),reason=a[6],advice_age_ns=int(a[7]),latched_reason=a[8] if len(a)>11 else 'ACCEPT',latched_x=reporter.clean(a[9]) if len(a)>11 else None,latched_y=reporter.clean(a[10]) if len(a)>11 else None))
      except ValueError:pass
   s['telemetry']=s['telemetry'][-500:]
 return s

class Handler(BaseHTTPRequestHandler):
 def json(self,value,status=200):
  b=json.dumps(value,allow_nan=False).encode();self.send_response(status);self.send_header('Content-Type','application/json');self.send_header('Cache-Control','no-store');self.send_header('Content-Length',str(len(b)));self.end_headers();self.wfile.write(b)
 def do_GET(self):
  route=urllib.parse.urlsplit(self.path).path
  if route=='/api/state':return self.json(snapshot())
  if route=='/api/report':
   with lock:name=state.get('report','acceptance.json')
   p=REPORTS/name
   if not p.exists():return self.json({'disclaimer':WARNING,'runs':[],'comparisons':[],'policy':{'pass':False}},200)
   return self.json(json.loads(p.read_text()))
  allowed={'/':('index.html','text/html'),'/app.js':('app.js','text/javascript'),'/style.css':('style.css','text/css')}
  if route in allowed:
   f,mime=allowed[route];b=(ROOT/'ui'/f).read_bytes();self.send_response(200);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(b)));self.end_headers();self.wfile.write(b);return
  if route.startswith('/exports/'):
   name=urllib.parse.unquote(route[len('/exports/'):])
   if Path(name).name!=name or not name.endswith(('.csv','.json')):return self.send_error(403)
   p=REPORTS/name
   if not p.is_file():return self.send_error(404)
   b=p.read_bytes();self.send_response(200);self.send_header('Content-Type','application/json' if name.endswith('.json') else 'text/csv');self.send_header('Content-Disposition','attachment; filename="'+name+'"');self.send_header('Content-Length',str(len(b)));self.end_headers();self.wfile.write(b);return
  self.send_error(404)
 def do_POST(self):
  global process
  origin=self.headers.get('Origin')
  if origin and origin not in (f'http://127.0.0.1:{PORT}',f'http://localhost:{PORT}'):return self.json({'error':'Origin rejected'},403)
  if self.headers.get_content_type()!='application/json':return self.json({'error':'application/json required'},415)
  try:
   n=int(self.headers.get('Content-Length','0'))
   if n<0 or n>16384:raise ValueError('Invalid body size')
   data=json.loads(self.rfile.read(n) or b'{}');route=urllib.parse.urlsplit(self.path).path
   if route=='/api/start':
    slot=data.get('slot',0);mask=data.get('occupied',14);mode=data.get('mode',0);fault=data.get('fault','none')
    if any(type(x) is not int for x in (slot,mask,mode)):raise ValueError('Recipe and mode must be integers')
    if slot not in range(4) or mask not in range(16) or mask&(1<<slot) or mode not in (0,1) or fault not in FAULTS:raise ValueError('Invalid recipe/mode/fault; start slot cannot be occupied')
    if data.get('period_ms',10)!=10 or data.get('dock',[390,250])!=[390,250]:raise ValueError('Dock and 10ms period are frozen')
    if os.statvfs(ROOT).f_bavail*os.statvfs(ROOT).f_frsize<3*1024**3:raise ValueError('Less than 3GiB free; stopped')
    with lock:
     if state['status'] in ('starting','running','cancelling'):return self.json({'error':'A target session is already running'},409)
     state.update(status='starting',error=None)
     threading.Thread(target=worker,args=({'slot':slot,'occupied':mask,'mode':mode,'fault':fault},),daemon=True).start()
    return self.json({'ok':True,'disclaimer':WARNING})
   if route=='/api/inject':
    fault=data.get('fault')
    if fault not in FAULTS:raise ValueError('Unknown fault')
    snap=snapshot()
    with lock:
     if state['status']!='running' or snap['phase']!='transfer':return self.json({'error':'Wait for the transfer phase'},409)
     with Path(state['events']).open('a') as f:f.write(fault+'\n')
    return self.json({'ok':True,'disclaimer':WARNING})
   if route=='/api/reset':
    with lock:
     state['cancelled']=True
     if process and process.poll() is None:
      state['status']='cancelling';os.killpg(process.pid,signal.SIGTERM)
     else:state.update(status='idle',job=None,error=None)
    return self.json({'ok':True,'disclaimer':WARNING})
   return self.json({'error':'Unknown endpoint'},404)
  except (ValueError,TypeError,json.JSONDecodeError) as e:return self.json({'error':str(e),'disclaimer':WARNING},400)
 def log_message(self,*args):pass
if __name__=='__main__':
 print(f'Split Loop local UI: http://127.0.0.1:{PORT} — {WARNING}',flush=True)
 def stop(signum,frame):raise KeyboardInterrupt
 signal.signal(signal.SIGTERM,stop)
 try:ThreadingHTTPServer(('0.0.0.0',PORT),Handler).serve_forever()
 except KeyboardInterrupt:pass
 finally:
  if process and process.poll() is None:os.killpg(process.pid,signal.SIGTERM)
