# Rendering details

For renderer selection and performance guidance, see the [README](../README.md#rendering).
This guide describes the Native renderer's effects and implementation on current main.
Native CPU vertex/normal preparation is covered separately in [Native preparation](NATIVE_PREPARATION.md).

World tints (the red alarm lighting, GameWorld::setTintOverride) reach the renderer as GL lighting
with no lights: scene ambient x vertex colour. The native shader implements that ambient-only case.

## Native enhancements

- **Texture filtering** (`anisotropy`, default 16): world (depth-tested) draws sample through the
  port's own sampler objects: trilinear + anisotropic filtering. Textures the game uploaded without
  mipmaps get a generated mip chain (regenerated if the game changes the image). HUD and menu draws
  keep the game's filtering; render targets and nearest-filtered textures are left alone.
- **Depth precision** (`reversed_depth`, default 1): the game framebuffer has a 32-bit float depth
  buffer and native draws use reversed-Z (near = 1, far = 0, needs glClipControl / GL 4.5).
- **Post-processing** (`bloom`, `sharpen`, 0-3, default 1 = Low each): applied to the 3D scene just
  before the game draws its HUD, so HUD, menus and text are never touched. Bloom: bright areas
  (lights, glowing suit parts, screens) are blurred at 1/2, 1/4 and 1/8 resolution and added back.
  Sharpening: an unsharp mask limited to the local brightness range, so it adds no halos.
- **Ambient occlusion** (`ambient_occlusion`, 0-3, default 1 = Low): soft shading in corners, recesses
  and where characters' parts overlap, computed from depth and the port's reconstructed normals
  when available. The HUD is untouched; bright pixels (lights, glows) are left alone.
  Higher levels use more samples, a larger radius and more darkening.
- **Reconstructed normals**: the game draws without surface normals, so the native renderer builds
  them. Per draw, face normals are averaged per vertex (vertices at the same position share one, so
  texture seams do not show); the shader blends that smooth normal with the exact face normal from
  screen-space derivatives, keeping hard edges hard. Opaque scene geometry writes the result to a
  second color target, which ambient occlusion uses (no faceting on low-poly characters) and which
  dynamic lighting builds on. Dev command `normals 1` shows it.
- **Dynamic lights** (`dynamic_lights`, 0-3, default 2 = Medium): the game has no lights of its own,
  so the port makes point lights from game events (src/lights.cpp): a flash at the muzzle when a
  weapon fires, a flash where a shot lands, a light that follows projectiles in flight (line gun
  bolts, enemy projectiles) and a larger flash for explosives. They light nearby surfaces using the
  reconstructed normals, on top of the baked look. Only with the gameplay camera
  (not in cinematics). Dev commands: `dlights N`, `light ...` (test light), `lighttrace 1`.
- **RIG glow**: two of the dynamic lights sit on the player's back. The spine light takes the health
  bar's colour (cyan above 50%, yellow 25-50%, red below 25%, the game's own thresholds) and moves
  down the back as health drops; the stasis module's light dims as the meter empties and pulses
  brighter while kinesis is held. Part of Dynamic Lights (off when that is off).
- **Visor, saw and navigation line glow** (also part of Dynamic Lights): a faint blue light in front
  of the helmet visor; a flickering cyan light at the weapon while a melee swing plays, with a brighter
  flash when it connects; and a row of cyan lights along the navigation line's path while the line is
  shown.
- **Shadows** (`shadows`, 0-3, default 2 = Medium): shadows of things that move. The level's own
  shadows are baked into its textures, so only moving objects cast: a mesh whose vertex data changes
  between frames (CPU-skinned characters) or whose world transform changes (props, doors) is a caster
  from then on. Casters are drawn again into a 2048x2048 depth map from an assumed light above the
  camera target (16 m square); upward-facing surfaces below them are darkened, less the further the
  caster is above. That light follows the real ones: each strip light or alarm spotlight near the
  player pushes the shadow away from itself and adds to its strength (eased over ~120 ms); with no
  light near, a faint shadow falls almost straight down. The option level scales the strength. Only with the gameplay camera.
  Dev: `shadows N debug` tints shadowed pixels red and logs the caster count.
- **Light shadows** (`light_shadows`, default 1): every light that stays in the world (fixtures, lamps,
  panel glows, fans, beacons) has an identity, a fixed position and a fixed facing. A lamp looks out
  toward the open space around it (found once by casting rays through the still scene) and covers 150
  degrees; spotlights look along their cone. The **still scene** is kept in world space: every mesh that
  has stayed exactly in place for a few frames is copied into it the first time it is drawn (and taken
  out again if it starts to move, like a door). Each fixed light has a kept ("baked") map of that still
  scene and, near the player, a live tile for what moves: see *Baked shadow maps* below. Because the
  fixtures' light is already painted into the level, a blocked fixture darkens the scene there by the
  blocked share of the fixtures reaching that pixel (characters 40% of that). Short-lived lights (shots,
  impacts, RIG glow) use a screen-space trace instead. Lamps less than 0.8 m apart are one light.
  Dev: `shadows 2 debug` logs map and still-scene counts, `lightshadows 1 N`, `lightshade 0-1`,
  `probe X Y Z R` lists the lights and maps near a point.
- **Light Contrast** (`light_contrast`, 0-3, default 2 = Medium): the environment light. The game's
  painted lighting cannot react to anything, so it is turned down to a base level (100 / 85 / 70 /
  55%) on the level, props and characters alike, and the added lights bring their surroundings back
  up. Lamp faces and other bright pixels are left alone. Needs Dynamic Lights.
- **Player glows**: a glowing effect drawn at the player (the plasma saw's blade during a swing) gets
  a light exactly where it is drawn, in its own colour, for as long as it is drawn.
- **World tints**: the red alarm lighting and the hallucinations' blackouts (fade to black, then a dim
  grey a0a0a0 room) tint the level through the GL light-model colour. The renderer takes the colour that
  most of a frame's geometry is drawn with as the world tint (single objects such as an orange door lock
  have their own and are outvoted), tints the added point lights and highlights with it, and measures
  "bright pixel" thresholds relative to it, so a tinted or blacked-out room keeps its colour and darkness.
  `tools/tinttour.sh` and `tools/tintcmp.py` compare forced tints (dev command `tint RRGGBB AMOUNT`,
  `tint off`) between the legacy and native renderers.
- **See-through effects are not lit as surfaces**: fog banks, light shafts, beacon flares and steam are
  drawn over the finished scene, and the lighting pass only knows the wall behind them. Their draws also
  write their coverage to a third colour target, and what the lights, shadows and occlusion did to a
  pixel is applied only to the share they leave open. Fog banks (recognised by their vertex data) count
  in full; any other alpha-blended (not additive) draw counts where it floats more than a few centimetres in front of the
  solid scene (its depth is copied before the first such draw), so decals and holograms lying on a
  surface are still lit with it. Dev: `fogveil 0|1`.
- **RIG glow stays on the spine**: two lights of 36 cm reach along the lit part of the health meter,
  close to the back, instead of one a metre wide that tinted the whole suit; stasis module and visor
  lights shortened to match.
- **Baked shadow maps**: every fixed light (strip lights, detected lamps, fan lights, panel glows) owns a
  kept depth map of the still level as it sees it: 128 maps of 512 x 512 in one 8192 x 4096 atlas, each
  light's view passed to the shader through a small float texture. A map is drawn when its light is
  first met (nearest first, at most six per frame, fading in over 0.4 s) and again only when the still
  scene around it changes (a door opening, new geometry coming into view), and it is remembered when the
  light leaves the view (the one unseen longest goes first when all 128 are taken). A lamp's facing is
  settled once from the level around it and no longer follows the player. The 24 live tiles now hold
  only what moves (the player, enemies, doors, props) for the lights nearest the player, plus the turning
  alarm beacons, which can keep nothing. A fixed light without a map casts no shadow; the screen-space
  estimate is left to passing lights only (muzzle flashes, impacts). Shadows therefore do not depend on
  the camera, on which lights are in view, or on how the player got there.
  `tools/hallwalk.sh` walks chapter 1's second hallway at maximum settings and records filmstrips;
  `tools/shadowrepeat.sh` measures wall brightness at three spots before and after a detour.
- **Lamps everywhere**: every still level mesh is examined once by sampling its texture over its surface
  (a point every 6 cm). Bright, near-white samples that the level's painted lighting also shows lit are
  gathered into fixtures by position; a fixture between 0.006 and 0.9 m2 gets a point light whose
  strength and reach grow with its size. A mesh that is more than a fifth bright is a white surface and
  gives none. (Chapter 1 models its strip lights as separate triangles; later chapters paint them inside
  wall panels, which the earlier triangle test missed: 4-8 lights per view there, 40-90 now.)
- **Light Contrast is local**: the painted lighting is turned down only around added lamps (by how much
  unshadowed lamp light there is at a pixel), so places no lamp reaches keep the game's own brightness
  instead of just getting darker. A lamp that barely reaches a pixel cannot shadow it strongly, and a
  lamp's own face is never darkened by the light that sits on it.
- **Texture packs** (Options > Graphics > Texture Packs; setting `texture_packs`; read when the game
  starts): `mods/textures/<pack>/` next to the exe. Each texture is identified by a hash of its original
  pixels; a pack holds `<hash>.png` replacement colour textures (any size). `--dump-textures-png` (or
  `texture_dump=1`) writes every texture the game loads to `mods/textures/_dump/`. The folder's
  `README.txt` (written by the game) explains this for pack makers. (Derived PBR material maps were tried
  and removed.)
- **Bright rooms**: the scene as the game lit it is reduced to one pixel each frame (its average colour).
  In a room the game already lit brightly (average luma above about 0.18, fully by 0.34) the added lamps
  add little, the Light Contrast dimming is nearly off, overlapping lamps are compressed and the vignette
  is lighter; shadows and occlusion stay. Without this, a white room with a hundred lamps had its dark
  tones lifted and its whites greyed (washed out). Dark levels are untouched. `tools/tonecheck.sh LEVEL`
  compares tone percentiles with the legacy renderer at maximum settings (`tools/pngtone.py`).
- **Headroom**: added light only fills the headroom a pixel has left. The gain is scaled by
  (1 - the game's own colour) squared, capped at 0.45, and the result never passes 0.92 unless the game
  itself drew it brighter. Dark corridors take the lights fully; the white decks of chapter 1's later
  levels, which the game already lights to near white, gain almost nothing instead of burning out. A
  mesh in which more than a fifth of the triangles (or more than 40) look like lamps is a bright wall,
  not a row of lamps, and gives none. `tools/lighttour.sh LEVEL N` visits N doors of a level and prints
  the share of clipped pixels with the added lighting off and on (`tools/blowout.py`).
- **Solid cables, level lamps**: blended meshes that are really solid objects (the hanging cable
  bundles) are drawn opaque, so they take light and cast shadows. Small bright patches of the level's
  textures are taken as lamps and get a point light where the lockdown meshes give none. Only meshes of
  the still scene that are at least 2.5 m across are searched (the level's structure, not weapons, props
  or pickups), and a lamp goes away with its mesh. This finds only some of the lamps so far.
- **Ceiling fan lights** (part of Dynamic Lights): the game fakes light falling through its ceiling
  fans with turning shaft meshes and a floor texture. Each fan gets a real spotlight pointing down from
  it, as wide as the shafts, shaded by three blades and a hub that turn with the shafts, so the blade
  shadows fall on whatever is underneath (floor, props, characters). The fan is recognised from its
  shaft mesh (upright fins through a vertical axis that keep turning); it has a shadow map like the
  alarm spotlights and stays lit while the player is within 10 m.
  The blade pattern is the game's own floor shadow (a textured quad multiplied over the floor under
  the fan): the light samples that same texture with the quad's own mapping, carried up the cone, so on
  the floor the two coincide exactly and anything above the floor gets the matching part of the
  pattern. (Until a fan's floor quad has been drawn once, a generated three-blade pattern is used.)
  The pattern is one tile of a texture sheet; its brightness range is measured from that tile. Things
  standing under the fan are also darkened where a blade's shadow crosses them (as the game does to the
  floor), so the blades read as shadows on characters. Dev: `fanshade 0-1` (2 = debug colours).
- **Panel glows** (part of Dynamic Lights): door holograms, screens and other flat glowing overlays
  cast light in the colour they are showing. The renderer finds them as it draws (flat additive or
  bright alpha-blended meshes of the 3D scene, 0.2-2.2 m across) and takes the colour from the part of
  the texture each one uses times the tint and opacity the game draws it with, so the light follows
  the panel's state by itself: another texture or tint changes the colour, a fading panel dims, a
  hidden one gives no light. Parts of one panel are merged into one light. Found lights are used from
  the next frame on, so they get a shadow map like the others. Dev: `glowgain N`.
- **Strip lights** (part of Dynamic Lights): the small ceiling and door strip lights get point lights
  whose colour follows their state: a dim cool white normally, red during a lockdown. Their positions
  are read from the level's lockdown light meshes when a level starts (src/lights.cpp). Every fixture
  the camera can see is lit (within its 30 m view distance and inside the view frustum, up to 96), so
  lights do not switch on as the player approaches. Up to 128 lights at once in total; the shader
  skips a light for every pixel outside its radius.
- **Specular highlights** (`specular`, 0-3, default 2 = Medium): Blinn-Phong shine from the dynamic
  lights. The game has no material data, so brighter, less saturated surfaces (metal, armour plates)
  shine more than dark or strongly coloured ones. Needs Dynamic Lights on.
