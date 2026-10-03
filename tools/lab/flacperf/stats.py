import sys, collections
sys.path.insert(0, "/Users/abel.santos/Downloads/DEV PROJECTS/Tau Alpha/tau-alpha-meter-builder/tools")
import flac_verify as fv
path = sys.argv[1]; nfr = int(sys.argv[2]); skipfr = int(sys.argv[3]) if len(sys.argv)>3 else 0
data = open(path,'rb').read()
b = fv.Bits(data); si = fv.open_stream(b); print(si['rate'], si['bps'], si['ch'], si['minb'], si['maxb'])
types = collections.Counter(); modes = collections.Counter(); params = collections.Counter(); porders=collections.Counter()
orig_res = fv.residual
def res(b_, bs, order, out, base):
    # peek method/porder
    return orig_res(b_, bs, order, out, base)
orig_bits = fv.Bits.bits
cap = si['maxb']; ch0=[0]*cap; ch1=[0]*cap
st = None; samples=0
for f in range(skipfr+nfr):
    p0 = b.p*8 - b.n
    n, m, _ = fv.frame_header(b, cap)
    for ch, buf in ((0,ch0),(1,ch1)):
        bps = si['bps'] + ((1 if m==9 else 0) if ch==0 else (1 if m in (8,10) else 0))
        # record type by peeking
        typ = (b.acc >> (b.n-7)) & 0x3F if b.n>=7 else None
        if b.n < 8: b._fill(8); typ = (b.acc >> (b.n-7)) & 0x3F
        fv.subframe(b, n, buf, bps)
        if f>=skipfr:
            t = 'LPC%d'%(typ-31) if typ>=32 else ('FIX%d'%(typ-8) if 8<=typ<=12 else 'T%d'%typ)
            types[t]+=1
    b.align(); b.bits(16)
    p1 = b.p*8 - b.n
    if f>=skipfr: modes[m]+=1; samples+=n; 
    if f==skipfr: st=p0
print('blocksize', n, 'modes', dict(modes))
print('types', sorted(types.items(), key=lambda x:-x[1]))
print('bits/stereo-sample-pair %.2f  bits/sample %.2f' % ((p1-st)/samples, (p1-st)/samples/2))
