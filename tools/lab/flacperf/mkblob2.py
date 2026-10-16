import sys, struct
import pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))
import flac_verify as fv
# like mkblob.py, but the blob carries a minimal file header for a whole-file decoder: u32 ch,bps,maxb,nframes, u32 hdrlen, u32 pairs, hdr(fLaC+STREAMINFO), frames
path, skip, nfr, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
data = open(path,'rb').read(); b = fv.Bits(data); si = fv.open_stream(b); cap = si['maxb']
assert data[:4]==b'fLaC' and (data[4]&0x7f)==0 and data[5:8]==b'\x00\x00\x22'
sinfo = bytearray(data[8:42])
ch0=[0]*cap; ch1=[0]*cap
def pos(): return (b.p*8 - b.n)//8
pairs=0
for f in range(skip+nfr+1):
    if f == skip: start = pos(); pairs=0
    if f == skip+nfr: end = pos(); break
    n, m, _ = fv.frame_header(b, cap)
    if f >= skip: pairs += n
    fv.subframe(b, n, ch0, si['bps'] + (1 if m==9 else 0))
    fv.subframe(b, n, ch1, si['bps'] + (1 if m in (8,10) else 0))
    b.align(); b.bits(16)
sinfo[13] &= 0xF0; sinfo[14:18] = b'\0\0\0\0'; sinfo[18:34] = b'\0'*16     # total samples and md5 unknown
hdr = b'fLaC' + bytes([0x80,0,0,34]) + bytes(sinfo)
open(out,'wb').write(struct.pack('<6I', si['ch'], si['bps'], cap, nfr, len(hdr), pairs) + hdr + data[start:end] + b'\0'*64)
print(out, pairs, len(hdr), end-start)
