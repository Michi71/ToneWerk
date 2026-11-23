#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
# make_video.py - the feature video from the "[.video]" render: the editor flies in from space, captions name what plays,
# the controls that move are marked, an end card closes it. 1920 x 1080; needs Pillow, numpy and ffmpeg.
#
#   python3 demo/make_video_midi.py demo/ToneWerk-Video.mid
#   TONEWERK_VIDEO_MIDI=demo/ToneWerk-Video.mid TONEWERK_VIDEO_AUTOMATION=demo/ToneWerk-Video.automation.txt \
#       TONEWERK_VIDEO_DIR=Builds/video/frames ./Builds/Tests "[.video]"
#   python3 demo/make_video.py Builds/video/frames demo/ToneWerk-Video.cues.json Builds/video/ToneWerk.mp4
#
# A fourth argument "first:last" renders only those frames as PNGs beside the output (to look at single pictures).
import glob
import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 1920, 1080
TOP = 20                      # the editor's place: centred, the band below it holds the captions
FLY = 3.0                     # seconds the editor takes to arrive
ACCENT = (0xE8, 0x91, 0x3A)   # source/LookAndFeel.h
TEXT = (0xE9, 0xE6, 0xDF)
DIM = (0x8C, 0x8A, 0x85)


def font(size, weight):
    index = {"heavy": 8, "demi": 2, "medium": 5}[weight]
    for path, i in [("/System/Library/Fonts/Avenir Next.ttc", index), ("/System/Library/Fonts/HelveticaNeue.ttc", 0),
                    ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if weight != "medium" else
                     "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 0), ("C:/Windows/Fonts/segoeui.ttf", 0)]:
        try:
            return ImageFont.truetype(path, size, index=i)
        except OSError:
            pass
    return ImageFont.load_default(size)


def smooth(u):
    u = min(1.0, max(0.0, u))
    return u * u * (3 - 2 * u)


class Stars:
    """Stars the camera flies through: fast while the editor arrives, a slow drift afterwards."""

    def __init__(self, n=1100, seed=7):
        self.rng = np.random.default_rng(seed)
        self.xy = self.rng.uniform(-1.6, 1.6, (n, 2))
        self.z = self.rng.uniform(0.04, 1.0, n)
        self.lum = self.rng.uniform(0.35, 1.0, n) ** 2
        yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
        d = np.hypot((xx - W * 0.3) / W, (yy - H * 0.25) / H)
        e = np.hypot((xx - W * 0.8) / W, (yy - H * 0.9) / H)
        self.nebula = np.stack([10 + 26 * np.exp(-6 * e * e), 10 + 14 * np.exp(-5 * d * d),
                                16 + 44 * np.exp(-5 * d * d) + 20 * np.exp(-6 * e * e)], axis=-1).astype(np.float32)

    def frame(self, t):
        speed = 0.0007 + 0.03 * (1 - smooth(t / (FLY + 0.6))) ** 2
        z0 = self.z.copy()
        self.z -= speed
        gone = self.z < 0.04
        k = int(gone.sum())
        if k:
            self.xy[gone] = self.rng.uniform(-1.6, 1.6, (k, 2))
            self.z[gone] = 1.0
            z0[gone] = 1.0
        buf = np.zeros((H, W), np.float32)
        steps = 1 + int(speed * 400)  # a streak while it is fast
        for s in range(steps):
            z = z0 + (self.z - z0) * (s + 1) / steps
            x = W / 2 + self.xy[:, 0] / z * W * 0.3
            y = H / 2 + self.xy[:, 1] / z * W * 0.3
            lum = self.lum * (1 - z) ** 1.5 * 1.6 / steps ** 0.6
            xi, yi = np.floor(x).astype(int), np.floor(y).astype(int)
            fx, fy = x - xi, y - yi
            for dx, dy, wgt in [(0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)), (0, 1, (1 - fx) * fy), (1, 1, fx * fy)]:
                px, py = xi + dx, yi + dy
                ok = (px >= 0) & (px < W) & (py >= 0) & (py < H)
                np.add.at(buf, (py[ok], px[ok]), (lum * wgt)[ok])
        img = self.nebula + buf[..., None] * np.array([235, 240, 255], np.float32)
        return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))


def rounded_mask(size, radius):
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, size[0] - 1, size[1] - 1], radius, fill=255)
    return mask


def main(frames_dir, cues_path, out_path, only=None):
    cues = json.load(open(cues_path))
    fps = cues["fps"]
    files = sorted(glob.glob(os.path.join(frames_dir, "f*.png")))
    assert files, "no frames in " + frames_dir
    controls = {}
    for line in open(os.path.join(frames_dir, "controls.txt")):
        name, x, y, w, h = line.split()
        controls[name] = (int(x), int(y), int(w), int(h))
    ew, eh = controls["editor"][2:]
    left = (W - ew) // 2
    assert eh + TOP < H - 80, "the editor leaves no room for the captions"
    version = open(os.path.join(os.path.dirname(__file__), "..", "VERSION")).read().strip()

    mask = rounded_mask((ew, eh), 14)
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(glow).rounded_rectangle([left - 6, TOP - 6, left + ew + 6, TOP + eh + 6], 20, fill=ACCENT + (70,))
    glow = glow.filter(ImageFilter.GaussianBlur(26))
    f_title, f_text, f_brand = font(38, "demi"), font(26, "medium"), font(22, "medium")
    f_logo, f_line, f_small = font(132, "heavy"), font(36, "medium"), font(27, "medium")
    stars = Stars()
    total = len(files)
    length = total / fps
    end_card = cues["end_card"]

    first, last = (0, total - 1) if only is None else [int(v) for v in only.split(":")]
    ffmpeg = None
    if only is None:
        ffmpeg = subprocess.Popen(
            ["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(fps),
             "-i", "-", "-i", os.path.join(frames_dir, "audio.wav"), "-c:v", "libx264", "-preset", "slow", "-crf", "17",
             "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "256k", "-af",
             f"afade=t=in:d=0.05,afade=t=out:st={length - 1.2:.2f}:d=1.2", "-shortest", "-movflags", "+faststart", out_path],
            stdin=subprocess.PIPE)

    for n in range(total):
        t = n / fps
        frame = stars.frame(t)  # every frame, so the stars are where they belong in a partial render too
        if n < first or n > last:
            continue
        editor = Image.open(files[n]).convert("RGB")
        if t < FLY:
            u = t / FLY
            e = smooth(u) ** 1.3  # far away for a while, then it comes close and settles
            scale = 0.02 + 0.98 * e
            sprite = editor.convert("RGBA")
            sprite.putalpha(mask)
            sprite = sprite.resize((max(2, round(ew * scale)), max(2, round(eh * scale))), Image.LANCZOS)
            sprite = sprite.rotate(-22 * (1 - e), expand=True, resample=Image.BICUBIC)
            cx = W * 0.64 + (left + ew / 2 - W * 0.64) * e
            cy = H * 0.34 + (TOP + eh / 2 - H * 0.34) * e
            alpha = sprite.getchannel("A").point(lambda v, a=min(1.0, u * 5): int(v * a))
            frame.paste(sprite, (round(cx - sprite.width / 2), round(cy - sprite.height / 2)), alpha)
        else:
            g = smooth((t - FLY) / 0.6)
            frame.paste(glow, (0, 0), glow.getchannel("A").point(lambda v, a=g: int(v * a)))
            frame.paste(editor, (left, TOP), mask)

        over = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        draw = ImageDraw.Draw(over)
        if t >= FLY:
            for h in cues["highlights"]:
                if h["start"] <= t <= h["end"] and h["id"] in controls:
                    a = min(1.0, (t - h["start"]) / 0.2, (h["end"] - t) / 0.4)
                    x, y, w, hh = controls[h["id"]]
                    box = [left + x - 8, TOP + y - 8, left + x + w + 8, TOP + y + hh + 8]
                    draw.rounded_rectangle([box[0] - 4, box[1] - 4, box[2] + 4, box[3] + 4], 14, outline=ACCENT + (int(70 * a),), width=6)
                    draw.rounded_rectangle(box, 10, outline=ACCENT + (int(255 * a),), width=3)
            brand = f"ToneWerk {version}"
            a = smooth((t - FLY) / 0.6)
            draw.text((left + ew - draw.textlength(brand, font=f_brand), H - 54), brand, font=f_brand, fill=DIM + (int(255 * a),))
        for c in cues["captions"]:
            if c["start"] <= t <= c["end"]:
                a = smooth(min((t - c["start"]) / 0.3, (c["end"] - t) / 0.3))
                x = left + 24 + round((1 - a) * 22)
                y = TOP + eh + 14
                draw.rectangle([x - 24, y + 6, x - 19, y + 84], fill=ACCENT + (int(255 * a),))
                draw.text((x, y), c["title"], font=f_title, fill=ACCENT + (int(255 * a),))
                draw.text((x, y + 50), c["text"], font=f_text, fill=TEXT + (int(255 * a),))
        if t >= end_card:
            a = smooth((t - end_card) / 0.8)
            draw.rectangle([0, 0, W, H], fill=(8, 8, 12, int(205 * a)))
            w1, w2 = draw.textlength("Tone", font=f_logo), draw.textlength("Werk", font=f_logo)
            x0 = (W - w1 - w2) / 2
            draw.text((x0, 330), "Tone", font=f_logo, fill=TEXT + (int(255 * a),))
            draw.text((x0 + w1, 330), "Werk", font=f_logo, fill=ACCENT + (int(255 * a),))
            for y, text, f, colour in [(530, "Electric and acoustic keyboards with an MK8 effect chain", f_line, TEXT),
                                       (610, "Rhodes  ·  Wurlitzer  ·  Pianet  ·  Clavinet  ·  grand and upright pianos", f_small, DIM),
                                       (660, "VST3  ·  AU  ·  CLAP  ·  Standalone        macOS  ·  Linux  ·  Windows", f_small, DIM)]:
                draw.text(((W - draw.textlength(text, font=f)) / 2, y), text, font=f, fill=colour + (int(255 * a),))
        frame.paste(over, (0, 0), over)

        fade = min(1.0, t / 0.5, (length - t) / 0.9)
        if fade < 1.0:
            frame = Image.eval(frame, lambda v, a=max(0.0, fade): int(v * a))
        if ffmpeg is not None:
            ffmpeg.stdin.write(frame.tobytes())
        else:
            frame.save(f"{os.path.splitext(out_path)[0]}-{n:05d}.png")
        if n % 150 == 0:
            print(f"frame {n} of {total}", flush=True)
    if ffmpeg is not None:
        ffmpeg.stdin.close()
        if ffmpeg.wait() != 0:
            sys.exit("ffmpeg failed")
        print(f"{out_path}: {length:.1f} s, {total} frames")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit("usage: make_video.py <frames dir> <cues.json> <out.mp4> [first:last]")
    main(*sys.argv[1:5])
