import sys, struct
sys.path.insert(0, "/Users/abel.santos/Downloads/DEV PROJECTS/Tau Alpha/tau-alpha-meter-builder/tools")
import flac_verify as fv
path, skip, nfr, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
data = open(path,'rb').read(); b = fv.Bits(data); si = fv.open_stream(b); cap = si['maxb']
ch0=[0]*cap; ch1=[0]*cap
def pos(): return (b.p*8 - b.n)//8
for f in range(skip+nfr+1):
    if f == skip: start = pos()
    if f == skip+nfr: end = pos(); break
    n, m, _ = fv.frame_header(b, cap)
    fv.subframe(b, n, ch0, si['bps'] + (1 if m==9 else 0))
    fv.subframe(b, n, ch1, si['bps'] + (1 if m in (8,10) else 0))
    b.align(); b.bits(16)
open(out,'wb').write(struct.pack('<4I', si['ch'], si['bps'], cap, nfr) + data[start:end] + b'\0'*64)
print(out, si, start, end)
