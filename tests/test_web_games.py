#!/usr/bin/env python3
"""Exercise real CEF WebGL/WASM, service workers and PWA lifecycle without OS input."""
import ctypes,json,os,re,select,socket,subprocess,sys,tempfile,time
from pathlib import Path
from test_embedded_browser_security import Message
ROOT=Path(__file__).resolve().parents[1]
BINARY=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'build/browser/Release/OS999Browser.app/Contents/MacOS/OS999Browser'
class Session:
    def __init__(self,profile,log):
        self.socket,child=socket.socketpair();self.buffer=bytearray();self.events=[];self.loading={}
        self.log=log.open('wb')
        self.process=subprocess.Popen([str(BINARY),'--os999-embed-fd='+str(child.fileno()),'--os999-profile='+str(profile)],pass_fds=(child.fileno(),),stdout=self.log,stderr=subprocess.STDOUT)
        child.close();self.wait(lambda m:m.kind==110)
    def send(self,kind,id=1,url='',a=640,b=480):
        self.socket.sendall(bytes(Message(kind=kind,id=id,x=100,a=a,b=b,text=url.encode())))
    def wait(self,predicate,timeout=50):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            if len(self.buffer)<ctypes.sizeof(Message):
                if not select.select([self.socket],[],[],1)[0]:continue
                data=self.socket.recv(65536);assert data,'helper closed'
                self.buffer.extend(data)
            while len(self.buffer)>=ctypes.sizeof(Message):
                m=Message.from_buffer_copy(self.buffer[:ctypes.sizeof(Message)]);del self.buffer[:ctypes.sizeof(Message)]
                if m.kind==102:self.loading[m.id]=bool(m.a)
                if m.kind in [113,114]:self.events.append((m.kind,m.id,bytes(m.text).decode()))
                if predicate(m):return m
        raise AssertionError('Timed out waiting for game/PWA result: '+repr(self.events[-4:]))
    def loaded(self,id=1):
        if self.loading.get(id,True):self.wait(lambda m:m.kind==102 and m.id==id and m.a==0)
    def caps(self,id=1,focused=None):
        def matches(m):
            if m.kind!=114 or m.id!=id:return False
            j=json.loads(bytes(m.text))
            return (j.get('wasm') and j.get('offlineCache') and (focused is None or j.get('focused')==focused)) or j.get('error')
        return json.loads(bytes(self.wait(matches).text))
    def close(self):
        try:self.send(23,0);assert self.process.wait(timeout=20)==0
        finally:
            self.socket.close();self.log.close()
            if self.process.poll() is None:self.process.kill();self.process.wait()
def main():
    if sys.platform=='darwin':
        text=subprocess.check_output(['security','find-identity','-v','-p','codesigning'],text=True)
        m=re.search(r'"(Apple Development:[^"]+)"',text)
        subprocess.run([sys.executable,str(ROOT/'browser/scripts/sign.py'),str(BINARY.resolve().parents[2]),'--identity',m.group(1) if m else '-'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    results=[]
    with tempfile.TemporaryDirectory(prefix='os999-games-') as temp:
        profile=Path(temp).resolve()/'profile'
        s=Session(profile,ROOT/'build/web-games-runtime.log')
        try:
            s.send(1,url='https://os999.invalid/play/')
            caps=s.caps();print('Capabilities',json.dumps(caps),flush=True)
            assert caps.get('secure') and caps.get('wasm') and caps.get('serviceWorker') and caps.get('offlineCache') and caps.get('gamepadAPI'),caps
            if sys.platform=='darwin':assert caps.get('webgl1') and caps.get('webgl2'),caps
            results.append({'case':'real renderer capabilities','result':caps})
            assert not caps["focused"], "unfocused tab claimed native focus"
            s.send(20,a=1); focused=s.caps(focused=True);assert focused['focused']
            s.send(1,id=2,url='https://os999.invalid/play/');s.caps(2)
            s.send(20,id=2,a=1);s.caps(2,True)
            s.send(20,id=2,a=0);s.caps(2,False)
            results.append({'case':'focus ownership','passed':True})
            s.loaded()
            s.send(24);result=s.wait(lambda m:m.kind==113 and m.id==1);assert result.a,bytes(result.text)
            registries=list(profile.rglob('os999-apps.json'));assert len(registries)==1,registries
            registry=registries[0];data=json.loads(registry.read_text());assert len(data)==1 and data[0]['name']=='Controller Arena'
            s.send(24);assert s.wait(lambda m:m.kind==113 and m.id==1).a;assert len(json.loads(registry.read_text()))==1
            results.append({'case':'install and idempotent reinstall','passed':True})
        finally:s.close()
        s=Session(profile,ROOT/'build/web-games-restart.log')
        try:
            assert len(json.loads(registry.read_text()))==1
            s.send(1,url=data[0]['start_url']);caps=s.caps();assert caps['serviceWorker'] and caps['offlineCache'],caps
            results.append({'case':'restart with persistent app and worker cache','passed':True})
            s.send(25);assert s.wait(lambda m:m.kind==113).a;assert json.loads(registry.read_text())==[]
            results.append({'case':'remove app shortcut','passed':True})
            s.send(1,id=2,url='about:blank');s.wait(lambda m:m.kind==102 and m.id==2 and m.a==0)
            s.send(24,id=2);assert not s.wait(lambda m:m.kind==113 and m.id==2).a
            results.append({'case':'unverified page cannot install','passed':True})
        finally:s.close()
    (ROOT/'build/web-games-results.json').write_text(json.dumps({'status':'passed','results':results,'limits':'No physical controller, real-game compatibility, performance, vibration or Linux GPU claim.'},indent=2)+'\n')
    print('Web game and PWA lifecycle tests passed')
if __name__=='__main__':main()
