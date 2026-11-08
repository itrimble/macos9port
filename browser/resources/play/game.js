'use strict';
const report = {secure: isSecureContext, webgl1:false, webgl2:false, wasm:false, gamepadAPI:typeof navigator.getGamepads==='function', controllers:0, serviceWorker:false, offlineCache:false};
const canvas=document.querySelector('#game'), status=document.querySelector('#status');
function context(kind) {
  const c=document.createElement('canvas'); c.width=c.height=2;
  const gl=c.getContext(kind);
  if (!gl) return false;
  gl.clearColor(0.25,0.5,0.75,1); gl.clear(gl.COLOR_BUFFER_BIT);
  const p=new Uint8Array(4); gl.readPixels(0,0,1,1,gl.RGBA,gl.UNSIGNED_BYTE,p);
  const ok=gl.getError()===gl.NO_ERROR && Math.abs(p[0]-64)<=1 && Math.abs(p[1]-128)<=1 && Math.abs(p[2]-191)<=1;
  const debug=gl.getExtension('WEBGL_debug_renderer_info');
  if (debug) report[kind+'Renderer']=gl.getParameter(debug.UNMASKED_RENDERER_WEBGL);
  gl.getExtension('WEBGL_lose_context')?.loseContext();
  return ok;
}
function publish() {
  report.focused=document.hasFocus();
  status.textContent=JSON.stringify(report,null,2);
  console.log('OS999_CAPABILITIES '+JSON.stringify(report));
}
let add=(a,b)=>a+b, score=0, active=false, x=0,y=0, last=performance.now(), pressed=false;
const keys=new Set();
document.querySelector('#play').onclick=()=>{active=true; canvas.focus();};
canvas.tabIndex=0;
addEventListener('keydown',e=>{if(['ArrowLeft','ArrowRight','ArrowUp','ArrowDown',' '].includes(e.key)){keys.add(e.key);if(active)e.preventDefault();}});
addEventListener('keyup',e=>keys.delete(e.key));
addEventListener('focus',()=>publish());
addEventListener('blur',()=>{publish();active=false;keys.clear();pressed=false;});
document.addEventListener('visibilitychange',()=>{if(document.hidden){active=false;keys.clear();}});
addEventListener('gamepadconnected',()=>{report.controllers=[...navigator.getGamepads()].filter(Boolean).length;publish();});
addEventListener('gamepaddisconnected',()=>{report.controllers=[...navigator.getGamepads()].filter(Boolean).length;publish();});
const gl=canvas.getContext('webgl2')||canvas.getContext('webgl');
if(gl){
  const v2=typeof WebGL2RenderingContext!=='undefined'&&gl instanceof WebGL2RenderingContext;
  function shader(type,code){const s=gl.createShader(type);gl.shaderSource(s,code);gl.compileShader(s);if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(s));return s;}
  const program=gl.createProgram();
  gl.attachShader(program,shader(gl.VERTEX_SHADER,(v2?'#version 300 es\nin':'attribute')+' vec2 p; uniform vec2 pos; void main(){gl_Position=vec4(p*0.09+pos,0.,1.);}'));
  gl.attachShader(program,shader(gl.FRAGMENT_SHADER,(v2?'#version 300 es\n':'')+'precision mediump float;'+(v2?'out vec4 color;':'')+'void main(){'+(v2?'color':'gl_FragColor')+'=vec4(.3,.85,1.,1.);}'));
  gl.linkProgram(program);if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(program));
  gl.useProgram(program);gl.bindBuffer(gl.ARRAY_BUFFER,gl.createBuffer());gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([-1,-1,1,-1,-1,1,1,1]),gl.STATIC_DRAW);
  const p=gl.getAttribLocation(program,'p'),pos=gl.getUniformLocation(program,'pos');gl.enableVertexAttribArray(p);gl.vertexAttribPointer(p,2,gl.FLOAT,false,0,0);
  function frame(now){
    const dt=Math.min((now-last)/1000,.05);last=now;
    const pads=report.gamepadAPI?[...navigator.getGamepads()].filter(Boolean):[],pad=pads[0];
    if(active&&document.hasFocus()&&!document.hidden){
      const axis=v=>Math.abs(v||0)>.15?v:0;
      x=Math.max(-.9,Math.min(.9,x+dt*(axis(pad?.axes[0])+(keys.has('ArrowRight')?1:0)-(keys.has('ArrowLeft')?1:0))));
      y=Math.max(-.9,Math.min(.9,y-dt*(axis(pad?.axes[1])+(keys.has('ArrowDown')?1:0)-(keys.has('ArrowUp')?1:0))));
      const fire=!!pad?.buttons[0]?.pressed||keys.has(' ');
      if(fire&&!pressed){score=add(score,1);document.querySelector('#score').textContent='Score: '+score;}
      pressed=fire;
    }
    gl.clearColor(.03,.07,.12,1);gl.clear(gl.COLOR_BUFFER_BIT);gl.uniform2f(pos,x,y);gl.drawArrays(gl.TRIANGLE_STRIP,0,4);
    requestAnimationFrame(frame);
  } requestAnimationFrame(frame);
  canvas.addEventListener('webglcontextlost',e=>{e.preventDefault();active=false;status.textContent='Graphics context lost. Reload to resume.';});
}
(async()=>{
  try{
    report.webgl1=context('webgl');report.webgl2=context('webgl2');
    const result=await WebAssembly.instantiateStreaming(fetch('add.wasm'));
    add=result.instance.exports.add;report.wasm=add(19,23)===42&&add(-7,4)===-3;
    if('serviceWorker' in navigator){
      await navigator.serviceWorker.register('sw.js',{scope:'/play/'});await navigator.serviceWorker.ready;
      if(!navigator.serviceWorker.controller)await new Promise(resolve=>navigator.serviceWorker.addEventListener('controllerchange',resolve,{once:true}));
      report.serviceWorker=!!navigator.serviceWorker.controller;
      report.offlineCache=(await (await fetch('offline-check')).text())==='offline-cache-ready';
    }
  }catch(e){report.error=String(e);}
  publish();
})();
