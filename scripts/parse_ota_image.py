import struct

b = open('factory_fw/v1.2_full.bin', 'rb').read()
print('total', len(b), 'boot magic', hex(b[0]))
off = 0x8000
for i in range(12):
    e = b[off + i * 32: off + (i + 1) * 32]
    if e[:2] != b'\xAA\x50':
        print('end of table at entry', i)
        break
    ptype, sub = e[2], e[3]
    ofs, sz = struct.unpack('<I', e[4:8])[0], struct.unpack('<I', e[8:12])[0]
    name = e[12:28].rstrip(b'\x00').decode()
    print(f'{name:8s} type={ptype} sub={sub:#4x} offset={ofs:#x} '
          f'size={sz:#x} ({sz // 1024}KB)')

# app image magic at its offset (0x10000 unless table says otherwise)
app = open('factory_fw/v1.6_app.bin', 'rb').read()
print('v1.6 magic', hex(app[0]), 'len', len(app))
