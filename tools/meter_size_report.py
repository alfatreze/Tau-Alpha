# Per-meter size report from objdump -t output (name-prefix grouping). Usage: python3 tools/meter_size_report.py <name> (reads <name>.sym)
import re,sys,collections
pat={'layered_wave':r'^(lw_|layered_wave|LW_|lwv)','chladni':r'^(chl|chladni)','winamp':r'^(wviz|wvcfg|winamp)','vu_master':r'^(vum_|vu_master)','fullscreen':r'^(fs_|fullscreen)','waterfall/wave/etc':r'^(wf_|wave_|spec_|dots_|led_)','meter_infra':r'^(mtr_|meter_|viz_|helios_meter)','scope_hw':r'^(scope|hwwave)'}
for t in sys.argv[1:]:
    agg=collections.defaultdict(lambda: collections.Counter())
    for l in open(t+'.sym'):
        p=l.split()
        if len(p)<5 or not p[0].isalnum(): continue
        sec=p[3] if p[3].startswith('.') else None
        if not sec: continue
        sz=int(p[4],16); name=p[-1]
        if not sz: continue
        for k,r in pat.items():
            if re.match(r,name): agg[k][sec]+=sz; break
    print('==',t)
    for k,c in sorted(agg.items()): print(f'{k:20}',dict(c))
