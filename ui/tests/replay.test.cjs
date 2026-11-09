/* DOM-level integration checks; not a browser screenshot/layout audit. */
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const path=require('node:path'),root=path.resolve(__dirname,'../..');
const html=fs.readFileSync(path.join(root,'ui/standalone.html'),'utf8');
const data=JSON.parse(html.match(/<script type="application\/json" id="embedded-evidence">([\s\S]*?)<\/script>/)[1]);
const js=fs.readFileSync(path.join(root,'ui/app.js'),'utf8');
function boot(embedded,protocol='file:'){
 const nodes=new Map();
 class E{constructor(){this.children=[];this.attrs={};this.value='0';this.textContent='';this.style={};this.disabled=false;}setAttribute(k,v){this.attrs[k]=v;}append(x){this.children.push(x);}replaceChildren(...x){this.children=x;}}
 for(const m of html.matchAll(/\bid="([^"]+)"/g))nodes.set(m[1],new E());
 if(embedded)nodes.get('embedded-evidence').textContent=JSON.stringify(data);else nodes.delete('embedded-evidence');
 const occ=[0,1,2,3].map(i=>Object.assign(new E(),{value:String(i),checked:i>0}));
 let fetches=[],intervals=[];
 const ctx=vm.createContext({document:{getElementById:id=>nodes.get(id),createElement:()=>new E(),createElementNS:()=>new E(),querySelectorAll:q=>q==='.occ:checked'?occ.filter(x=>x.checked):occ},
 location:{protocol},performance:{now:()=>0},requestAnimationFrame:()=>{},setInterval:f=>intervals.push(f),Blob,URL,console,
 fetch:async(url,options)=>{fetches.push([url,options]);if(protocol==='file:')throw Error('File view must not fetch');return {ok:true,json:async()=>url==='/api/state'?{status:'idle',phase:'idle',report:'acceptance',telemetry:[]}:url==='/api/report'?data:{ok:true}};}});
 vm.runInContext(js,ctx);
 return {ctx,nodes,occ,fetches,intervals,run:code=>vm.runInContext(code,ctx)};
}
(async()=>{
 let a=boot(true);
 assert.equal(a.fetches.length,0);assert.equal(a.intervals.length,0);
 assert.equal(a.nodes.get('cycle').textContent,'2.960 s');assert.equal(a.nodes.get('baseline').textContent,'7.990 s');
 assert.match(a.nodes.get('policy').textContent,/fd=-1 · errno=1/);assert.equal(a.nodes.get('start').disabled,true);
 assert.equal(a.nodes.get('run').children.length,12);
 assert(a.nodes.get('run').children.every(o=>o.textContent.length<=40&&/→ (DOCK|HOME)$/.test(o.textContent)));
 assert.match(a.nodes.get('policy').textContent,/CONTROL \/dev\/joint — OPEN ALLOWED/);
 assert.match(a.nodes.get('policy').textContent,/device mode=0666/);
 assert.match(a.nodes.get('policy').textContent,/ADVISOR \/dev\/joint — DENIED \(EPERM\)/);
 assert(!a.nodes.get('policy').textContent.includes('Not a medical device'));
 const labels=a.nodes.get('lab').children.filter(x=>x.textContent==='FULL');assert.equal(labels.length,3);
 assert(labels.every(x=>x.attrs['aria-label'].startsWith('Occupied slot')));
 const housing=a.nodes.get('lab').children.find(x=>x.textContent==='HOUSING KEEP-OUT');assert.equal(housing.attrs.y,38);
 assert.equal(a.nodes.get('csv').href,'../reports/acceptance-r01-planner-none-0.csv');
 assert.match(a.nodes.get('json').href,/^blob:/);
 let lines=a.nodes.get('lab').children.filter(x=>x.attrs.points!==undefined);assert.equal(lines.length,2);assert(lines.every(x=>x.attrs.points.length>100));
 for(const [fault,expected] of [['keepout','REJECT_KEEP_OUT'],['jerk','CLAMP_JERK'],['stale','REJECT_STALE'],['replay','REJECT_REPLAY'],['expired','REJECT_EXPIRED'],['malformed','REJECT_MALFORMED']]){
  a.run(`selected=report.runs.findIndex(r=>r.fault===${JSON.stringify(fault)});choose();`);
  assert.match(a.nodes.get('reason').textContent,new RegExp(expected));
 }
 a.run('selected=2;choose();');
 const endpoint=a.nodes.get('lab').children.find(x=>x.attrs.fill==='#f9b466');
 const keepoutLabel=a.nodes.get('lab').children.find(x=>x.textContent==='HOUSING KEEP-OUT');
 assert(endpoint.attrs.cy-endpoint.attrs.r-keepoutLabel.attrs.y>=8);
 a.run('selected=11;choose();');assert.match(a.nodes.get('health').textContent,/19 missed periods/);
 a.nodes.get('play').onclick();a.run('animate(20000)');assert.match(a.nodes.get('source').textContent,/REPLAY/);
 // Idle cassette follows the current start slot even without an evidence report.
 let idle=boot(false);idle.nodes.get('slot').value='2';idle.nodes.get('slot').onchange();
 const marker=idle.nodes.get('lab').children.find(x=>x.attrs['aria-label']?.startsWith('Idle cassette'));
 assert.equal(marker.attrs.x,213);assert.equal(marker.attrs.y,321);assert.equal(idle.fetches.length,0);
 let live=boot(false,'http:');await new Promise(r=>setImmediate(r));
 assert(live.fetches.some(x=>x[0]==='/api/state'));assert(live.fetches.some(x=>x[0]==='/api/report'));
 assert.equal(live.nodes.get('cycle').textContent,'2.960 s');assert.equal(live.nodes.get('start').disabled,false);
 assert.equal(live.nodes.get('csv').href,'/exports/acceptance-r01-planner-none-0.csv');
 live.nodes.get('start').onclick();await new Promise(r=>setImmediate(r));
 assert(live.fetches.some(x=>x[0]==='/api/start'&&x[1].method==='POST'));
 console.log('PASS short occupied-slot labels, compact complete replay labels, keep-out endpoint separation and scannable policy display');
 console.log('PASS offline no-network load, cassette/path/cards/policy, six contract reasons, hang A/B, replay, CSV/JSON links, idle recipe cassette, live API routing');
})().catch(e=>{console.error(e);process.exitCode=1;});
