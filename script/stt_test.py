"""Send a WAV to GLM-ASR (Zhipu) for speech-to-text from the PC.

API: https://docs.z.ai/guides/audio/glm-asr-2512
  POST {base}/audio/transcriptions, multipart/form-data
  model=glm-asr-2512, stream=false, file=<wav>, Bearer key

The key is read from a local gitignored file - never paste it into chat
or source:  backups/glm_key.txt  (one line, the raw key)

Usage: python script/stt_test.py [wav path] [model]
Defaults: backups/mic_take1.wav  glm-asr-2512
"""
import json
import sys
import urllib.request
import uuid

WAV = sys.argv[1] if len(sys.argv) > 1 else "backups/mic_take1.wav"
MODEL = sys.argv[2] if len(sys.argv) > 2 else "glm-asr-2512"
KEY = open("backups/glm_key.txt").read().strip()

# api.z.ai first (per docs); fall back to the domestic mirror, since a
# bigmodel.cn key uses the same path on its own domain.
BASES = ["https://api.z.ai/api/paas/v4",
         "https://open.bigmodel.cn/api/paas/v4"]


def multipart(fields, file_field, filename, file_bytes, boundary):
    out = bytearray()
    for k, v in fields.items():
        out += ("--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n"
                "\r\n%s\r\n" % (boundary, k, v)).encode()
    out += ("--%s\r\nContent-Disposition: form-data; name=\"%s\"; "
            "filename=\"%s\"\r\nContent-Type: audio/wav\r\n\r\n"
            % (boundary, file_field, filename)).encode()
    out += file_bytes + ("\r\n--%s--\r\n" % boundary).encode()
    return bytes(out)


wav_bytes = open(WAV, "rb").read()
boundary = uuid.uuid4().hex
body = multipart({"model": MODEL, "stream": "false"},
                 "file", "take.wav", wav_bytes, boundary)

last_err = None
for base in BASES:
    req = urllib.request.Request(
        base + "/audio/transcriptions",
        data=body,
        headers={
            "Content-Type": "multipart/form-data; boundary=" + boundary,
            "Authorization": "Bearer " + KEY,
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            text = r.read().decode()
        print("endpoint:", base)
        try:
            parsed = json.loads(text)
            print("response:", json.dumps(parsed, ensure_ascii=False,
                                          indent=2))
        except ValueError:
            print("response:", text)
        sys.exit(0)
    except urllib.error.HTTPError as e:
        last_err = "%s -> HTTP %d: %s" % (
            base, e.code, e.read().decode()[:500])
        print(last_err)
    except Exception as e:
        last_err = "%s -> %s" % (base, e)
        print(last_err)

print("FAILED on all endpoints")
sys.exit(1)
