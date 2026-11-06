// Die Weboberflaeche liegt als ein Stueck im Flash. Keine externen
// Schriften, Skripte oder CDNs -- am Tag der offenen Tuer haengt die Wand
// in ihrem eigenen WLAN ohne Internet.
#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="de"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0b0e14">
<title>Kletterwand</title>
<style>
:root{
  --bg:#0b0e14; --panel:#151a24; --panel2:#1d2430; --line:#2a3342;
  --fg:#e8edf5; --dim:#8b97a8; --accent:#4ade80; --accent2:#38bdf8;
  --start:#22c55e; --move:#3b82f6; --top:#ef4444; --foot:#d946ef;
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;background:var(--bg);color:var(--fg);
  font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
body{padding:0 16px 48px;max-width:560px;margin:0 auto}
h1{font-size:1.35rem;margin:18px 0 2px;letter-spacing:-.02em}
h2{font-size:.82rem;text-transform:uppercase;letter-spacing:.09em;
   color:var(--dim);margin:0 0 10px;font-weight:600}
.sub{color:var(--dim);font-size:.82rem;margin:0 0 14px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:16px;
  padding:16px;margin-bottom:14px}
.row{display:flex;gap:8px;flex-wrap:wrap}
.modes{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.modes button:last-child{grid-column:1/-1}
.tabs{display:flex;gap:6px;margin:14px 0}
.tab{flex:1;padding:10px;border-radius:12px;border:1px solid var(--line);
  background:var(--panel);color:var(--dim);font-size:.9rem;font-weight:600;
  text-align:center;cursor:pointer}
.tab.on{background:var(--panel2);color:var(--fg);border-color:var(--accent)}
button{font:inherit;color:var(--fg);background:var(--panel2);
  border:1px solid var(--line);border-radius:12px;padding:12px 14px;
  min-height:46px;cursor:pointer;flex:1}
button:active{transform:scale(.97)}
button.on{border-color:var(--accent);background:#17301f;color:#d7ffe6}
button.wide{flex:1 1 100%}
button.small{padding:8px 10px;min-height:38px;font-size:.85rem;flex:0 1 auto}
.danger{border-color:#7f1d1d;background:#2a1414}
/* Wandansicht */
.board{background:#070a10;border:1px solid var(--line);border-radius:14px;
  padding:14px;display:flex;flex-direction:column;align-items:center;gap:12px}
.grid{display:grid;grid-template-columns:repeat(6,1fr);gap:9px;width:100%}
.mini{display:grid;grid-template-columns:repeat(2,1fr);gap:9px;width:38%}
.cell{aspect-ratio:1;border-radius:50%;background:#111722;
  border:1px solid #222b38;position:relative;cursor:pointer;
  display:flex;align-items:center;justify-content:center;
  font-size:.62rem;color:#4b5565;transition:box-shadow .12s}
.cell.sensor{border:2px solid #3f4c60}
.cell.sensor::after{content:"";position:absolute;bottom:-1px;
  width:5px;height:5px;border-radius:50%;background:#54637a}
.flip .grid,.flip .mini{transform:scaleY(-1)}
.flip .cell{transform:scaleY(-1)}
.legend{display:flex;gap:12px;flex-wrap:wrap;font-size:.72rem;color:var(--dim)}
.legend i{display:inline-block;width:9px;height:9px;border-radius:50%;
  margin-right:5px;vertical-align:middle}
/* Regler */
label.sl{display:block;font-size:.8rem;color:var(--dim);margin:12px 0 4px}
input[type=range]{width:100%;height:36px;accent-color:var(--accent);background:none}
input[type=text],input[type=number]{font:inherit;background:var(--panel2);
  color:var(--fg);border:1px solid var(--line);border-radius:10px;padding:10px;width:100%}
.val{float:right;color:var(--fg);font-variant-numeric:tabular-nums}
.big{font-size:2.3rem;font-weight:700;font-variant-numeric:tabular-nums;
  letter-spacing:-.02em;line-height:1.1}
.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;text-align:center}
.stats div span{display:block;font-size:.7rem;color:var(--dim);
  text-transform:uppercase;letter-spacing:.06em;margin-top:2px}
.pill{display:inline-flex;align-items:center;gap:6px;font-size:.74rem;
  color:var(--dim);background:var(--panel2);border:1px solid var(--line);
  border-radius:999px;padding:4px 10px}
.dot{width:7px;height:7px;border-radius:50%;background:#64748b}
.dot.live{background:var(--accent);box-shadow:0 0 8px var(--accent)}
table{width:100%;border-collapse:collapse;font-size:.8rem}
td{padding:5px 0;border-bottom:1px solid var(--line);color:var(--dim)}
td:last-child{text-align:right;color:var(--fg);font-variant-numeric:tabular-nums}
details{margin-top:4px}
summary{cursor:pointer;color:var(--dim);font-size:.85rem;padding:8px 0}
.bars{display:grid;grid-template-columns:repeat(8,1fr);gap:5px;
  align-items:end;height:70px;margin-top:8px}
.bars div{background:var(--panel2);border-radius:4px 4px 0 0;position:relative}
.bars div.hit{background:var(--accent2)}
.bars span{position:absolute;bottom:-17px;left:0;right:0;text-align:center;
  font-size:.6rem;color:var(--dim)}
.hide{display:none}
.note{font-size:.8rem;color:var(--dim);line-height:1.5}
.note b{color:var(--fg);font-weight:600}
</style></head><body>

<h1>Kletterwand</h1>
<p class="sub"><span class="pill"><span class="dot" id="live"></span>
  <span id="netinfo">verbinde ...</span></span></p>

<div class="tabs">
  <div class="tab on" data-tab="steuern">Steuern</div>
  <div class="tab" data-tab="spiel">Spiel</div>
  <div class="tab" data-tab="info">Info</div>
</div>

<!-- Wandansicht, immer sichtbar -->
<div class="card board" id="boardCard">
  <div class="grid" id="grid"></div>
  <div style="font-size:.68rem;color:#64748b;letter-spacing:.08em;
       text-transform:uppercase;margin-top:-4px">Zusatzgriffe</div>
  <div class="mini" id="miniGrid"></div>
  <div class="legend">
    <span>&#9900; = Griff mit Touch-Sensor (antippen schaltet die Farbe)</span>
  </div>
</div>

<!-- ------------------------------------------------ Steuern -->
<section data-page="steuern">
  <div class="card hide" id="remoteCard" style="border-color:var(--accent2)">
    <h2>Ferngesteuert</h2>
    <p class="note">Die Wand wird gerade von einem anderen Programm über die
      Schnittstelle gesteuert. Animationen sind ausgesetzt, es leuchtet nur,
      was dieses Programm setzt.</p>
    <div class="row" style="margin-top:10px">
      <button class="small" data-mode="1">Zurück zum Regenbogen</button>
    </div>
  </div>
  <div class="card">
    <h2>Betriebsart</h2>
    <div class="modes">
      <button data-mode="1">Regenbogen</button>
      <button data-mode="2">Griffe</button>
      <button data-mode="3">Spiel</button>
      <button data-mode="4">Licht aus</button>
    </div>
    <label class="sl">Helligkeit <span class="val" id="brightVal">-</span></label>
    <input type="range" id="bright" min="5" max="255" step="5">
    <label class="sl">Tempo der Animation <span class="val" id="speedVal">-</span></label>
    <input type="range" id="speed" min="0" max="100" step="5">
  </div>

  <div class="card">
    <h2>Griffe schalten</h2>
    <p class="note">In der Betriebsart <b>Griffe</b> schaltet jede Berührung
      an der Wand die Farbe weiter: aus &rarr; blau &rarr; grün &rarr; rot.
      Hier geht das auch vom Handy aus.</p>
    <div class="row" id="gripRow" style="margin-top:12px"></div>
  </div>
  <div class="card">
    <h2>Stimmen die Farben nicht?</h2>
    <p class="note">Wenn die Wand andere Farben zeigt als diese Seite, lässt
      sich das in einer halben Minute richtigstellen.</p>
    <div class="row" style="margin-top:10px">
      <button class="small" onclick="openTab('info');
        document.getElementById('farbtest').scrollIntoView({behavior:'smooth'})">
        Zum Farbtest</button>
    </div>
  </div>
</section>

<!-- ------------------------------------------------ Spiel -->
<section data-page="spiel" class="hide">
  <div class="card">
    <h2>Griff-Jagd</h2>
    <p class="note">Ein Griff leuchtet <b style="color:#22d3ee">türkis</b> &ndash;
      schnell hingreifen! Sobald du ihn berührst, leuchtet der nächste.
      Wer schafft alle Griffe am schnellsten? Starten kann man auch
      direkt an der Wand: einfach einen Griff antippen.</p>
    <input type="text" id="player" maxlength="12" placeholder="Dein Name (für die Bestenliste)">
    <div class="row" style="margin-top:10px">
      <button class="small" data-n="5">5 Griffe</button>
      <button class="small on" data-n="10">10 Griffe</button>
      <button class="small" data-n="20">20 Griffe</button>
    </div>
    <div class="stats" style="margin:18px 0">
      <div><div class="big" id="gHits">0</div><span>Treffer</span></div>
      <div><div class="big" id="gTime">0.0</div><span>Sekunden</span></div>
      <div><div class="big" id="gSplit" style="color:var(--accent2)">-</div><span>letzter Griff</span></div>
    </div>
    <div class="row">
      <button id="gStart" class="on">Los geht's!</button>
      <button id="gStop">Abbrechen</button>
    </div>
    <p class="note" id="gMsg" style="margin-top:12px"></p>
  </div>
  <div class="card">
    <h2>Bestenliste</h2>
    <table id="topList"></table>
    <p class="note" style="margin-top:10px">Sortiert wird nach der Zeit
      <b>je Griff</b> &ndash; so sind kurze und lange Runden vergleichbar.</p>
    <div class="row" style="margin-top:10px">
      <button class="small danger" id="gReset">Liste löschen</button>
    </div>
  </div>
</section>

<!-- ------------------------------------------------ Info -->
<section data-page="info" class="hide">
  <div class="card">
    <h2>Was ist das?</h2>
    <p class="note">Eine Kletterwand im Kleinformat, nach dem Vorbild eines
      <b>Kilterboards</b>: Hinter jedem Griff sitzt eine eigene Beleuchtung.
      Sechs der Griffe erkennen per <b>kapazitivem Sensor</b>, dass eine
      Hand sie berührt &ndash; ohne Taster, ohne mechanischen Kontakt.
      Daraus wird ein Reaktionsspiel: Ein Griff leuchtet, und wer ihn am
      schnellsten erwischt, kommt auf die Bestenliste.</p>
  </div>
  <div class="card">
    <h2>Technik</h2>
    <table>
      <tr><td>Rechner</td><td>Seeed XIAO ESP32-C3</td></tr>
      <tr><td>Touch-Sensor</td><td>CAP1298, 8 Kanäle, I2C 0x28</td></tr>
      <tr><td>Beleuchtung</td><td>50x WS2812B, einzeln adressierbar</td></tr>
      <tr><td>Griffpositionen</td><td>5 x 6 plus 2 x 2</td></tr>
      <tr><td>Bedienung</td><td>Diese Seite, direkt vom Board</td></tr>
      <tr><td>Firmware</td><td>C++ / Arduino, Update per WLAN</td></tr>
    </table>
    <p class="note" style="margin-top:12px">Der Mikrocontroller spannt sein
      eigenes WLAN auf und liefert diese Seite selbst aus &ndash; es läuft
      kein Server irgendwo im Internet, alles passiert in dem kleinen
      Rechner an der Wand.</p>
  </div>
  <div class="card">
    <h2>Sensor &amp; System</h2>
    <details>
      <summary>Messwerte und Einstellungen anzeigen</summary>
      <div class="bars" id="bars"></div>
      <div style="height:20px"></div>
      <div class="row">
        <button class="small" id="calib">Sensor kalibrieren</button>
        <button class="small" id="srcToggle">Quelle: Status</button>
      </div>
      <label class="sl">Empfindlichkeit <span class="val" id="sensVal">-</span></label>
      <input type="range" id="sens" min="0" max="7" step="1">
      <label class="sl">Delta-Schwelle <span class="val" id="thrVal">-</span></label>
      <input type="range" id="thr" min="2" max="120" step="1">
      <table style="margin-top:12px">
        <tr><td>Firmware</td><td id="iVer">-</td></tr>
        <tr><td>Laufzeit</td><td id="iUp">-</td></tr>
        <tr><td>Freier Speicher</td><td id="iHeap">-</td></tr>
        <tr><td>Sensor</td><td id="iSensor">-</td></tr>
        <tr><td>Netzwerk</td><td id="iNet">-</td></tr>
      </table>
      <div class="row" style="margin-top:12px">
        <button class="small" onclick="location.href='/update'">Firmware-Update</button>
        <button class="small danger" id="reboot">Neu starten</button>
      </div>
    </details>
  </div>
  <div class="card" id="farbtest">
    <h2>Farben richtigstellen</h2>
    <p class="note">Zeigt die Wand andere Farben als diese Seite, ist die
      Reihenfolge der Farbkanäle im LED-Streifen eine andere. Testbild
      einschalten und angeben, was die Wand <b>wirklich</b> zeigt &ndash;
      die richtige Einstellung ergibt sich daraus von selbst.</p>
    <div class="row" style="margin-top:12px">
      <button data-mode="5" class="wide">1. Testbild anzeigen</button>
    </div>
    <div id="ctAsk"></div>
    <p class="note" id="ctResult" style="margin-top:10px"></p>
    <details style="margin-top:6px">
      <summary>Lieber selbst durchprobieren</summary>
      <div class="row" style="margin-top:8px" id="orderRow"></div>
    </details>
  </div>
  <div class="card">
    <h2>Verbinden</h2>
    <p class="note">Wenn die Seite beim Verbinden mit dem WLAN von selbst
      in einem Fenster aufgehen soll, lässt sich das hier einschalten.
      Standardmäßig ist es aus &ndash; die Steuerung erreicht man dann
      über die Adresse oben.</p>
    <div class="row" style="margin-top:10px">
      <button class="small" id="captive">Seite automatisch öffnen</button>
    </div>
  </div>
  <div class="card">
    <h2>Ansicht</h2>
    <div class="row"><button class="small" id="flip">Wandansicht drehen</button></div>
    <p class="note" style="margin-top:8px">Falls die Anzeige auf dem Kopf
      steht, weil die Wand anders herum hängt.</p>
  </div>
</section>

<script>
const MAIN=30, CELLS=34, ROLE=["#111722","#22c55e","#3b82f6","#ef4444","#d946ef"];
let flipped=localStorage.getItem('kb_flip')==='1', state=null;

// ---------- Wandansicht aufbauen
function buildBoard(){
  const g=document.getElementById('grid'), m=document.getElementById('miniGrid');
  g.innerHTML=m.innerHTML='';
  for(let i=0;i<CELLS;i++){
    const d=document.createElement('div');
    d.className='cell'; d.dataset.i=i;
    if(i<12){ d.classList.add('sensor'); d.onclick=()=>cellTap(i); }
    (i<MAIN?g:m).appendChild(d);
  }
  document.getElementById('boardCard').classList.toggle('flip',flipped);
}
// Die zwölf oberen Positionen gehören zu den sechs Touch-Griffen;
// ein Tipper schaltet dort dieselbe Farbfolge weiter wie an der Wand.
async function cellTap(i){
  if(i>=12) return;
  if(state && state.mode!==2) await fetch('/api/mode?m=2');
  go('/api/grip?i='+Math.floor(i/2));
}

// ---------- Kommunikation
async function go(url){ try{ await fetch(url); refresh(); }catch(e){} }
async function refresh(){
  try{
    const r=await fetch('/api/status',{cache:'no-store'});
    state=await r.json(); paint(state);
    document.getElementById('live').classList.add('live');
  }catch(e){ document.getElementById('live').classList.remove('live'); }
}

// ---------- Anzeige
function paint(s){
  // Farben der Wand
  for(let i=0;i<CELLS;i++){
    const el=document.querySelector('.cell[data-i="'+i+'"]'); if(!el) continue;
    const hex='#'+s.cells.substr(i*6,6);
    const dark=hex==='#000000';
    el.style.background=dark?'#111722':hex;
    el.style.boxShadow=dark?'none':'0 0 14px '+hex+'cc';
  }
  document.querySelectorAll('[data-mode]').forEach(b=>
    b.classList.toggle('on', +b.dataset.mode===s.mode));
  document.getElementById('remoteCard').classList.toggle('hide', s.mode!==6);
  setSlider('bright',s.bright,s.bright);
  setSlider('speed',s.speed,s.speed+' %');
  // Griff-Knöpfe
  const gr=document.getElementById('gripRow');
  if(gr.children.length!==6){
    gr.innerHTML='';
    for(let i=0;i<6;i++){
      const b=document.createElement('button');
      b.textContent='Griff '+(i+1); b.onclick=()=>go('/api/grip?i='+i);
      gr.appendChild(b);
    }
  }
  const gc=['#111722','#3b82f6','#22c55e','#ef4444'];
  [...gr.children].forEach((b,i)=>{
    b.style.borderColor = s.touch.down[i+2] ? '#ffffff' : gc[s.grips[i]];
    b.style.background  = s.grips[i] ? gc[s.grips[i]]+'33' : 'var(--panel2)';
  });
  // Spiel
  const g=s.game;
  document.getElementById('gHits').textContent=g.hits+'/'+g.total;
  document.getElementById('gTime').textContent=z(g.state?g.ms/1000:0);
  document.getElementById('gSplit').textContent=g.split?z(g.split/1000,2)+' s':'-';
  document.querySelectorAll('[data-n]').forEach(b=>
    b.classList.toggle('on',+b.dataset.n===g.total));
  const pl=document.getElementById('player');
  if(document.activeElement!==pl && !pl.dataset.touched) pl.value=g.player;
  // Bestenliste
  const tl=document.getElementById('topList');
  const rang=['1.','2.','3.','4.','5.'];
  tl.innerHTML=s.top.map((e,i)=>e.g
    ? '<tr><td>'+rang[i]+' '+esc(e.n)+'<br><span style="font-size:.72rem">'
      +z(e.ms/1000)+' s für '+e.g+' Griffe</span></td><td>'
      +z(e.ms/e.g/1000,2)+' s<br><span style="font-size:.72rem;color:#8b97a8">je Griff</span></td></tr>'
    : '<tr><td>'+rang[i]+' &mdash;</td><td></td></tr>').join('');
  const msg=['Bereit &ndash; Runde starten oder einen Griff an der Wand antippen.',
    'Achtung, gleich geht\'s los ...','Los! Den türkisen Griff anfassen.',
    '<b>Geschafft in '+z(g.ms/1000)+' s!</b>'
      +(g.bestSplit?' Schnellster Griff: '+z(g.bestSplit/1000,2)+' s.':'')
      +(g.miss?' '+g.miss+' Fehlgriffe.':'')];
  document.getElementById('gMsg').innerHTML=msg[g.state];
  // Sensor
  const bars=document.getElementById('bars');
  if(bars.children.length!==8){
    bars.innerHTML='';
    const nm=['T1','T2','G1','G2','G3','G4','G5','G6'];
    for(let i=0;i<8;i++){
      const d=document.createElement('div');
      d.innerHTML='<span>'+nm[i]+'</span>'; bars.appendChild(d);
    }
  }
  [...bars.children].forEach((d,i)=>{
    d.style.height=Math.max(3,Math.min(100,s.touch.delta[i]))+'%';
    d.classList.toggle('hit',!!s.touch.down[i]);
  });
  setSlider('sens',s.touch.sens,s.touch.sens);
  setSlider('thr',s.touch.thr,s.touch.thr);
  document.getElementById('srcToggle').textContent =
    'Quelle: '+(s.touch.useDelta?'Delta':'Status');
  buildColorTest();
  const or_=document.getElementById('orderRow');
  if(or_.children.length!==s.ledOrders.length){
    or_.innerHTML='';
    s.ledOrders.forEach((n,i)=>{
      const b=document.createElement('button');
      b.className='small'; b.textContent=n;
      b.onclick=()=>go('/api/ledorder?o='+i);
      or_.appendChild(b);
    });
  }
  [...or_.children].forEach((b,i)=>b.classList.toggle('on',i===s.ledOrder));
  const cap=document.getElementById('captive');
  cap.classList.toggle('on', !!s.net.captive);
  cap.textContent='Seite automatisch öffnen: '+(s.net.captive?'an':'aus');
  document.getElementById('iVer').textContent=s.ver;
  document.getElementById('iUp').textContent=fmtUp(s.up);
  document.getElementById('iHeap').textContent=(s.heap/1024).toFixed(0)+' kB';
  document.getElementById('iSensor').textContent=
    s.touch.ok?('bereit (0x'+s.touch.pid.toString(16)+')'):'nicht gefunden';
  document.getElementById('iNet').textContent=s.net.mode+' / '+s.net.ip;
  document.getElementById('netinfo').textContent=
    s.net.mode==='AP' ? s.net.ssid+' · '+s.net.ip : s.net.ssid+' · '+s.net.ip;
}
function setSlider(id,v,label){
  const el=document.getElementById(id);
  if(document.activeElement!==el) el.value=v;
  const l=document.getElementById(id+'Val'); if(l) l.textContent=label;
}
const z=(n,k=1)=>n.toFixed(k).replace('.',',');
// Farbtest: Reihe 1 ist als Rot programmiert, Reihe 2 als Grün. Aus dem,
// was die Wand davon tatsächlich zeigt, folgt eindeutig, wie der Streifen
// seine Kanäle erwartet.
const CT=[['R','Rot','#ef4444'],['G','Grün','#22c55e'],['B','Blau','#3b82f6']];
const gesehen={};
function buildColorTest(){
  const w=document.getElementById('ctAsk');
  if(w.children.length) return;
  [['2. Welche Farbe hat die <b>oberste</b> Reihe?',0],
   ['3. Und die <b>zweite</b> Reihe?',1]].forEach(([frage,nr])=>{
    const l=document.createElement('p');
    l.className='note'; l.style.margin='12px 0 6px'; l.innerHTML=frage;
    const r=document.createElement('div'); r.className='row'; r.dataset.q=nr;
    CT.forEach(([k,name,farbe])=>{
      const b=document.createElement('button');
      b.className='small'; b.textContent=name;
      b.style.borderColor=farbe;
      b.onclick=()=>{
        gesehen[nr]=k;
        [...r.children].forEach(x=>x.classList.remove('on'));
        b.classList.add('on'); applyColorTest();
      };
      r.appendChild(b);
    });
    w.appendChild(l); w.appendChild(r);
  });
}
function applyColorTest(){
  const out=document.getElementById('ctResult');
  if(gesehen[0]===undefined||gesehen[1]===undefined||!state) return;
  if(gesehen[0]===gesehen[1]){
    out.textContent='Die beiden Reihen können nicht dieselbe Farbe haben – bitte nochmal schauen.';
    return;
  }
  const akt=state.ledOrders[state.ledOrder];
  const p=[];
  p[akt.indexOf('R')]=gesehen[0];
  p[akt.indexOf('G')]=gesehen[1];
  const fehlt='RGB'.split('').find(c=>!p.includes(c));
  for(let i=0;i<3;i++) if(!p[i]) p[i]=fehlt;
  const ziel=p.join(''), idx=state.ledOrders.indexOf(ziel);
  if(idx<0){ out.textContent='Diese Kombination passt zu keiner Reihenfolge.'; return; }
  if(idx===state.ledOrder){
    out.innerHTML='<b>Passt</b> – die Reihenfolge '+ziel+' stimmt bereits.';
    return;
  }
  out.innerHTML='Umgestellt auf <b>'+ziel+'</b>. Die Wand sollte jetzt von oben '+
    'rot, grün, blau, gelb, weiß zeigen.';
  go('/api/ledorder?o='+idx);
}
function esc(t){return (t||'').replace(/[<>&"]/g,c=>
  ({'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;'}[c]));}
function fmtUp(ms){
  const s=Math.floor(ms/1000), h=Math.floor(s/3600), m=Math.floor(s%3600/60);
  return h?h+' h '+m+' min':m+' min '+(s%60)+' s';
}

// ---------- Bedienung
function openTab(name){
  const t=document.querySelector('.tab[data-tab="'+name+'"]'); if(t) t.onclick();
}
document.querySelectorAll('.tab').forEach(t=>t.onclick=()=>{
  document.querySelectorAll('.tab').forEach(x=>x.classList.remove('on'));
  t.classList.add('on');
  document.querySelectorAll('[data-page]').forEach(p=>
    p.classList.toggle('hide',p.dataset.page!==t.dataset.tab));
  if(t.dataset.tab==='spiel' && state && state.mode!==3) go('/api/mode?m=3');
});
document.querySelectorAll('[data-mode]').forEach(b=>
  b.onclick=()=>go('/api/mode?m='+b.dataset.mode));
const send=(id,url)=>{const e=document.getElementById(id);
  e.oninput=()=>document.getElementById(id+'Val').textContent=e.value;
  e.onchange=()=>go(url+e.value);};
send('bright','/api/bright?v=');
send('speed','/api/speed?v=');
send('sens','/api/touch?sens=');
send('thr','/api/touch?thr=');
let runde=10;
document.querySelectorAll('[data-n]').forEach(b=>b.onclick=()=>{runde=+b.dataset.n;
  document.querySelectorAll('[data-n]').forEach(x=>x.classList.remove('on'));
  b.classList.add('on');});
document.getElementById('player').oninput=function(){this.dataset.touched=1};
document.getElementById('gStart').onclick=()=>go('/api/game/start?n='+runde
  +'&name='+encodeURIComponent(document.getElementById('player').value.slice(0,12)));
document.getElementById('gStop').onclick=()=>go('/api/game/stop');
document.getElementById('gReset').onclick=()=>{
  if(confirm('Bestenliste wirklich löschen?')) go('/api/game/top/clear');};
document.getElementById('calib').onclick=()=>go('/api/touch/calib');
document.getElementById('captive').onclick=()=>
  go('/api/captive?on='+(state&&state.net.captive?'0':'1'));
document.getElementById('srcToggle').onclick=()=>
  go('/api/touch?src='+(state&&state.touch.useDelta?'status':'delta'));
document.getElementById('reboot').onclick=()=>{
  if(confirm('Wand neu starten?')) go('/api/reboot');};
document.getElementById('flip').onclick=()=>{
  flipped=!flipped; localStorage.setItem('kb_flip',flipped?'1':'0');
  document.getElementById('boardCard').classList.toggle('flip',flipped);};

buildBoard(); refresh();
if(location.hash) openTab(location.hash.slice(1));
setInterval(()=>{if(!document.hidden) refresh();},400);
</script></body></html>)HTML";

static const char UPDATE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Firmware-Update</title><style>
body{margin:0;padding:24px;background:#0b0e14;color:#e8edf5;max-width:520px;
 font-family:system-ui,sans-serif}
h1{font-size:1.2rem}p{color:#8b97a8;font-size:.88rem;line-height:1.5}
.card{background:#151a24;border:1px solid #2a3342;border-radius:16px;padding:18px}
input[type=file]{width:100%;margin:10px 0;color:#8b97a8}
button{font:inherit;color:#e8edf5;background:#1d2430;border:1px solid #2a3342;
 border-radius:12px;padding:12px 18px;min-height:46px;width:100%}
progress{width:100%;height:14px;margin-top:14px}
a{color:#4ade80}
</style></head><body>
<h1>Firmware-Update</h1>
<div class="card">
  <p>Datei <b>firmware.bin</b> aus <code>.pio/build/kilterboard/</code> wählen.
     Die Wand startet nach dem Hochladen selbst neu; Strom bitte nicht trennen.</p>
  <form id="f" method="POST" action="/update" enctype="multipart/form-data">
    <input type="file" name="firmware" accept=".bin" required>
    <button type="submit">Hochladen und neu starten</button>
  </form>
  <progress id="p" value="0" max="100" hidden></progress>
  <p id="s"></p>
</div>
<p><a href="/">&larr; zurück zur Steuerung</a></p>
<script>
const f=document.getElementById('f'),p=document.getElementById('p'),s=document.getElementById('s');
f.onsubmit=e=>{e.preventDefault();
  const x=new XMLHttpRequest(); p.hidden=false;
  x.upload.onprogress=ev=>{p.value=ev.loaded/ev.total*100;
    s.textContent='Lade '+(p.value|0)+' % ...';};
  x.onload=()=>{s.textContent=x.status===200
    ? 'Fertig. Die Wand startet neu, diese Seite in 15 s neu laden.'
    : 'Fehlgeschlagen: '+x.responseText;};
  x.onerror=()=>s.textContent='Verbindung abgebrochen.';
  x.open('POST','/update'); x.send(new FormData(f));};
</script></body></html>)HTML";
