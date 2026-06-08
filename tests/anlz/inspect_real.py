import struct

def be16(d,o): return struct.unpack_from('>H',d,o)[0]
def be32(d,o): return struct.unpack_from('>I',d,o)[0]
def hexrow(d,off,n=16):
    row=d[off:off+n]
    return ' '.join(f'{b:02X}' for b in row)+'  '+''.join(chr(b) if 32<=b<127 else '.' for b in row)

DAT = 'F:/PIONEER/USBANLZ/P000/00000832/ANLZ0000.DAT'
EXT = 'F:/PIONEER/USBANLZ/P000/00000832/ANLZ0000.EXT'
EX2 = 'F:/PIONEER/USBANLZ/P000/00000832/ANLZ0000.2EX'

data = open(DAT,'rb').read()
print(f'=== DAT: {len(data)} bytes ===')

# Sequential tag scan
offset = 0
segments = []
while offset + 12 <= len(data):
    tag     = data[offset:offset+4]
    tag_str = tag.decode('ascii','replace')
    hdr     = be32(data, offset+4)
    seg     = be32(data, offset+8)
    segments.append((offset, tag_str, hdr, seg))
    if tag_str == 'PMAI':
        offset += hdr
    else:
        if seg < 12 or offset + seg > len(data):
            break
        offset += seg

print('Tags found:')
for (off, t, h, s) in segments:
    print(f'  0x{off:04X}  {t}  hdr={h}  seg={s}  data={s-h}B')

# PPTH
print()
off,_,hdr,seg = next((x for x in segments if x[1]=='PPTH'), (None,None,None,None))
if off is not None:
    print(f'=== PPTH hdr={hdr} seg={seg} ===')
    print(f'  full header: {hexrow(data, off, hdr)}')
    path_bytes = data[off+hdr : off+seg]
    path = path_bytes.decode('utf-16-be').rstrip('\x00').replace('\\','/')
    print(f'  path: {path}')

# PVBR
off,_,hdr,seg = next((x for x in segments if x[1]=='PVBR'), (None,None,None,None))
if off is not None:
    print()
    print(f'=== PVBR hdr={hdr} seg={seg} ===')
    print(f'  full header: {hexrow(data, off, hdr)}')
    count = (seg-hdr)//4
    offs  = [be32(data, off+hdr+i*4) for i in range(count)]
    print(f'  entries: {count}')
    print(f'  first 4: {offs[:4]}')
    print(f'  last  4: {offs[-4:]}')

# PQTZ
off,_,hdr,seg = next((x for x in segments if x[1]=='PQTZ'), (None,None,None,None))
if off is not None:
    print()
    print(f'=== PQTZ hdr={hdr} seg={seg} ===')
    print(f'  full header (24B): {hexrow(data, off, hdr)}')
    count = (seg-hdr)//8
    print(f'  beat entries: {count}')
    for i in range(min(3, count)):
        o = off+hdr+i*8
        phase,bpm100,tms = be16(data,o),be16(data,o+2),be32(data,o+4)
        print(f'  beat[{i}]: phase={phase}  bpm_x100={bpm100} ({bpm100/100:.2f} BPM)  time_ms={tms}')
    if count > 3:
        o = off+hdr+(count-1)*8
        phase,bpm100,tms = be16(data,o),be16(data,o+2),be32(data,o+4)
        m,s = divmod(tms//1000,60)
        print(f'  beat[{count-1}]: phase={phase}  bpm_x100={bpm100}  time_ms={tms}  ({m}:{s:02d})')

# PWAV
off,_,hdr,seg = next((x for x in segments if x[1]=='PWAV'), (None,None,None,None))
if off is not None:
    print()
    print(f'=== PWAV hdr={hdr} seg={seg} ===')
    print(f'  full header: {hexrow(data, off, hdr)}')
    wav    = data[off+hdr : off+seg]
    heights = [b&0x1F for b in wav]
    print(f'  bytes: {len(wav)}  max_height={max(heights)}')
    print(f'  heights[0:16]: {heights[:16]}')

# PCOB
pcobs = [x for x in segments if x[1]=='PCOB']
print()
print(f'=== PCOB ({len(pcobs)} entries) ===')
for (off,_,hdr,seg) in pcobs:
    data_len = seg-hdr
    print(f'  0x{off:04X}: hdr={hdr} seg={seg} data={data_len}B')
    print(f'  header: {hexrow(data, off, hdr)}')
    if data_len >= 56:
        cue_count = data_len // 56
        for i in range(cue_count):
            o = off+hdr+i*56
            etype,idx = data[o],data[o+1]
            start,end = be32(data,o+4),be32(data,o+8)
            print(f'    cue[{i}]: type={etype} idx={idx} start={start}ms end={end}ms')

# EXT
print()
print(f'=== EXT: {EXT} ===')
edata = open(EXT,'rb').read()
print(f'  size: {len(edata)} bytes')
off2 = 0
while off2 + 12 <= len(edata):
    tag     = edata[off2:off2+4].decode('ascii','replace')
    hdr     = be32(edata, off2+4)
    seg     = be32(edata, off2+8)
    print(f'  0x{off2:04X}  {tag}  hdr={hdr}  seg={seg}  data={seg-hdr}B')
    if tag == 'PMAI':
        off2 += hdr
    else:
        if seg < 12 or off2 + seg > len(edata):
            break
        off2 += seg

# 2EX
import os
if os.path.exists(EX2):
    print()
    print(f'=== 2EX: {EX2} ===')
    edata2 = open(EX2,'rb').read()
    print(f'  size: {len(edata2)} bytes')
    off2 = 0
    while off2 + 12 <= len(edata2):
        tag     = edata2[off2:off2+4].decode('ascii','replace')
        hdr     = be32(edata2, off2+4)
        seg     = be32(edata2, off2+8)
        print(f'  0x{off2:04X}  {tag}  hdr={hdr}  seg={seg}  data={seg-hdr}B')
        if tag == 'PMAI':
            off2 += hdr
        else:
            if seg < 12 or off2 + seg > len(edata2):
                break
            off2 += seg
