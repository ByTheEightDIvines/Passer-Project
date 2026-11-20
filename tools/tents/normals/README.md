# Tent normal fix (2026-11-19)

Tools used to check and fix the tents' normals in PsrTent.arc (no game assets here).

- `bmdread.py`: reads positions, normals and triangles from a rigid BMD.
- `litrender.py`: renders a model lit only by its own normals (no texture), to spot bad shading.
- `fixnrm.py`: flat canvas whose normals stray > 30 deg from its face gets the face normal, on the side
  it already faced; thin pieces (ropes, poles) keep their rounded normals.
- `bmdedit.py`: appends positions/normals and rewrites the (single) shape's display list; with no changes it
  rebuilds the file byte for byte.

Applied: wall_tent (302 tris), guard_tent (40), beedle_tent (449) re-normalled; camp_flap given an inside
layer (its 7 tris duplicated 0.4 behind, normal -z), so the flaps light right when tied open.
