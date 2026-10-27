"""Generate assets/audio/telegraphs/<shape>-<ms>.wav: the five telegraph words spoken by Zira (SAPI "Microsoft Zira
Desktop", the same voice the positional speech uses in-game), silence-trimmed, time-compressed with ffmpeg's atempo
(pitch preserved) to 200 ms (the 100 ms set was dropped 2026-09-01: too clipped to read), loudness-matched to the sonar's
enemy cue, 48 kHz mono 16-bit.
Usage: uv run tools/gen_telegraph_cues.py [--ms 200] [--words swing,stomp,wave,shot,ring,area,charge,aura,beam,frontal]"""
import os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "assets", "audio", "telegraphs")
VOICE = "Microsoft Zira Desktop"
TARGET_LKFS = -13.9   # assets/audio/interactables/units-enemy.wav by tools/loudness.py (the sonar's reference cue)
sys.path.insert(0, HERE)
import loudness as _ld
def k_lkfs(path): return _ld.k_loudness(_ld.load_mono_48k(path))

def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0: raise SystemExit(f"{cmd[0]} failed: {r.stderr[-800:]}")
    return r

def synth(word, path):
    ps = ("Add-Type -AssemblyName System.Speech; $s = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
          f"$s.SelectVoice('{VOICE}'); $s.Rate = 0; $s.SetOutputToWaveFile('{path}'); $s.Speak('{word}'); $s.Dispose()")
    run(["powershell", "-NoProfile", "-Command", ps])

def duration(path):
    # decode and count samples (ffprobe reports N/A on silenceremove's streamed output)
    r = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "s16le", "-ac", "1", "-ar", "48000", "-"], capture_output=True)
    if r.returncode != 0: raise SystemExit(r.stderr[-800:])
    return len(r.stdout) / 2 / 48000.0

def peak_db(path):
    r = subprocess.run(["ffmpeg", "-hide_banner", "-i", path, "-af", "volumedetect", "-f", "null", "-"], capture_output=True, text=True)
    m = re.search(r"max_volume: (-?[\d.]+) dB", r.stderr)
    return float(m.group(1)) if m else 0.0

def main():
    ms_list = [200]; words = ["swing", "stomp", "wave", "shot", "ring", "area", "charge", "aura", "beam", "frontal"]
    a = sys.argv[1:]
    if "--ms" in a: ms_list = [int(x) for x in a[a.index("--ms") + 1].split(",")]
    if "--words" in a: words = a[a.index("--words") + 1].split(",")
    os.makedirs(OUT, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="telegraph_")
    for w in words:
        raw = os.path.join(tmp, f"{w}-raw.wav"); trimmed = os.path.join(tmp, f"{w}-trim.wav")
        synth(w, raw)
        run(["ffmpeg", "-y", "-hide_banner", "-i", raw, "-af",
             "silenceremove=start_periods=1:start_threshold=-45dB,areverse,silenceremove=start_periods=1:start_threshold=-45dB,areverse", "-ar", "48000", "-ac", "1", trimmed])
        d = duration(trimmed)
        for ms in ms_list:
            factor = d / (ms / 1000.0)
            fast = os.path.join(tmp, f"{w}-{ms}-fast.wav")
            run(["ffmpeg", "-y", "-hide_banner", "-i", trimmed, "-af", f"atempo={factor:.4f}", fast])
            # Loudness-match to the sonar's enemy cue (K-weighted, tools/loudness.py); a limiter absorbs the overs.
            gain = TARGET_LKFS - k_lkfs(fast)
            out = os.path.join(OUT, f"{w}-{ms}.wav")
            run(["ffmpeg", "-y", "-hide_banner", "-i", fast, "-af", f"volume={gain:.2f}dB,alimiter=limit=0.94:attack=1:release=20:level=false",
                 "-ar", "48000", "-ac", "1", "-c:a", "pcm_s16le", out])
            print(f"{w:6s} {ms} ms: word {d:.3f}s x{factor:.1f} -> {duration(out):.3f}s, {gain:+.1f} dB -> {k_lkfs(out):.1f} LKFS (target {TARGET_LKFS}) peak {peak_db(out):.1f} dBFS")

if __name__ == "__main__":
    main()
