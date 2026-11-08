#!/usr/bin/env python3
"""Builds build/qol_ui.mpq: the buff icon pictures (data\\global\\ui\\spells\\buffs24.dc6) used by qol_ui.dll.

The result is BH's 45 icons (read from BH's buffs.mpq) plus 7 Druid icons BH does not have, made the way BH made its own: the game's 48x48 Druid skill
icons (DrSkillIcon.dc6 in d2exp.mpq) scaled to 24x24 and quantised to the palette colours BH's icons use (palette from d2data.mpq).
Pure Python, no dependencies. Run it once; the generated file is committed in build/.

    python3 tools/make_qol_ui_mpq.py "<game folder>" build/qol_ui.mpq

The game folder must hold d2data.mpq, d2exp.mpq and BH's buffs.mpq. Frame order of the new icons (45..51) = bf_states in src/buffs.h:
Spirit of Barbs, Heart of Wolverine, Armageddon, Werewolf, Werebear, Feral Rage, Maul.
"""
import struct, zlib, sys, os
import struct,zlib,sys
LITLEN=[11,124,8,7,28,7,188,13,76,4,10,8,12,10,12,10,8,23,8,9,7,6,7,8,7,6,55,8,23,24,12,11,7,9,11,12,6,7,22,5,7,24,6,11,9,6,7,22,7,11,38,7,9,8,25,11,8,11,9,12,8,12,5,38,5,38,5,11,7,5,6,21,6,10,53,8,7,24,10,27,44,253,253,253,252,252,252,13,12,45,12,45,12,61,12,45,44,173]
LENLEN=[2,35,36,53,38,23]; DISTLEN=[2,20,53,230,247,151,248]
BASE=[3,2,4,5,6,7,8,9,10,12,16,24,40,72,136,264]; EXTRA=[0,0,0,0,0,0,0,0,1,2,3,4,5,6,7,8]
def construct(rep):
    lengths=[]
    for b in rep:
        l=b&15; run=(b>>4)+1; lengths+= [l]*run
    count=[0]*16
    for l in lengths: count[l]+=1
    symbol=[0]*len(lengths); offs=[0]*16
    for l in range(1,15): offs[l+1]=offs[l]+count[l]
    for s,l in enumerate(lengths):
        if l: symbol[offs[l]]=s; offs[l]+=1
    return count,symbol
LIT=construct(LITLEN); LEN=construct(LENLEN); DIST=construct(DISTLEN)
def explode(data):
    pos=[0]; bitbuf=[0]; bitcnt=[0]
    def bits(n):
        v=bitbuf[0]
        while bitcnt[0]<n:
            v|=data[pos[0]]<<bitcnt[0]; pos[0]+=1; bitcnt[0]+=8
        bitbuf[0]=v>>n; bitcnt[0]-=n
        return v&((1<<n)-1)
    def decode(h):
        count,symbol=h
        code=first=index=0
        for ln in range(1,14):
            code|=bits(1)^1
            c=count[ln]
            if code<first+c: return symbol[index+(code-first)]
            index+=c; first+=c; first<<=1; code<<=1
        raise Exception('bad code')
    lit=bits(8); dic=bits(8)
    out=bytearray()
    while True:
        if bits(1):
            s=decode(LEN); ln=BASE[s]+bits(EXTRA[s])
            if ln==519: break
            sh=2 if ln==2 else dic
            dist=(decode(DIST)<<sh)+bits(sh)+1
            for _ in range(ln): out.append(out[-dist])
        else:
            out.append(decode(LIT) if lit else bits(8))
    return bytes(out)

import struct, zlib
_cr=[0]*0x500
def _init():
    seed=0x00100001
    for i in range(0x100):
        idx=i
        for _ in range(5):
            seed=(seed*125+3)%0x2AAAAB; t1=(seed&0xFFFF)<<16
            seed=(seed*125+3)%0x2AAAAB; t2=seed&0xFFFF
            _cr[idx]=t1|t2; idx+=0x100
_init()
def hash_s(s,typ):
    s1=0x7FED7FED; s2=0xEEEEEEEE
    for c in s.upper():
        ch=ord(c); s1=_cr[typ*0x100+ch]^((s1+s2)&0xFFFFFFFF); s2=(ch+s1+s2+(s2<<5)+3)&0xFFFFFFFF
    return s1
def decrypt(data,key):
    out=[];s2=0xEEEEEEEE
    for i in range(len(data)//4):
        v=struct.unpack_from('<I',data,i*4)[0]
        s2=(s2+_cr[0x400+(key&0xFF)])&0xFFFFFFFF
        v^=(key+s2)&0xFFFFFFFF
        key=(((~key<<0x15)+0x11111111)|(key>>0x0B))&0xFFFFFFFF
        s2=(v+s2+(s2<<5)+3)&0xFFFFFFFF; out.append(v)
    return out
def encrypt(vals,key):
    out=[];s2=0xEEEEEEEE
    for v in vals:
        s2=(s2+_cr[0x400+(key&0xFF)])&0xFFFFFFFF
        e=v^((key+s2)&0xFFFFFFFF)
        key=(((~key<<0x15)+0x11111111)|(key>>0x0B))&0xFFFFFFFF
        s2=(v+s2+(s2<<5)+3)&0xFFFFFFFF; out.append(e)
    return out
class MPQ:
    def __init__(self,fn):
        self.f=open(fn,'rb'); f=self.f
        h=f.read(0x20)
        magic,hsize,asize,fmt,ssize=struct.unpack_from('<4sIIHH',h,0)
        self.ssize=ssize
        ht_off,bt_off,self.ht_n,self.bt_n=struct.unpack_from('<IIII',h,0x10)
        f.seek(ht_off); self.ht=decrypt(f.read(self.ht_n*16),hash_s('(hash table)',3))
        f.seek(bt_off); self.bt=decrypt(f.read(self.bt_n*16),hash_s('(block table)',3))
    def find(self,name):
        a=hash_s(name,1);b=hash_s(name,2);mask=self.ht_n-1;i=hash_s(name,0)&mask
        for _ in range(self.ht_n):
            e=self.ht[i*4:i*4+4]
            if e[0]==a and e[1]==b and e[3]<0xFFFFFFFE: return e[3]
            if e[3]==0xFFFFFFFF: return None
            i=(i+1)&mask
    def read(self,name):
        bi=self.find(name)
        if bi is None: return None
        off,csize,fsize,flags=self.bt[bi*4:bi*4+4]
        self.f.seek(off); raw=self.f.read(csize)
        if flags&0x10000: return None
        if not (flags&0x200 or flags&0x100): return raw
        if flags&0x100 and not flags&0x200:  # implode whole file
            return explode(raw)
        ss=0x200<<self.ssize; n=(fsize+ss-1)//ss
        if csize==fsize: return raw
        offs=struct.unpack_from('<%dI'%(n+1),raw,0); out=b''
        for k in range(n):
            sec=raw[offs[k]:offs[k+1]]; want=min(ss,fsize-k*ss)
            if len(sec)<want:
                m=sec[0]; body=sec[1:]
                if m==8: sec=explode(body)
                elif m==2: sec=zlib.decompress(body)
                else: raise Exception('method %x'%m)
            out+=sec
        return out

def load_pal(fn='pal.dat'):
    d=open(fn,'rb').read(); return [(d[i*3+2],d[i*3+1],d[i*3]) for i in range(256)]  # BGR
def decode_dc6(data):
    ver,flg,enc,term,dirs,fpd=struct.unpack_from('<6I',data,0)
    offs=struct.unpack_from('<%dI'%(dirs*fpd),data,24); frames=[]
    for o in offs:
        f,w,h,ox,oy,nx,nxt,ln=struct.unpack_from('<IIIiiIII',data,o)
        px=[[None]*w for _ in range(h)]; p=o+32; end=p+ln; x=0; y=h-1
        while p<end and y>=0:
            b=data[p]; p+=1
            if b==0x80: x=0; y-=1
            elif b&0x80: x+=b&0x7f
            else:
                for _ in range(b):
                    if x<w: px[y][x]=data[p]
                    p+=1; x+=1
        frames.append(dict(w=w,h=h,ox=ox,oy=oy,px=px,flip=f))
    return frames
def encode_dc6(frames):
    out=bytearray(); hdr_len=24+4*len(frames)
    bodies=[]
    for fr in frames:
        w,h,px=fr['w'],fr['h'],fr['px']; b=bytearray()
        for y in range(h-1,-1,-1):
            x=0
            while x<w:
                if px[y][x] is None:
                    s=x
                    while x<w and px[y][x] is None and x-s<127: x+=1
                    if x>=w: break          # trailing transparency needs no code
                    b.append(0x80|(x-s))
                else:
                    s=x
                    while x<w and px[y][x] is not None and x-s<127: x+=1
                    b.append(x-s); b+=bytes(px[y][s:x])
            b.append(0x80)
        bodies.append(bytes(b))
    out+=struct.pack('<6I',6,1,0,0xcdcdcdcd,1,len(frames))
    pos=hdr_len; ptrs=[]
    for fr,b in zip(frames,bodies):
        ptrs.append(pos); pos+=32+len(b)+3
    out+=struct.pack('<%dI'%len(frames),*ptrs)
    pos_end=pos
    for i,(fr,b) in enumerate(zip(frames,bodies)):
        nxt=ptrs[i+1] if i+1<len(frames) else 0
        if not nxt: nxt=pos_end      # the last frame's next-block pointer is the end of the file (as in BH's file)
        out+=struct.pack('<IIIiiIII',fr.get('flip',0),fr['w'],fr['h'],fr.get('ox',0),fr.get('oy',0),0,nxt,len(b))+b+b'\xcd\xcd\xcd'
    return bytes(out)
def png(path,w,h,rgb):   # rgb: list of rows of (r,g,b)
    raw=b''.join(b'\x00'+bytes(c for px in row for c in px) for row in rgb)
    def chunk(t,d): c=struct.pack('>I',len(d))+t+d; return c+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    open(path,'wb').write(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))


# ---- quantising the Druid skill icons ----
NEW_ICONS = [4, 10, 50, 20, 22, 54, 56]       # IconCel (Skills.txt / SkillDesc.txt) of the 7 skills, in bf_states order
def d2(a, b): return sum((x - y) ** 2 for x, y in zip(a, b))
def down(fr, pal, cand):
    w, h = fr['w'] // 2, fr['h'] // 2
    px = [[None] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            acc = [0, 0, 0]; n = 0
            for dy in range(2):
                for dx in range(2):
                    v = fr['px'][y * 2 + dy][x * 2 + dx]
                    if v is None: continue
                    c = pal[v]; acc[0] += c[0]; acc[1] += c[1]; acc[2] += c[2]; n += 1
            if n == 0: continue
            avg = tuple(a / n for a in acc)
            px[y][x] = min(cand, key=lambda i: d2(avg, pal[i]))
    return dict(w=w, h=h, ox=0, oy=0, px=px)

# ---- Meditation: BH's picture is one big bright disc that hides the stone background; shrink the disc and show the background around it ----
def shrink_disc(frame, base_frames, pal, scale=0.78, radius=10.6):
    import math
    from collections import Counter
    def isbg(v):
        r, g, b = pal[v]; return r >= b + 12 and r < 200 and (r + g + b) < 420
    tpl = [[None] * 24 for _ in range(24)]                     # the usual stone texture: most common background colour per pixel over BH's icons
    for y in range(24):
        for x in range(24):
            cnt = Counter(f['px'][y][x] for f in base_frames if f['px'][y][x] is not None and isbg(f['px'][y][x]))
            if cnt: tpl[y][x] = cnt.most_common(1)[0][0]
    out = [[None] * 24 for _ in range(24)]; c = 11.5
    for y in range(24):
        for x in range(24):
            if math.hypot(x - c, y - c) <= radius * scale + 0.4:
                sx = min(23, max(0, int(round(c + (x - c) / scale)))); sy = min(23, max(0, int(round(c + (y - c) / scale))))
                out[y][x] = frame['px'][sy][sx]
            else:
                out[y][x] = tpl[y][x] if tpl[y][x] is not None else frame['px'][y][x]
    return dict(w=24, h=24, ox=0, oy=0, px=out)

# ---- writing the MPQ (version 1, one uncompressed file per entry) ----
def write_mpq(path, files):
    n_files = len(files); ht_n = 16
    while ht_n < n_files * 2: ht_n *= 2
    blobs = []; pos = 32
    for name, data in files: blobs.append((pos, len(data), data)); pos += len(data)
    ht_off = pos; bt_off = ht_off + ht_n * 16
    ht = [0xFFFFFFFF] * (ht_n * 4)
    for bi, (name, data) in enumerate(files):
        i = hash_s(name, 0) & (ht_n - 1)
        while ht[i * 4 + 3] != 0xFFFFFFFF: i = (i + 1) & (ht_n - 1)
        ht[i * 4:i * 4 + 4] = [hash_s(name, 1), hash_s(name, 2), 0, bi]       # locale / platform 0 = neutral
    bt = []
    for off, ln, _ in blobs: bt += [off, ln, ln, 0x80000000]                   # exists, not compressed
    total = bt_off + n_files * 16
    out = bytearray(struct.pack('<4sIIHHIIII', b'MPQ\x1a', 32, total, 0, 3, ht_off, bt_off, ht_n, n_files))
    for _, _, data in blobs: out += data
    out += struct.pack('<%dI' % len(ht), *encrypt(ht, hash_s('(hash table)', 3)))
    out += struct.pack('<%dI' % len(bt), *encrypt(bt, hash_s('(block table)', 3)))
    open(path, 'wb').write(out)

def main():
    if len(sys.argv) != 3: sys.exit(__doc__)
    game, outp = sys.argv[1], sys.argv[2]
    data = MPQ(os.path.join(game, 'd2data.mpq')); exp = MPQ(os.path.join(game, 'd2exp.mpq')); bh = MPQ(os.path.join(game, 'buffs.mpq'))
    pal_raw = data.read('data\\global\\palette\\ACT1\\Pal.dat')
    pal = [(pal_raw[i * 3 + 2], pal_raw[i * 3 + 1], pal_raw[i * 3]) for i in range(256)]       # file is BGR
    base = decode_dc6(bh.read('data\\global\\ui\\spells\\buffs24.dc6'))
    assert len(base) == 45 and all(f['w'] == 24 and f['h'] == 24 for f in base), 'unexpected buffs24.dc6'
    cand = sorted({v for f in base for row in f['px'] for v in row if v is not None})
    src = decode_dc6(exp.read('data\\global\\ui\\spells\\DrSkillIcon.dc6'))
    base[34] = shrink_disc(base[34], base, pal)                                                    # frame 34 = Meditation in BH's order
    frames = base + [down(src[i], pal, cand) for i in NEW_ICONS]
    blob = encode_dc6(frames)
    back = decode_dc6(blob)
    assert len(back) == len(frames) and all(a['px'] == b['px'] for a, b in zip(frames, back)), 'DC6 round trip failed'
    write_mpq(outp, [('data\\global\\ui\\spells\\buffs24.dc6', blob)])
    chk = MPQ(outp).read('data\\global\\ui\\spells\\buffs24.dc6')
    assert chk == blob, 'MPQ round trip failed'
    print('wrote %s: %d icons (%d from BH, %d new), %d bytes' % (outp, len(frames), len(base), len(NEW_ICONS), os.path.getsize(outp)))

if __name__ == '__main__': main()
