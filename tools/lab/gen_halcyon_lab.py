#!/usr/bin/env python3
"""Writes tools/lab/halcyon_lab.html: the Halcyon tuning lab (parallel plan A5, B-632). One self-contained page: six control sliders and the eight default presets drive the SAME integer
macro mapping and the SAME quantised Q2.22 coefficient rows the firmware core uses (fw/halcyon_core.h, generated tables), the page draws the composite magnitude (and each stage), shows
the stage gains, the peak boost and the peak-safe preamp exactly as the firmware would compute them, and dumps the six coefficient rows. Host-only: nothing here talks to a Pocket.
    python3 tools/lab/gen_halcyon_lab.py            (writes the page)      --check   (fails if the page is stale)"""
import json, os, sys
os.environ["EQ_COEF_BITS"] = "24"
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools" / "lab"))
import halcyon_model as m
import gen_halcyon_tab as tab

TEMPLATE = r"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Halcyon Lab</title>
<style>
:root{--bg:#f6f6f4;--fg:#1c1c1a;--mut:#6b6b66;--line:#d6d6d0;--acc:#1f6feb;--st:#b0b0a8;--card:#fff}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#17181a;--fg:#e8e8e4;--mut:#9a9a94;--line:#34363a;--acc:#6aa5ff;--st:#55585e;--card:#202225}}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,sans-serif}
main{max-width:980px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:0 0 4px}p.sub{color:var(--mut);margin:0 0 14px}
.grid{display:grid;grid-template-columns:1fr;gap:14px}@media(min-width:820px){.grid{grid-template-columns:300px 1fr}}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px}
label{display:flex;justify-content:space-between;font-size:13px;margin-top:8px}input[type=range]{width:100%}
button{font:inherit;border:1px solid var(--line);background:var(--card);color:var(--fg);border-radius:8px;padding:5px 9px;margin:2px;cursor:pointer}
button.on{border-color:var(--acc);color:var(--acc)}
canvas{width:100%;height:320px;display:block}
table{border-collapse:collapse;width:100%;font:12px ui-monospace,monospace}td,th{border-bottom:1px solid var(--line);padding:3px 6px;text-align:right}th:first-child,td:first-child{text-align:left}
.big{font-size:22px;font-weight:600}.mut{color:var(--mut)}
</style></head><body><main>
<h1>Halcyon Lab</h1><p class="sub">Six controls, six biquad stages, peak-safe preamp: the same numbers the firmware core produces (Q2.22 coefficients, 48 kHz).</p>
<div class="grid"><div class="card" id="ctl"></div><div>
<div class="card"><canvas id="cv" width="900" height="320"></canvas>
<div style="display:flex;gap:20px;flex-wrap:wrap;margin-top:6px"><div><div class="mut">peak boost</div><div class="big" id="pk">-</div></div><div><div class="mut">preamp (attenuate only)</div><div class="big" id="pre">-</div></div><div><div class="mut">overall at 1 kHz</div><div class="big" id="k1">-</div></div></div></div>
<div class="card" style="margin-top:14px"><table id="tb"></table></div></div></div>
</main>
<script>
const D=__DATA__;
const FS=48000,QS=Math.pow(2,22),NG=D.ngrid;
const MACROS=["warmth","bass","vocal","punch","sibilance","air"];
const rdiv=(n,d)=>n>=0?Math.floor((n+d/2)/d):-Math.floor((-n+d/2)/d);
const cl=(v,a,b)=>v<a?a:v>b?b:v;
const state={warmth:0,bass:0,vocal:0,punch:0,sibilance:0,air:0};
function steps(c){const w=cl(c.warmth,-5,5),b=cl(c.bass,-5,5),v=cl(c.vocal,-5,5),p=cl(c.punch,-5,5),s=cl(c.sibilance,0,5),a=cl(c.air,-5,5);
 const h=[2*b+w,rdiv(16*w,10),rdiv(18*v,10),rdiv(18*p,10),-2*s,rdiv(18*a-10*w,10)];return h.map(x=>cl(x,-18,18)+18);}
function respDb(co,f){const w=2*Math.PI*f/FS,c1=Math.cos(w),s1=Math.sin(w),c2=Math.cos(2*w),s2=Math.sin(2*w);
 const b0=co[0]/QS,b1=co[1]/QS,b2=co[2]/QS,a1=co[3]/QS,a2=co[4]/QS;
 const nr=b0+b1*c1+b2*c2,ni=-(b1*s1+b2*s2),dr=1+a1*c1+a2*c2,di=-(a1*s1+a2*s2);
 return 10*Math.log10((nr*nr+ni*ni)/(dr*dr+di*di));}
function compute(){const st=steps(state);const f=[];for(let i=0;i<=300;i++)f.push(20*Math.pow(1000,i/300));
 const per=st.map((s,i)=>f.map(x=>respDb(D.coef[i][s],x)));const tot=f.map((_,k)=>per.reduce((a,r)=>a+r[k],0));
 // firmware path: add the table rows on the 40-point grid, round the peak up to 1/8 dB, add the 0.25 dB margin
 let pk64=-32768;for(let k=0;k<NG;k++){let s=0;for(let i=0;i<6;i++)s+=D.mag[i][st[i]][k];if(s>pk64)pk64=s;}
 const eighths=pk64<=0?0:Math.min(512,Math.floor((pk64+7)/8)+2);
 return {st,f,per,tot,pk64,eighths};}
function draw(r){const cv=document.getElementById("cv"),g=cv.getContext("2d"),W=cv.width,H=cv.height;const css=getComputedStyle(document.documentElement);
 g.clearRect(0,0,W,H);const L=44,R=10,T=10,B=24,lo=-14,hi=14;const X=hz=>L+(Math.log10(hz/20)/3)*(W-L-R),Y=db=>T+(hi-db)/(hi-lo)*(H-T-B);
 g.strokeStyle=css.getPropertyValue("--line");g.fillStyle=css.getPropertyValue("--mut");g.font="12px system-ui";
 for(const db of [-12,-6,0,6,12]){g.beginPath();g.moveTo(L,Y(db));g.lineTo(W-R,Y(db));g.stroke();g.fillText(db+" dB",4,Y(db)+4);}
 for(const hz of [20,100,1000,10000,20000]){g.beginPath();g.moveTo(X(hz),T);g.lineTo(X(hz),H-B);g.stroke();g.fillText(hz>=1000?(hz/1000)+"k":hz,X(hz)-8,H-6);}
 g.strokeStyle=css.getPropertyValue("--st");g.lineWidth=1;for(const row of r.per){g.beginPath();row.forEach((v,i)=>{const x=X(r.f[i]),y=Y(cl(v,lo,hi));i?g.lineTo(x,y):g.moveTo(x,y)});g.stroke();}
 g.strokeStyle=css.getPropertyValue("--acc");g.lineWidth=2.5;g.beginPath();r.tot.forEach((v,i)=>{const x=X(r.f[i]),y=Y(cl(v,lo,hi));i?g.lineTo(x,y):g.moveTo(x,y)});g.stroke();}
function render(){const r=compute();draw(r);document.getElementById("pk").textContent=(r.pk64/64).toFixed(2)+" dB";
 document.getElementById("pre").textContent="-"+(r.eighths/8).toFixed(2)+" dB";
 const k1=r.tot[Math.round(300*Math.log10(1000/20)/3)];document.getElementById("k1").textContent=(k1-r.eighths/8).toFixed(2)+" dB";
 const tb=document.getElementById("tb");let h="<tr><th>stage</th><th>gain</th><th>b0</th><th>b1</th><th>b2</th><th>a1</th><th>a2</th></tr>";
 r.st.forEach((s,i)=>{const c=D.coef[i][s];h+="<tr><td>"+D.names[i]+" "+D.f0[i]+" Hz</td><td>"+((s-18)/2).toFixed(1)+" dB</td>"+c.map(v=>"<td>"+v+"</td>").join("")+"</tr>";});tb.innerHTML=h;}
function build(){const el=document.getElementById("ctl");let h="<div>";D.presets.forEach((p,i)=>{h+="<button data-p='"+i+"'>"+p.name+"</button>"});h+="</div>";
 MACROS.forEach(k=>{const lo=k==="sibilance"?0:-5;h+="<label><span>"+k+"</span><span id='v_"+k+"'>0</span></label><input type='range' id='r_"+k+"' min='"+lo+"' max='5' step='1' value='0'>"});
 el.innerHTML=h;MACROS.forEach(k=>document.getElementById("r_"+k).oninput=e=>{state[k]=+e.target.value;sync(false)});
 el.querySelectorAll("button").forEach(b=>b.onclick=()=>{const p=D.presets[+b.dataset.p];MACROS.forEach(k=>state[k]=p.controls[k]||0);sync(true)});}
function sync(setSliders){MACROS.forEach(k=>{document.getElementById("v_"+k).textContent=state[k];if(setSliders)document.getElementById("r_"+k).value=state[k]});render();}
build();sync(true);addEventListener("resize",render);
</script></body></html>
"""


def render():
    coef, mag, _pre = tab.build()
    data = {"ngrid": tab.NGRID, "names": [s[0] for s in m.STAGES], "f0": [s[2] for s in m.STAGES], "coef": coef, "mag": mag,
            "presets": [{"name": n, "controls": c} for n, c, _why in m.PRESETS]}
    return TEMPLATE.replace("__DATA__", json.dumps(data, separators=(",", ":")))


if __name__ == "__main__":
    out = ROOT / "tools" / "lab" / "halcyon_lab.html"
    text = render()
    if "--check" in sys.argv:
        sys.exit(0 if out.exists() and out.read_text() == text else "tools/lab/halcyon_lab.html is stale: run tools/lab/gen_halcyon_lab.py")
    out.write_text(text)
    print(f"wrote {out} ({len(text)} bytes)")
