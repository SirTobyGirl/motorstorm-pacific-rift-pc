#!/usr/bin/env python3
"""List or extract files from a PSARC archive (the game's ms2.psarc), for reading the game's own data files.

  work/tools/psarc.py list <archive> [substring]
  work/tools/psarc.py cat <archive> </path/inside> > file
  work/tools/psarc.py extract <archive> </path/inside> <outfile>

Reads your own copy of the archive; extracted files are the game's data, so keep them out of the repository (the
pre-commit check refuses them). Format: 32-byte header (big-endian), a table of contents (MD5, first block index,
5-byte size, 5-byte offset per file), a table of block sizes, then blocks of at most `block size` bytes, each stored
as is when its size equals what it holds, else zlib-compressed. Entry 0 is the list of names, one per line.
"""
import struct, sys, zlib


class Psarc:
    def __init__(self, path):
        self.f = open(path, 'rb')
        h = self.f.read(32)
        if h[:4] != b'PSAR':
            raise ValueError('not a PSARC archive')
        self.comp = h[8:12]
        if self.comp != b'zlib':
            raise ValueError('compression %r not supported (zlib only)' % self.comp)
        toc_len, ent_size, n_ent, self.bsz, self.flags = struct.unpack('>IIIII', h[12:32])
        toc = self.f.read(toc_len - 32)
        self.ents = []
        for i in range(n_ent):
            e = toc[i * ent_size:(i + 1) * ent_size]
            self.ents.append((struct.unpack('>I', e[16:20])[0], int.from_bytes(e[20:25], 'big'), int.from_bytes(e[25:30], 'big')))
        w = 2 if self.bsz <= 65536 else 3 if self.bsz <= 16777216 else 4
        tbl = toc[n_ent * ent_size:]
        self.blocks = [int.from_bytes(tbl[i:i + w], 'big') for i in range(0, len(tbl) - w + 1, w)]
        self.names = self._entry(0).decode('utf-8', 'replace').split('\n')
        self.index = {n: i + 1 for i, n in enumerate(self.names)}

    def _entry(self, i):
        bi, size, off = self.ents[i]
        self.f.seek(off)
        out = bytearray()
        while len(out) < size:
            held = min(self.bsz, size - len(out))
            n = self.blocks[bi] or self.bsz
            bi += 1
            raw = self.f.read(n)
            out += raw if n == held else zlib.decompress(raw)
        return bytes(out[:size])

    def read(self, name):
        return self._entry(self.index[name])


def main(argv):
    if len(argv) < 3 or argv[1] not in ('list', 'cat', 'extract'):
        print(__doc__)
        return 2
    p = Psarc(argv[2])
    if argv[1] == 'list':
        for n in p.names:
            if n and (len(argv) < 4 or argv[3] in n):
                print(n)
    elif argv[1] == 'cat':
        sys.stdout.buffer.write(p.read(argv[3]))
    else:
        open(argv[4], 'wb').write(p.read(argv[3]))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
