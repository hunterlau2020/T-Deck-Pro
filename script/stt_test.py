"""Send a WAV to GLM (Zhipu) for speech-to-text from the PC.

The key is read from a local gitignored file - never paste it into chat
or source:  backups/glm_key.txt  (one line, the raw key)

Usage: python script/stt_test.py [wav path] [model]
Defaults: backups/mic_take1.wav  glm-4-voice
"""
import base64
import json
import sys
import urllib.request

WAV = sys.argv[1] if len(sys.argv) > 1 else "backups/mic_take1.wav"
MODEL = sys.argv[2] if len(sys.argv) > 2 else "glm-4-voice"
KEY = open("backups/glm_key.txt").read().strip()

audio_b64 = base64.b64encode(open(WAV, "rb").read()).decode()

payload = {
    "model": MODEL,
    "messages": [
        {
            "role": "user",
            "content": [
                {
                    "type": "text",
                    "text": "请逐字转写这段音频里的英文内容，只输出转写文本。",
                },
                {
                    "type": "input_audio",
                    "input_audio": {"data": audio_b64, "format": "wav"},
                },
            ],
        }
    ],
}

req = urllib.request.Request(
    "https://open.bigmodel.cn/api/paas/v4/chat/completions",
    data=json.dumps(payload).encode(),
    headers={
        "Content-Type": "application/json",
        "Authorization": "Bearer " + KEY,
    },
)

try:
    with urllib.request.urlopen(req, timeout=120) as r:
        body = json.loads(r.read())
except urllib.error.HTTPError as e:
    print("HTTP %d:" % e.code)
    print(e.read().decode()[:2000])
    sys.exit(1)

msg = body["choices"][0]["message"]
content = msg.get("content")
if isinstance(content, list):          # some voice models return parts
    content = "".join(
        p.get("text", "") for p in content if isinstance(p, dict)
    )
print("model :", body.get("model"))
print("usage :", body.get("usage"))
print("transcript:")
print(content)
