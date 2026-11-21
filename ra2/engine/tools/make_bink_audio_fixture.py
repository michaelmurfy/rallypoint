"""W5-E2 (R-134): build a tiny Bink 'BIKi' file with one Bink Audio (RDFT)
track and a couple of synthetic tones, for tests/test_bink_audio.cpp. Run:

    python tools/make_bink_audio_fixture.py tests/data/bink_audio_rdft.bik [stereo]

then decode the reference with ffmpeg (the de-facto Bink Audio reference):

    ffmpeg -i tests/data/bink_audio_rdft.bik -map 0:a -f s16le tests/data/bink_audio_rdft.s16
"""
import math
import struct
import sys

CRIT = [100, 200, 300, 400, 510, 630, 770, 920, 1080, 1270, 1480, 1720, 2000,
        2320, 2700, 3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500, 24500]


class Bits:
    def __init__(self):
        self.bits = []

    def put(self, v, n):
        for i in range(n):
            self.bits.append((v >> i) & 1)

    def align8(self):
        while len(self.bits) % 8:
            self.bits.append(0)

    def align32(self):
        while len(self.bits) % 32:
            self.bits.append(0)

    def bytes(self):
        self.align8()
        out = bytearray()
        for i in range(0, len(self.bits), 8):
            b = 0
            for k in range(8):
                b |= self.bits[i + k] << k
            out.append(b)
        return bytes(out)


def block(bits, frame_len, num_bands, seed, nch=1, dct=False):
    if dct:
        bits.put(0, 2)
    for ch in range(nch):
        chan(bits, frame_len, num_bands, seed + ch * 3)
    bits.align32()   # get_bits_align32 after every block


def chan(bits, frame_len, num_bands, seed):
    # coeffs[0], coeffs[1] as get_float(): 5-bit power, 23-bit mantissa, sign
    bits.put(0, 5); bits.put(1 << 22, 23); bits.put(0, 1)   # DC = 0.5
    bits.put(0, 5); bits.put(0, 23); bits.put(0, 1)         # Nyquist = 0
    for b in range(num_bands):
        bits.put(30 + (b % 7), 8)                           # quant indices
    i = 2
    while i < frame_len:
        j = min(i + 8, frame_len)
        bits.put(0, 1)                                      # no RLE: 8 coefficients
        nonzero = ((i // 8) % 5 == seed % 5) and i < frame_len // 4
        if not nonzero:
            bits.put(0, 4)                                  # width 0: all zero
            i = j
            continue
        width = 5
        bits.put(width, 4)
        while i < j:
            c = (i * 7 + seed) % 31
            bits.put(c, width)
            if c:
                bits.put((i // 2 + seed) & 1, 1)            # sign
            i += 1


def main():
    out = sys.argv[1]
    opts = sys.argv[2:]
    stereo = 'stereo' in opts
    dct = 'dct' in opts
    rate = 22050
    channels = 2 if stereo else 1
    frame_len_bits = 10 + (1 if stereo and not dct else 0)   # 22050 Hz; RDFT stereo +1
    frame_len = 1 << frame_len_bits
    half = ((rate * channels if not dct else rate) + 1) // 2
    nb = 1
    while nb < 25 and half > CRIT[nb - 1]:
        nb += 1
    frames = []
    for f in range(6):
        b = Bits()
        b.put(0, 32)                                        # sample count (skipped)
        for k in range(2):
            block(b, frame_len, nb, f * 2 + k, channels if dct else 1, dct)
        pkt = b.bytes()
        frames.append(struct.pack('<I', len(pkt)) + pkt + b'\0' * 4)   # + a 4-byte video part
    flags = (0x2000 if stereo else 0) | (0x1000 if dct else 0)
    hdr_tracks = struct.pack('<I', 0x10000) + struct.pack('<HH', rate, flags) + struct.pack('<I', 0)
    index_start = 0x2C + 12
    data_start = index_start + 4 * (len(frames) + 1)
    offs, pos = [], data_start
    for i, fr in enumerate(frames):
        offs.append(pos | (1 if i == 0 else 0))
        pos += len(fr)
    offs.append(pos)
    body = b''.join(frames)
    total = data_start + len(body)
    largest = max(len(f) for f in frames)
    hdr = b'BIKi' + struct.pack('<IIIIIIIIII', total - 8, len(frames), largest, len(frames), 16, 16, 15, 1, 0, 1)
    with open(out, 'wb') as fp:
        fp.write(hdr + hdr_tracks + b''.join(struct.pack('<I', o) for o in offs) + body)


if __name__ == '__main__':
    main()


def mux_into_video(video_bik, audio_bik, out):
    """Copy video_bik's frames and give every frame one audio packet taken (in
    turn) from audio_bik -- a playable movie with sound for the --play check."""
    v = open(video_bik, 'rb').read()
    a = open(audio_bik, 'rb').read()
    def frames_of(b):
        n = struct.unpack_from('<I', b, 8)[0]
        nt = struct.unpack_from('<I', b, 0x28)[0]
        idx = 0x2C + 12 * nt
        offs = [struct.unpack_from('<I', b, idx + 4 * i)[0] for i in range(n + 1)]
        return nt, [(offs[i] & 1, b[offs[i] & ~1:offs[i + 1] & ~1]) for i in range(n)]
    vt, vframes = frames_of(v)
    at, aframes = frames_of(a)
    assert vt == 0 and at == 1
    tracks = a[0x2C:0x2C + 12]
    frames = []
    for i, (key, vf) in enumerate(vframes):
        af = aframes[i % len(aframes)][1]
        alen = struct.unpack_from('<I', af, 0)[0]
        frames.append((key, af[:4 + alen] + vf))
    hdr = bytearray(v[:0x2C])
    struct.pack_into('<I', hdr, 0x28, 1)
    index_start = 0x2C + 12
    data_start = index_start + 4 * (len(frames) + 1)
    offs, pos = [], data_start
    for key, fr in frames:
        offs.append(pos | key)
        pos += len(fr)
    offs.append(pos)
    struct.pack_into('<I', hdr, 4, pos - 8)
    struct.pack_into('<I', hdr, 12, max(len(f) for _, f in frames))
    with open(out, 'wb') as fp:
        fp.write(bytes(hdr) + tracks + b''.join(struct.pack('<I', o) for o in offs) + b''.join(f for _, f in frames))
