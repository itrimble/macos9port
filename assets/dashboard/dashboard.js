import {renderBuildProgress} from './build-progress.js';
const $ = (selector) => document.querySelector(selector);
const colors = {verified:'#69e3b4',implemented:'#6cb7ff',in_progress:'#e5c476',blocked:'#ff929c',pending:'#687d94'};
const labels = {verified:'Accepted',implemented:'Implemented',in_progress:'In progress',blocked:'Blocked',pending:'Pending'};
const escape = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
let data, currentView = 'overview', source, lastMessage = 0, lastGanttRevision = '';
const date = value => value ? new Date(/^\d{4}-\d{2}-\d{2}$/.test(value)?value+'T12:00:00':value).toLocaleDateString(undefined,{month:'short',day:'numeric',year:'numeric'}) : 'Not scheduled';
const stamp = value => value ? new Date(value * 1000).toLocaleString() : '—';
const sourceLink = path => `/api/source?path=${encodeURIComponent(path)}`;
const pct = (n,total) => total ? Math.round(n / total * 100) : 0;
const icon = (status) => {
  const shape = status === 'verified' ? '<path d="m4 8 3 3 5-6"/>' : status === 'blocked' ? '<path d="M8 4v5M8 12h.01"/>' : status === 'implemented' ? '<path d="M4 8h8M8 4v8"/>' : '<path d="M5 4h6M5 12h6M5 4c0 4 6 4 6 8M11 4c0 4-6 4-6 8"/>';
  return `<svg class="status-icon" viewBox="0 0 16 16" fill="none" stroke="${colors[status] || colors.pending}" stroke-width="1.4" stroke-linecap="round" aria-hidden="true">${shape}</svg>`;
};
function donut(counts, total, large=false) {
  let offset=0;
  const radius=large?76:15, size=large?200:40, center=size/2, length=2*Math.PI*radius;
  const slices=Object.entries(colors).map(([state,color])=>{
    const fraction=(counts[state] || 0)/(total || 1), segment=fraction*length;
    const result=`<circle cx="${center}" cy="${center}" r="${radius}" fill="none" stroke="${color}" stroke-width="${large?12:4}" stroke-dasharray="${segment} ${length-segment}" stroke-dashoffset="${-offset}" transform="rotate(-90 ${center} ${center})"/>`;
    offset+=segment;return result;
  }).join('');
  return `<svg viewBox="0 0 ${size} ${size}" role="img" aria-label="${counts.verified || 0} of ${total} gates accepted"><circle cx="${center}" cy="${center}" r="${radius}" fill="none" stroke="#283b4e" stroke-width="${large?12:4}"/>${slices}${large?`<text x="100" y="101" text-anchor="middle" class="donut-value">${pct(counts.verified,total)}%</text><text x="100" y="121" text-anchor="middle" class="donut-label">${counts.verified} OF ${total} GATES</text>`:''}</svg>`;
}
function taskDetails(task) {
  return `<details class="task" data-task="${escape(task.id)}"><summary>${icon(task.status)}<span><span class="task-id mono">${escape(task.id)} · ${escape(labels[task.status])}</span>${escape(task.title)}</span></summary><div class="task-detail"><p>${escape(task.detail)}</p>${task.evidence?`<p><strong>Evidence</strong><br>${escape(task.evidence)}</p>`:''}<a href="${sourceLink(task.source)}" target="_blank" rel="noopener">Open source${task.line?` · line ${task.line}`:''} ↗</a><span class="task-progress">Acceptance: ${task.status==='verified'?'100%':'0% · not accepted yet'}</span></div></details>`;
}
function renderChecklist() {
  if (!data?.tasks) return;
  const query=$('#search').value.toLowerCase(), component=$('#component').value, status=$('#status').value;
  const open=new Set([...document.querySelectorAll('.task[open]')].map(el=>el.dataset.task));
  const tasks=data.tasks.filter(t=>(!component || t.component===component) && (!status || t.status===status) && `${t.id} ${t.title} ${t.detail}`.toLowerCase().includes(query));
  $('#task-count').textContent=`${tasks.length} / ${data.total}`;
  $('#checklist').innerHTML=tasks.length?tasks.map(taskDetails).join(''):'<p class="empty">No matching features. Try a different term or clear the filters.</p>';
  document.querySelectorAll('.task').forEach(el=>{el.open=open.has(el.dataset.task);});
}
function filterComponent(name) {
  $('#component').value=name;$('#status').value='';renderChecklist();
  $('#checklist').scrollTop=0;
  if (window.innerWidth<821) $('.checklist-head').scrollIntoView({block:'start',behavior:'auto'});
}
function timelineMarkup() {
  return `<div class="timeline-track"><svg class="timeline-wave" viewBox="0 0 1000 32" preserveAspectRatio="none" aria-hidden="true"><path class="wave-line" d="M0 18H1000"/><path class="wave-flow" d="M0 18H1000"/></svg>${data.milestones.map(m=>`<article class="milestone ${m.actual?'done':''}"><h3>${escape(m.title)}</h3><div class="meter" aria-label="${m.percent}% accepted"><span style="width:${m.percent}%"></span></div><p>${m.accepted}/${m.total} gates accepted</p><p>Estimate <span class="date">${date(m.estimated)}</span></p><p>Actual <span class="date">${m.actual?date(m.actual):'Not complete'}</span></p></article>`).join('')}</div>`;
}
function renderOverview() {
  $('#total-donut').innerHTML=donut(data.counts,data.total,true);
  $('#completion-note').textContent=`${data.counts.implemented} gates have implementation evidence but still need acceptance.`;
  $('#components').innerHTML=data.components.map(c=>`<button class="component" data-component="${escape(c.name)}" aria-label="Inspect ${escape(c.name)}: ${c.counts.verified} of ${c.total} accepted">${donut(c.counts,c.total)}<span><strong>${escape(c.name)}</strong><small>${c.counts.verified}/${c.total} accepted · ${c.counts.implemented} implemented</small></span></button>`).join('');
  $('#components').querySelectorAll('button').forEach(b=>b.onclick=()=>filterComponent(b.dataset.component));
  $('#legend').innerHTML=Object.entries(labels).map(([key,label])=>`<button data-status="${key}"><span class="dot" style="background:${colors[key]}"></span>${label}<b>${data.counts[key]}</b></button>`).join('');
  $('#legend').querySelectorAll('button').forEach(b=>b.onclick=()=>{$('#status').value=b.dataset.status;renderChecklist();});
  const build=data.builds[0], latest=data.latest;
  $('#build-strip').innerHTML=`<span class="build-mark"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="m12 3 8 4v10l-8 4-8-4V7zm-8 4 8 5 8-5M12 12v9"/></svg></span><div><strong>${build?`Build ${build.number || 'queued'} · ${escape(build.title)}`:latest?`Latest passing build · ${escape(latest.commit?.slice(0,12))}`:'Complete-build integration is underway'}</strong><small>${build?`${escape(build.status)} · ${escape(build.commit.slice(0,12))}`:'No new complete-build receipt yet'} · main ${escape(data.git.main.slice(0,8))}</small></div><a href="#builds">Inspect builds →</a>`;
  $('#timeline').innerHTML=timelineMarkup();$('#roadmap-timeline').innerHTML=timelineMarkup();
  $('#alert-count').textContent=`${data.alerts.length} notices`;
  $('#alerts').innerHTML=data.alerts.map(a=>`<article class="alert">${icon(a.level==='error'?'blocked':a.level==='warning'?'in_progress':'implemented')}<div><strong>${escape(a.title)}</strong><p>${escape(a.detail)}</p></div></article>`).join('') || '<p class="empty">No current alerts.</p>';
}
function table(headers,rows) {return `<div class="table-wrap"><table><thead><tr>${headers.map(h=>`<th scope="col">${escape(h)}</th>`).join('')}</tr></thead><tbody>${rows.map(row=>`<tr>${row.map(c=>`<td>${c}</td>`).join('')}</tr>`).join('')}</tbody></table></div>`;}
async function renderRoadmap() {
  $('#milestone-table').innerHTML=table(['Milestone','Depends on','Estimated','Actual','Acceptance'],data.milestones.map(m=>[escape(m.title),escape(m.depends.join(', ') || '—'),date(m.estimated),m.actual?date(m.actual):'Open',`${m.accepted}/${m.total} gates`]));
  if (lastGanttRevision===data.revision) return;
  const revision=data.revision;
  try {
    const {default:mermaid}=await import('/vendor/mermaid.js');
    mermaid.initialize({startOnLoad:false,securityLevel:'strict',theme:'dark',themeVariables:{fontFamily:'IBM Plex Sans, sans-serif',primaryColor:'#24455b',primaryTextColor:'#eaf2f8',lineColor:'#67d5f4',gridColor:'#294055'},gantt:{useMaxWidth:false,barHeight:25,barGap:12,fontSize:12}});
    const safe=s=>String(s).replace(/[^a-zA-Z0-9 +]/g,' ').slice(0,70);
    const lines=['gantt','dateFormat YYYY-MM-DD','axisFormat %b %d','section Required integration'];
    for(const m of data.milestones){
      const start=m.start || (m.depends.length?`after ${m.depends.join(' ')}`:new Date().toISOString().slice(0,10));
      lines.push(`${safe(m.title)} ${m.start?'':'(undated)'} :${m.actual?'done':m.start?'active, crit':'crit'}, ${safe(m.id)}, ${start}, ${m.actual && m.start && m.actual!==m.start?m.actual:'1d'}`);
    }
    const {svg}=await mermaid.render('os999-gantt-'+revision.replace(/[^a-z0-9]/gi,''),lines.join('\n'));
    if(data.revision===revision){$('#gantt').innerHTML=svg;lastGanttRevision=revision;}
  } catch(error){$('#gantt').textContent='The local Mermaid renderer is unavailable. The dependency table below remains live. '+error.message;}
}
function renderCompatibility() {
  let svg='<svg viewBox="0 0 720 350" role="img" aria-label="Application complexity versus measured compatibility; unmeasured apps are in a separate bottom lane">';
  for(let n=0;n<=100;n+=25){const y=215-n*1.7;svg+=`<path class="chart-grid" d="M65 ${y}H665"/><text class="chart-label" x="48" y="${y+4}" text-anchor="end">${n}%</text>`;}
  for(let n=1;n<=10;n++){const x=65+n*58;svg+=`<text class="chart-label" x="${x}" y="245" text-anchor="middle">${n}</text>`;}
  svg+='<text class="chart-label" x="65" y="22">Measured route compatibility</text><text class="chart-label" x="365" y="270" text-anchor="middle">Relative complexity →</text><rect x="65" y="287" width="600" height="48" rx="5" fill="#182b3c"/><text class="chart-label" x="8" y="313" font-size="10">Unknown</text>';
  const positions={};
  data.apps.forEach((app,i)=>{const key=`${app.complexity}-${app.compatibility}`,offset=(positions[key] || 0);positions[key]=offset+1;const x=65+app.complexity*58+offset*18,y=app.compatibility===null?306+(i%2)*14:215-app.compatibility*1.7;const color=app.status==='verified'?colors.verified:app.status==='incompatible'?colors.blocked:colors.in_progress;svg+=`<g><title>${escape(app.name)}: ${escape(app.detail)}</title><circle cx="${x}" cy="${y}" r="6" fill="${color}" stroke="#0a1019" stroke-width="2"/><text class="chart-name" x="${Math.min(x+10,580)}" y="${y-10}">${escape(app.name)}</text></g>`;});
  $('#scatter').innerHTML=svg+'</svg>';
  $('#app-table').innerHTML=table(['Application','State','Complexity','Measured compatibility','Evidence'],data.apps.map(a=>[escape(a.name),`<span class="state-label">${icon(a.status==='incompatible'?'blocked':a.status==='verified'?'verified':'pending')}${escape(a.status)}</span>`,`${a.complexity}/10 · estimate`,a.compatibility===null?'Unmeasured':`${a.compatibility}%`,`${escape(a.detail)}<small><a href="${sourceLink(a.source)}" target="_blank" rel="noopener">Open evidence ↗</a></small>`]));
}
function renderMetrics() {
  $('#metrics').innerHTML=data.metrics.map(m=>{const max=Math.max(m.os999?.value || 0,m.baseline?.value || 0,1);return `<article class="metric"><div><h3>${escape(m.name)}</h3><small>${escape(m.unit)} · ${m.direction==='lower'?'Lower':'Higher'} is better</small></div><div>${['os999','baseline'].map(side=>{const v=m[side];return `<div class="benchmark-bar"><span>${side==='os999'?'OS999':'Mac OS 9.2'}</span>${v?`<div class="bar-track ${side}"><span style="width:${Math.max(0,Math.min(100,v.value/max*100))}%"></span></div><span>${escape(v.value)} ${escape(m.unit)}</span>`:'<div class="bar-missing"></div><em>Not measured</em>'}</div>${v?`<p class="muted">${escape(v.hardware)} · ${escape(v.date)} · ${escape(v.source)}</p>`:''}`;}).join('')}</div></article>`;}).join('');
}
function anatomyTasks(group){return data.tasks.filter(t=>group.task_ids?group.task_ids.includes(t.id):t.component===group.component);}
function showAnatomy(index){const group=data.anatomy[index],tasks=anatomyTasks(group);$('#anatomy-detail').innerHTML=`<h2>${escape(group.name)}</h2><p class="muted">${tasks.length} tracked behaviors. Select a row to read its acceptance evidence.</p>${tasks.map(taskDetails).join('')}`;}
let anatomyIndex=0;
function renderAnatomy(){
  const groups=data.anatomy.map((group,index)=>({group,index,tasks:anatomyTasks(group)}));
  const rows=[groups.slice(0,2),groups.slice(2)];
  $('#treemap').innerHTML=rows.map(row=>`<div class="tree-row" style="flex:${row.reduce((n,g)=>n+g.tasks.length,0)}">${row.map(({group:g,index:i,tasks})=>{const accepted=tasks.filter(t=>t.status==='verified').length;return `<button class="tree-group" data-group="${i}" style="flex:${tasks.length}" aria-label="Inspect ${escape(g.name)}: ${accepted} of ${tasks.length} accepted"><h3>${escape(g.name)}</h3><span class="tree-cells">${tasks.map(t=>`<i style="background:${colors[t.status]}" title="${escape(t.title)} · ${labels[t.status]}"></i>`).join('')}</span><small>${tasks.length} tasks · ${pct(accepted,tasks.length)}% accepted</small></button>`;}).join('')}</div>`).join('');
  $('#treemap').querySelectorAll('button').forEach(b=>b.onclick=()=>{anatomyIndex=Number(b.dataset.group);showAnatomy(anatomyIndex);});
  if(data.anatomy.length)showAnatomy(Math.min(anatomyIndex,data.anatomy.length-1));
}
function renderBuilds(){
  renderWorkerHealth();
  renderBuildProgress($('#build-list'),data.builds);
  $('#worktrees').innerHTML=table(['Worktree','Branch','Revision'],data.git.worktrees.map(w=>[escape(w.name),escape(w.branch),`<code>${escape(w.head)}</code>`]));
  $('#sources').innerHTML=data.sources.map(p=>`<a href="${sourceLink(p)}" target="_blank" rel="noopener">${escape(p)} ↗</a>`).join('');
}
function renderWorkerHealth(){
  if(!data)return;
  const worker=data.watcher, fresh=worker && Date.now()/1000-worker.last_poll<45;
  $('#watcher-state').textContent=data.watcher_paused?'Paused':fresh?`Worker online · ${worker.status}`:worker?'Worker status is stale':'No worker receipt';
}
function render(next){
  lastMessage=Date.now();$('#connection').textContent=next.data_error?'Stale snapshot':'Live · local source';$('#signal').className=next.data_error?'signal error':'signal live';
  $('#data-error').hidden=!next.data_error;
  if(next.data_error)$('#data-error').textContent='Live data could not be refreshed. Last snapshot: '+(next.snapshot_time || 'unavailable')+'. '+next.data_error;
  if(!next.counts){$('#connection').textContent='Waiting for valid project data';return;}
  const previous=data;data=next;renderWorkerHealth();
  if(previous?.revision===next.revision)return;
  const focused=document.activeElement;
  const focusId=focused?.id;
  const focusHref=focused?.tagName==='A'?focused.getAttribute('href'):null;
  const focusLinkRegion=focused?.closest('#app-table,#sources,#build-strip')?.id;
  const focusTask=focused?.closest('.task');
  const focusPanel=focusTask?.closest('#anatomy-detail')?'#anatomy-detail':'#checklist';
  const focusKey=focusTask?.dataset.task;
  const focusTag=focused?.tagName;
  const focusComponent=focused?.dataset.component, focusStatus=focused?.dataset.status, focusGroup=focused?.dataset.group;
  const expanded=[...document.querySelectorAll('#anatomy-detail .task[open]')].map(el=>el.dataset.task);
  const selected=$('#component').value;
  $('#component').innerHTML='<option value="">All components</option>'+data.components.map(c=>`<option>${escape(c.name)}</option>`).join('');$('#component').value=selected;
  $('#revision').textContent=`${data.git.branch} · ${data.git.head.slice(0,8)}`;
  renderChecklist();renderOverview();renderCompatibility();renderMetrics();renderAnatomy();renderBuilds();
  document.querySelectorAll('#anatomy-detail .task').forEach(el=>el.open=expanded.includes(el.dataset.task));
  let target=focusId?document.getElementById(focusId):null;
  if(focusKey)target=[...document.querySelectorAll(focusPanel+' .task')].find(el=>el.dataset.task===focusKey)?.querySelector(focusTag==='A'?'a':'summary');
  for(const [key,value] of [['component',focusComponent],['status',focusStatus],['group',focusGroup]])if(value!==undefined)target=[...document.querySelectorAll('button[data-'+key+']')].find(el=>el.dataset[key]===value);
  if(!target && focusHref && focusLinkRegion)target=[...document.querySelectorAll('#'+focusLinkRegion+' a')].find(el=>el.getAttribute('href')===focusHref);
  if(target && target!==document.activeElement)target.focus({preventScroll:true});
  if(currentView==='roadmap')renderRoadmap();
}
function route(){const view=location.hash.slice(1)||'overview';currentView=document.getElementById('view-'+view)?view:'overview';document.querySelectorAll('.view').forEach(el=>el.hidden=el.id!=='view-'+currentView);document.querySelectorAll('nav a').forEach(a=>{a.classList.toggle('active',a.dataset.view===currentView);a.setAttribute('aria-current',a.dataset.view===currentView?'page':'false');});if(data && currentView==='roadmap')renderRoadmap();}
async function refresh(){try{const response=await fetch('/api/state',{cache:'no-store'});if(!response.ok)throw Error(response.status);render(await response.json());}catch(error){$('#connection').textContent='Disconnected · retrying';$('#signal').className='signal error';}}
function connect(){source=new EventSource('/api/events');source.onmessage=event=>{try{render(JSON.parse(event.data));}catch(error){$('#connection').textContent='Invalid live update';}};source.onerror=()=>{$('#connection').textContent='Reconnecting…';$('#signal').className='signal error';};}
$('#search').addEventListener('input',renderChecklist);$('#component').onchange=renderChecklist;$('#status').onchange=renderChecklist;
$('#clear-filters').onclick=()=>{$('#search').value='';$('#component').value='';$('#status').value='';renderChecklist();};$('#refresh').onclick=refresh;
window.addEventListener('hashchange',route);route();refresh();connect();
setInterval(()=>{if(Date.now()-lastMessage>15000)refresh();},10000);

setInterval(renderWorkerHealth,5000);
