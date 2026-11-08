const logs = new Map();
export const escapeText = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
export function durationText(seconds) {
  if (!Number.isFinite(seconds)) return 'Timing not recorded';
  seconds = Math.max(0, Math.floor(seconds));
  return seconds < 60 ? `${seconds}s` : `${Math.floor(seconds / 60)}m ${seconds % 60}s`;
}
export function progressSummary(progress) {
  if (!progress) return 'Individual step records unavailable';
  const counts = progress.counts;
  return `${counts.passed} passed · ${counts.running} running · ${counts.pending} pending · ${counts.failed} failed · ${counts.skipped} skipped`;
}
export function stepMarkup(build, step) {
  const key = `${build.number}-${step.id}`;
  const timer = step.status === 'running' && Number.isFinite(step.started)
    ? `<time data-progress-start="${step.started}">Elapsed ${durationText(Date.now()/1000-step.started)}</time>`
    : `<time>${escapeText(durationText(step.duration))}</time>`;
  const times = [['Started',step.started],['Ended',step.ended]].filter(([,value])=>Number.isFinite(value)).map(([label,value])=>`${label}: ${new Date(value*1000).toLocaleString()}`).join(' · ');
  return `<details class="build-step" data-progress-key="${escapeText(key)}"><summary id="step-${escapeText(key)}"><span>${escapeText(step.label)}</span><span class="step-status ${escapeText(step.status)}">${escapeText(step.status)}</span>${timer}</summary>${times?`<p class="muted">${escapeText(times)}</p>`:''}${step.reason?`<p>${escapeText(step.reason)}</p>`:''}<button id="log-${escapeText(key)}" data-log-key="${escapeText(key)}" data-run="${escapeText(build.run_id)}" data-step="${escapeText(step.id)}">Load / refresh log excerpt</button><p class="muted" data-log-note="${escapeText(key)}"></p><pre tabindex="0" id="excerpt-${escapeText(key)}" class="build-log"></pre></details>`;
}
export function renderBuildProgress(container, builds) {
  const expanded = new Set([...container.querySelectorAll('details[open]')].map(el => el.dataset.progressKey));
  const focused = container.contains(document.activeElement) ? document.activeElement.id : null;
  const scrolls = new Map([...container.querySelectorAll('pre')].map(el => [el.id, el.scrollTop]));
  const pageX = window.scrollX, pageY = window.scrollY;
  container.innerHTML = builds.length ? builds.map(build => {
    const progress = build.progress;
    const current = progress?.steps.find(step => step.id === progress.current);
    const revision = build.commit ? `Commit ${build.commit.slice(0,12)}` : build.source_tree ? `Source snapshot ${build.source_tree.slice(0,12)}` : 'Revision pending';
    return `<article class="build-card"><div class="build-number">${build.number?'#'+build.number:'Queue'}</div><div><h3>${escapeText(build.title || 'Feature integration')}</h3><p class="mono">${escapeText(revision)}${build.attempts?` · attempt ${build.attempts}`:''}</p><p>${build.started?new Date(build.started*1000).toLocaleString():''}</p>${build.error?`<p class="build-error">${escapeText(build.error)}</p>`:''}${build.scope?`<p>${escapeText(build.scope)}</p>`:''}<section class="build-progress"><p class="step-counts" role="status">${escapeText(progressSummary(progress))}</p>${current?`<p>Current: <strong>${escapeText(current.label)}</strong></p>`:''}${progress?.historical?`<p class="historical-progress">Historical reconstruction · ${escapeText(progress.evidence)}</p>`:''}${progress?.reason?`<p class="build-error">${escapeText(progress.reason)}</p>`:''}${build.progress_error?`<p class="build-error">${escapeText(build.progress_error)}</p>`:''}${(progress?.steps || []).map(step=>stepMarkup(build,step)).join('')}</section>${build.run?`<p class="muted">Receipt directory: ${escapeText(build.run)}</p>`:''}</div><span class="state-label">${escapeText(build.status)}</span></article>`;
  }).join('') : '<p class="empty">No numbered builds recorded yet.</p>';
  container.querySelectorAll('details').forEach(el => { el.open = expanded.has(el.dataset.progressKey); });
  const showLog = key => {
    const item = logs.get(key), pre = document.getElementById('excerpt-'+key);
    if (!item || !pre) return;
    pre.textContent = item.text;
    container.querySelector(`[data-log-note="${CSS.escape(key)}"]`).textContent = [item.note,item.truncated?'Showing a bounded tail (up to 64 KiB).':''].filter(Boolean).join(' ');
  };
  logs.forEach((_,key)=>showLog(key));
  container.querySelectorAll('[data-log-key]').forEach(button => button.onclick = async () => {
    const key = button.dataset.logKey;
    button.disabled = true;
    try {
      const response = await fetch(`/api/build-log?run=${encodeURIComponent(button.dataset.run)}&step=${encodeURIComponent(button.dataset.step)}`, {cache:'no-store'});
      const item = await response.json();
      if (!response.ok) throw Error(item.error || response.status);
      logs.set(key,item);
      if (logs.size > 20) logs.delete(logs.keys().next().value);
      showLog(key);
    } catch(error) {
      logs.set(key,{text:String(error),note:'Log excerpt unavailable'});showLog(key);
    } finally { const current = document.getElementById('log-'+key); if(current)current.disabled=false; }
  });
  scrolls.forEach((top,id)=>{const element=document.getElementById(id);if(element)element.scrollTop=top;});
  if(focused)document.getElementById(focused)?.focus({preventScroll:true});
  window.scrollTo(pageX,pageY);
}
if (typeof document !== 'undefined') setInterval(()=>{
  document.querySelectorAll('[data-progress-start]').forEach(el=>{
    el.textContent='Elapsed '+durationText(Date.now()/1000-Number(el.dataset.progressStart));
  });
},1000);
