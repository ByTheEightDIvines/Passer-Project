# Crash and file tools (2026-11-19)

Reading Dusklight crash dumps (sentry/reports/*.dmp) with real function names:

1. Stage `dusklight.exe` (Desktop\Dusklight) and extract its symbol table: the `.symdb` PE section is a symgen
   manifest (header `SYMGEN\0\0`, zstd payload at +0x48; decompress with libzstd streaming via ctypes — the
   one-shot call fails on trailing padding). Entries are {hash u64, rva u64, name_off u32, flags u32} x count,
   then the string table. Keep code entries (flags & 1) sorted by rva: `code.pkl` + `strs.bin`.
   (See the session's snippet in HANDOVER_2026-11-19-camp.md; symgen source: github.com/encounter/symgen,
   src/util/manifest.rs.)
2. `symb.py DUMP [thread|all]`: the crashing thread's stack, symbolised (needs code.pkl, strs.bin beside it).
   `mdmp.py` is the bare minidump reader it builds on; `stackvals.py DUMP` dumps the words around
   aurora push/push_verts/draw_prim (the vertex buffer's fill shows there, ~0x50142c when full).
3. `peexp.py`: the exe's export table (not useful for code addresses: exports are thunks).

Game files: `rarc.py` (read RARC/Yaz0), `rarc_replace.py` (replace one file in a RARC, others byte-for-byte),
`jpc.py` / `jpcmerge.py` (read JPAC2-10 particle packs; copy resources from one pack into ours, remapping
texture indices and de-duplicating textures), `bmg.py` (read zel_00.bmg: INF1/DAT1/FLW1 for Midna's flow).
