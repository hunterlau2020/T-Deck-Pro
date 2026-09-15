b = open('spiffs_v11.bin', 'rb').read()
for m in [b'OPENROUTER_KEY', b'MINIMAX_AUDIO_KEY', b'PENPAL_BASE',
          b'OWM_KEY', b'env.cfg']:
    i = b.find(m)
    if i < 0:
        print(m.decode(), 'MISSING')
    else:
        seg = b[i:i + 140].split(b'\x00')[0].split(b'\n')[0]
        print(m.decode(), '@', hex(i), '->', seg[:70].decode('ascii', 'replace'))
