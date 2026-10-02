# 2D scene collision example

Mount `Assets/` as the project's asset root and load
`worlds/RainCollision2D.scn` in a 2D client. Rain chooses a world-XY landing
anchor inside the emitter rectangle, begins at visual height 2, and falls toward
the highest authored surface at that anchor. The raised roof (tag 2, height
0.8) takes precedence over the ground (tag 1, height 0). A contact removes the
drop and creates a short blue impact pulse.

The two `ParticleSurface2DComponent` rectangles are scene data; they do not
render geometry. Add Sprite or Tilemap art separately to see a roof or ground.
The component supports axis-aligned rectangles only. Tile-by-tile painting,
rotated shapes, dynamic roofs, and editor preview surface visualization remain
future work. Surface `collisionMask` is separate from rendering layers.

`collision2D.mode=2` selects top-down Ground mode. XY is the landing anchor,
and `spawnHeight`/`fallSpeed` govern a separate visual height; therefore XY
velocity and gravity must be zero. `mode=1` instead sweeps ordinary XY motion
against surfaces with `solid=true`, suitable for a side-view spark or projectile.
Both modes are CPU-only. A GPU/Auto request falls back to CPU with a diagnostic.
