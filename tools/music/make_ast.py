"""A game music stream (.ast) from a 16-bit WAV, as Dusklight's stream player reads it
(libs/JSystem JASAramStream: headerLoad / load): a 0x40-byte 'STRM' header, then 'BLCK' blocks, each
a 0x20-byte header and 0x2760 bytes per channel (the last block shorter), samples big-endian 16-bit
(format 1, PCM16), looping the whole track.
Usage: python3 -I make_ast.py IN.wav OUT.ast"""
import sys, struct, wave
import numpy as np
BLOCK = 0x2760                     # JAUInitializer aramBlockSize_: the stream block size the game is set up with
src, dst = sys.argv[1:3]
w = wave.open(src)
assert w.getsampwidth() == 2, 'needs 16-bit'
ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
assert 1 <= ch <= 2, 'the game streams at most two channels'
pcm = np.frombuffer(w.readframes(n), '<i2').reshape(-1, ch)
blocks = bytearray()
for start in range(0, n, BLOCK // 2):
    part = pcm[start:start + BLOCK // 2]
    size = part.shape[0] * 2
    size += -size % 32                                # each channel's run padded to 32 bytes
    blocks += struct.pack('>4sI', b'BLCK', size) + bytes(0x18)   # no ADPCM history: PCM
    for c in range(ch):
        run = part[:, c].astype('>i2').tobytes()
        blocks += run + bytes(size - len(run))
head = bytearray(0x40)
struct.pack_into('>4sIHHHHiIiiI', head, 0, b'STRM', len(blocks), 1, 16, ch, 0xFFFF, rate, n, 0, n, BLOCK)
head[0x28] = 0x7F                                     # full volume
open(dst, 'wb').write(bytes(head) + bytes(blocks))
print(dst, ch, 'ch', rate, 'Hz', n, 'samples', (len(head) + len(blocks)) // 1024, 'KB')
