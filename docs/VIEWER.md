# Standalone 3D viewer

`ShaolinMonksViewer.exe` is a separate Windows x64 asset viewer for Shaolin Monks Recomp.
It reads the original USA/Europe Xbox ISO/XISO or extracted game archives directly.
It starts with the character catalog and supports textured meshes, original vertex
normals, skeleton joints, named animation clips, looping playback, frame scrubbing,
playback speed, and optional root motion.
The **Stage** dropdown loads campaign maps, versus arenas, and extra map resources.
Scenery playback supports original U/V texture scrolling and version 4 map mesh
animations, including ambient tracks and named animation groups.
Stage rendering also previews additive light/fog layers and authored alpha fades.
Modern material records supply diffuse/reflection/gloss texture channels, UV
transforms and U/V addressing. Stage light records can be inspected in the viewer;
compact scenery retains its baked vertex lighting.
Environment particle previews use original shared sprite textures and command
settings, with inferred flame anchors and manual placement on selected objects.
Optional emission bloom previews recognized eye/glow layers and additive effects.
Atmosphere previews use original fog presets and camera-relative sky meshes.
The **Concept Art** section browses original character art, environment art,
storyboard sheets, and design studies from the game's gallery.
It also browses other environment meshes, props, and individual textures.

## Use

Choose **Open game source** and select an image, `GAMEDATA.WAD`, or `default.xbe`.
An extracted source needs all eight archive banks and the original `default.xbe`
in the same directory. The executable supplies character bone name metadata;
its game code is not executed by the viewer.

### Character Debug

The **Characters** library shows an ON/OFF **Character Debug** toggle in place of
the Stage dropdown. Enable it to try the selected character on a large floor
using the checker texture read from the original stage texture banks. Each checker
square is one debug meter. The debug scene defines 512 source coordinate units as
one meter; this is a viewer calibration, not a verified original engine unit.
Black distance fog stays enabled, reaching full black at 60 debug meters, well
before the floor boundary. The floor recenters in whole-meter steps during travel,
with its texture coordinates anchored to the world. The floor and character share
the preview lighting setting. A 35% neutral diffuse tint keeps the original white
checker from dominating the actor; exported source texture pixels are unchanged.
The floor uses eight-meter tiles and the debug camera uses a five-centimeter
minimum near plane to preserve depth precision around the actor.

Click the canvas to control the character. **WASD** or **arrow keys** move relative
to the camera; hold **Right Shift** to run and press **Space** to jump. Mouse camera
navigation stays available, and the camera follows the character. Release movement
to return to idle. Hold **Space** for a full jump; an early release shortens a
standing jump. Running jumps use their own recovered profile. A source landing
clip starts when the actor reaches the floor. Focus loss freezes the debug
controller and releases its movement input. Text entry and library/inspector focus
do not drive the actor.

Modern character-specific idle, walk, run, jump, and landing clips take priority.
Short walk start/stop entries are excluded when a full gait is available. Older
legacy banks can have incompatible track orders; Character Debug prefers compatible
fallbacks for these banks while keeping original clips available in the ordinary
animation inspector. Packed legacy rotations use high/middle/low ten-bit X/Y/Z fields;
legacy source-specific track mapping remains unverified.
Run/jump fallbacks come from the shared motion bank. Missing unarmed idle/walk and
attack previews use a compatible authored humanoid bank from the supplied game;
the shared bank's weapon-holding `y00`/`y01` poses are not used as unarmed movement.
Bone-name metadata verifies the fallback track order. Unmatched body/finger tracks
remain unmapped, and non-root translations retain the target rig's own limb lengths
instead of stretching it to the source actor. **Movement clips** identifies fallbacks.
Nonstandard rigs may lack compatible actions:
missing ground actions hold idle/bind pose, and a missing jump disables jumping.
These limitations are reported in the inspector.

Ground movement changes blend joint rotations with quaternion interpolation and blend
local translations over 0.16 seconds. Walk/run speed estimates use authored root
travel or planted-foot motion when available, with nominal fallback speeds.
With a compatible original action graph, the controller uses fixed 60 Hz updates,
the source frame clock, authored attack root travel, and the executable's ground
turn limit. **Movement rate** starts at 1x in this mode. Clip-only fallbacks retain
their 2x preview rate. The slider scales walk/run travel and cadence together. Clip
height calibration and lower-foot contact keep grounded poses on the floor;
hanging garments do not determine actor height. The starting camera looks from
the character's left toward the right. Mouse orbit remains available.

**Attacks and kombos** provides these additional canvas controls:

| Key | Preview |
| --- | --- |
| J | Quick attack |
| K | Power attack |
| L | Launch attack |
| U | Throw |
| E + J/K/L | Special modifier with the chosen attack |
| I | E + J shortcut |
| O | Quick sequence in clip-only fallback mode |

The viewer reads each matching `ERG.DT2` input graph and `ERG.DAT` action records
from the supplied game. Button masks, held/forbidden inputs, graph child/sibling
links, input timeouts, minimum execution frames, target start frames and blend
durations replace name-sorted attack sequencing. Repeated presses follow the
character's graph; holding an attack does not invent additional presses. Up to
nine inputs can wait for their source execution frame. Ground and airborne roots
are distinct. Inspector attack buttons feed the same input path. The individual
move browser remains a direct clip preview.

This is a recovered subset of the original controller, not complete gameplay
parity. World/target/hit/unlock predicates without a simulated result fail closed;
paired throw victims, damage, collision, attack effects and progression are not
implemented. Some inputs therefore have no executable branch on the empty floor.
Compatible graphs retain the character's own action IDs and clips, including
legacy encodings; borrowed preview attacks do not replace those graph targets.
Assets without a compatible graph retain explicitly labeled clip-only sequences.
The browser marks retargeted fallback and legacy entries.

Normal jump profiles are read from the supported original executable, including
launch velocity, integer gravity, the early-release window, and airborne steering,
drag, turning, and speed limiting. The controller integrates velocity before gravity
at fixed 60 Hz rather than stretching an invented arc over the clip duration.
Standing jumps can shorten once on early release; running jumps retain the source
profile's full launch. The jump pose retains the grounded origin, advances at one
source frame per physics update, and holds its last frame until landing. Landing
starts on floor contact and accepts movement after the recovered cancel frame.
Unsupported executable metadata disables jumping instead of substituting guessed
parameters. No original executable code runs inside the viewer.

Isolated tests compared full/short/running vertical motion and airborne velocity
with the supplied game's native routines. This does not establish complete
controller or animation-transition parity. Ground gait selection/speed estimation
and locomotion blends remain viewer behavior; gameplay combat resolution,
double/wall/long jumps, upgrades,
world collision, and articulated foot IK are not implemented in Character Debug.

The graph loader and button matcher were traced through retail routines `1D84A0`,
`1D9DB0`, `783E0`, `7A980`, `7A150`, and `1C2880`. An isolated x86 comparison of
the portable button matcher against the supplied executable passed 347,680 cases.
Synthetic regression checks cover buffered links, authored start frames/blends,
held-input behavior, unavailable predicates, retained attack travel and identical
controller travel/frame results at 30/60/120/144 host FPS. These checks do not
replace gameplay or visual comparisons against the original game.

Use the left library to change characters while keeping Character Debug enabled;
each new character starts at the origin. **Reset character** resets travel, heading,
and movement state. Disable the toggle to return to the ordinary animation inspector
and restore its previous clip, time, and camera. Debug floor geometry and shared
fallback clips are temporary; the floor is excluded from hierarchy selection,
character framing, source geometry counts, and FBX export. Gameplay files are read-only.

### Animation inspection

Select a character and a clip in the animation dropdown. **Bind pose** restores
the rest geometry. **Show joints** draws the skeleton through the surface;
the joint list selects a highlight. **Frame** scrubs while **Play** is disabled.
The preview samples clips at 30 frames per second by default. **Playback FPS**
sets the clip clock independently of **Rate**; **30 FPS / 1x** restores the defaults.
Elapsed seconds and clip duration appear below the controls. Playback follows elapsed
time even when rendering falls below ten frames per second. This nominal rate is
not a verification of each gameplay animation: event scripts can override speed,
and those overrides are not executed by the viewer.
Looping playback interpolates from the last stored key back to the first over the
remaining clip interval, retaining in-place root rebasing and shortest-angle
rotation interpolation. One-shot debug attacks, jumps, and landings hold their end
pose instead. Continuous loop boundaries retain secondary-motion history.

Characters with original joints containing `flap` expose **Dynamic joints**,
enabled by default. The viewer reads spring stiffness, damping, mass, gravity,
stretch, and environment scales from the supplied executable and integrates the
hierarchy at its recovered 60 Hz update rate. Attachments stay on the animated rig;
Character Debug travel and turning drive inertia. Gravity lets the tails settle
rather than holding them in a narrow cone around their exported horizontal pose.
Pausing freezes motion; scrubbing or selecting bind pose clears its history.
Hair and clothing roots retain their full animated transform; simulated children
inherit that root's orientation and use their simulated positions, matching the
original joint manager. This keeps scalp-side hair vertices attached while tails
move. Turning dynamics off restores the original animation and weighted skin.

The force, velocity, extension, and attachment-matrix steps were compared with
isolated executions of the original x86 spring routines and joint manager.
This is a port of those steps, not complete
gameplay cloth parity: scripted per-joint overrides, wind fields, collision volumes,
and collision response are not reproduced. Unsupported executable layouts retain
**Secondary motion (preview)** with the older bounded spring behavior.

Character previews use an adjustable directional light. **Directional shadows**
starts on with Lighting and casts filtered, alpha-masked shadows onto the debug
floor and the character itself. **Light azimuth** and **Light elevation** change
the direction. Hidden geometry and transparent texels do not cast solid shadows.
This light and its shadow map are viewer features. Stage light records, baked
vertex illumination and baked shadow overlays use separate paths described below.

Animation mapping resolves short body clips separately from extended tracks and
uses the actor's executable registry where available, preserving character-specific
facial/finger ordering. Variants with only a static shared reference frame default
to a compatible retargeted idle; the original reference clip remains selectable.

Each clip starts at the character's rest root over the grid center. The centered
preview preserves pelvis sway and vertical motion while subtracting a straight
start-to-end horizontal travel path. **Root motion** retains the full horizontal
travel relative to that starting point. The ground grid stays at the original
rest floor when panning, reframing, scrubbing, or changing clips.

Use the **Stage** dropdown in the library to select a map. Its filter searches
stage names or resource IDs. Selecting a stage switches to the **Stages** category
and frames its geometry from above. Names come from the original executable
and map metadata; unnamed resources retain numeric stage IDs. Empty map slots are
omitted. Choose **Characters** in the category menu to return to the roster.
After clicking a stage in the left list, **Up** and **Down** load the previous
or next matching stage automatically. Keyboard navigation follows the current
search filter and scrolls the focused row into view.

Stage previews apply static mesh rotations and translations, load the associated
textures, and show original vertex colors. Mesh decoding preserves each triangle
strip's winding flag so back-face culling keeps the intended front surfaces.
Compact legacy exports without strip-direction bits or stored normals receive a connected-surface orientation
repair. Broad terrain sheets face upward and closed components face outward.
Open volumetric props use opposing surfaces and centered radial evidence to face
outward, including small disconnected details belonging to the same shell.
Other components retain the majority source orientation. This restores the checkerboard
floor gaps in Extra Map 2 [5413] and the terrain orientation in Extra Map 5 [5473].
It also repairs the open statue shell in Extra Map 7 [5513] and incense pots in
Extra Map 12 [5613]. This orientation evidence is a preview inference.
**Repair legacy winding** in Geometry toggles the repair to compare with the source.
It cannot reconstruct absent geometry or prove the intended side of every open surface.
Normal-less legacy shells with open or non-manifold edges also receive a
two-sided preview fallback, so uncertain room walls and props do not disappear
when viewed from the other side. This applies to solid surfaces; authored-normal
meshes, closed solids, and translucent effect layers retain ordinary culling.
**Two-sided object preview** in **Object details** overrides this per object.
Disabling **Repair legacy winding** also disables the inferred fallback for source
comparison. The fallback changes rendering and picking, not stored normals or FBX
faces, and does not establish the original game's intended front side.
**Flip object faces** under the selected object's **Object details** reverses all its
material batches and normals. These changes affect the preview and FBX export only.
**Stage helpers** reveals temporary volumes, source-hidden collision objects,
and legacy backgrounds hidden by default. Collision barriers can reuse ordinary
wall textures and appear as solid curtains if revealed. **Cull back faces** helps
inspect interiors from outside. Both can also be inspected with the part controls.
Use the **mouse wheel** to move closer and **Frame asset** or **F** to reframe visible parts.
Light shafts and fog meshes with additive render flags brighten the scenery using
their original textures, vertex tint, and alpha fade. Their depth-write flags are
preserved, so effects can overlap while solid geometry still occludes them.
Named light beams and matching opaque, row-invariant grayscale shaft masks with
fading vertex alpha receive an inferred additive light-shaft preview. Their inspector
role identifies the inference. A shared effect flag alone does not establish that
an entire prop is translucent: fully opaque volumetric props with depth writes,
such as Goro's Lair [4433] fire bowls, retain an opaque base and are excluded from
emission bloom. The inspector labels these **Opaque effect-flagged prop (inferred)**.
Gradient texture/vertex alpha uses continuous blending without the cutout threshold.
Ordinary translucent layers disable depth writes; additive layers retain their
source depth-write flags. Binary cutouts retain alpha testing. Solid scenery with
fully opaque vertices and at most 5% binary, dark transparent texels receives an
inferred RGB-only diffuse preview. This prevents sparse black detail from becoming
holes, including The Tower [4453] BackWall02 and Extra Map 7 [5513] wall/trim textures.
Sky masks, effects, substantial cutout masks, and gradient alpha keep transparency.
The inspector labels this inference; **Use texture alpha** overrides it for the
selected material batch. Vertex transparency remains
active when **Vertex colors** disables the RGB tint.
Alpha materials filter premultiplied texels and recover straight RGB in the shader,
preventing invisible texel colors from bleeding into mask edges. Opaque sampling
and texture PNG exports retain the original RGB/pixels. These material overrides
are session controls and never modify the original game. FBX exports preserve
scalar opacity and diffuse alpha selection, but do not recreate game-specific shaders.
Modern version 3 material extensions retain their diffuse, reflection and gloss
texture channels and independent UV offsets/scales. U and V can wrap or clamp
independently. Textures use up to 8x anisotropic filtering and generated mipmaps;
alpha views are premultiplied before mip generation. Particle animation atlases
use gutters and level zero to keep frames separate. Source texture exports remain
unchanged. Reflection/gloss rendering is a supported preview pass, not a complete
reconstruction of the original Xbox shaders. **Object details** reports channel
slots, material flags, addressing and baked/normal-based lighting.

**Stage lighting** shows the original point, spot and directional light records,
including their position, color, near/far radii and enable flags. **Light records**
allows individual lights to be disabled; hover a record for its stored values.
**Show light positions** draws colored markers without changing mesh framing,
selection or exports. **Source lights** starts enabled and lights normal-bearing
scenery with up to four nearby point/spot lights and two directional lights per
batch. The source color normalization and near/far ranges are retained; light
selection, cone shading and ambient response are preview approximations. Compact
scenery already stores baked vertex illumination, so **Lighting** uses that tint
without multiplying a generic directional light over it. Turning off **Lighting**
and **Vertex colors** together exposes the untinted textures. Baked shadows and
shadow receiver meshes retain their existing passes; stage lights do not create
new dynamic shadow maps. Character directional shadows are separate.

Effect layers draw after solid scenery and sort from far to near; animated mesh
positions update that ordering. Additive layers stay unlit under the preview's
**Lighting** option. Hover a part to inspect its blend mode and depth-write state.
Flagged shadow receivers use a multiplicative preview: white texels preserve the
floor below and dark texels darken it. Goro's Lair [2833], Part 212 / material 34 is
one such projection surface, rather than a replacement ground texture. Dynamic
shadow generation/projection is not reproduced. Flat stage meshes with black RGB
vertices and fading vertex alpha are also identified as **Vertex-alpha shadow overlay
(inferred)**. These can reuse a pillar's diffuse texture: Soul Tombs Map 2 [933],
Part 146 / PillerMid is one example. Their vertex tint remains active when **Vertex
colors** is off, and distance fog fades their opacity instead of coloring them orange.
This inference does not reproduce dynamic projection or change their stored positions.
Hierarchy rows label **(shadow)** for both kinds, and **(camera)** or **(helper)**
where source metadata identifies them.
**Object details** shows the role, object ID, render/object flags, texture slot, and
vertex-alpha range. Unflagged colored boxes or effect panels remain **unclassified**;
their shape or color alone does not establish a collision or trigger role.

Supported **FLICKERMIN**, **FLICKERMAX**, and **FLICKERSPEED** properties drive the
original background-opacity sine formula on the scenery timeline. Soul Tombs Map 1
[913], Part 109 / Material #128 is a scrolling fog sheet with this per-object alpha;
it must not remain opaque merely because its texture alpha is solid white.
**Animate textures** also controls this material animation. Disabling it or choosing
Static pose restores the source phase-zero opacity. Object details reports current
opacity. Random flicker perturbations and gameplay-driven overrides are not simulated.
Its fixed-step phase follows the preview's 60 Hz scenery clock; exact gameplay
flicker timing has not been verified.

Separate eye/glow masks can be intentional material layers. Soul Tombs Map 2 [933],
Part 306 / StatueEyes overlays the statue face with an alpha-blended eye mask.
The viewer keeps source additive layers and identified no-depth-write statue-eye/glow
materials unlit, preserving their blend modes. The selected-object inspector labels these
**Glow source (preview)**. Name-based glow recognition is an inference.

**Lighting** and **Bloom (preview)** under Display start enabled. Bloom starts at
**Glow strength 0.8**, **Glow threshold 0**, and **Glow radius 1.5**. Both toggles remain
user-controlled when changing assets. Bloom adds a soft halo from those
recognized emission layers, with **Glow strength**, **Glow radius**, and **Glow
threshold** controls. Opaque scenery occludes the emission mask. Ordinary bright
scenery, sky backgrounds, selection highlights, and flat image previews are excluded.
Mesh-based additive flames, eye masks, and recognized light shafts feed this pass;
ordinary stone, floors, and solid fire-bowl bases do not become glow sources.
Viewport PNG exports include enabled bloom; mesh and texture exports do not bake it.
The source contains glow-related stage effects and a blur subsystem, but the game's
full-screen bloom behavior is unverified. This optional viewer pass does not reproduce
the original post-processing pipeline, HDR, or tone mapping.

The **Atmosphere** section enables **Sky domes**, **Distance fog**, and **Stage
background** by default. Flagged sky meshes use their original textures and follow
the camera's position while retaining their orientation and layer offsets. They
draw behind solid scenery, stay unlit, and retain supported texture scrolling.
Sky visibility is separate from collision/helpers and from the hierarchy checkboxes.

**Fog preset** starts with **Primary** and exposes the five presets stored in the
map's atmosphere resource. Distance fog uses their colors, start/end distances,
and minimum/maximum opacity. Authored reversed distance ranges are retained.
Additive effects fade toward black under distance fog to avoid colored halos.
Some presets intentionally have no fog. Missing settings are identified in the
inspector; the viewer does not invent fog or sky geometry for those maps.

The selected fog color supplies the fallback **Stage background** behind the sky
and geometry. Without atmosphere data, that fallback is black. **Background**
allows a custom color. **Reset atmosphere** restores the primary preset, source
color, and enabled atmosphere toggles. Loading another asset restores its own
primary preset and clears the custom color. Distance fog can obscure distant
geometry in an overview; disable it to inspect those surfaces.

Camera-relative sky meshes are excluded from map framing and viewport picking,
so clicking only the sky clears selection. Their hierarchy rows are marked **(sky)**
and still support visibility and selection highlighting. F on a sky row frames
the scenery because the sky is a camera-relative background. FBX exports retain
visible sky meshes at their original stored positions and do not bake atmosphere;
viewport PNG exports include the rendered sky, fog, and background color.

The **Scenery Animation** section starts with **Ambient** playback. **Play** pauses
or resumes, **Restart** returns to the start, **Time** scrubs in seconds, and **Rate**
changes playback speed. **Loop preview** repeats the selected timeline; disabling
it stops at the end. **Animate textures** and **Animate meshes** control the two
channels independently. **Static pose** restores the exported geometry and texture
coordinates. The scene timeline uses 60 source frames per second at 1x.

The animation dropdown also lists named groups such as doors, destructible scenery,
or cutscene props. Selecting one previews that group while ambient tracks continue.
Each track retains its original looping, one-shot, or bounce behavior. Groups are
selected manually; gameplay triggers and event scripts are not executed.
Mesh motion interpolates translations, quaternion rotations, and scales, including
parent transforms. Texture scrolling uses the mesh's original U/V rate properties;
UV-only environment previews also receive a timeline. FBX exports save the current
mesh pose and texture offsets. Exported diffuse PNG files retain their original pixels.

Choose **Concept Art** in the category menu to open the artwork gallery. Use its
thumbnail grid, group filter, and search field to find pages. **Previous** and
**Next** follow the filtered collection. With the image hovered, the left and right
arrow keys change pages; dragging pans and the mouse wheel zooms. **Fit image** or
**F** restores the complete page. **Save artwork (.png)** exports the original
decoded image at its stored resolution, including the original gallery framing.

Gallery labels identify the artwork group, page number, and source resource ID.
The collection includes character renders and storyboard sheets presented as artwork
in the game. Team photos, move instructions, loading screens, and UI atlases remain
outside the Concept Art section. These assets are read from the supplied game;
the viewer package contains no artwork files.

| Control | Action |
| --- | --- |
| Left click | Select an object; click it again or empty canvas to clear |
| Left drag | Orbit |
| Middle drag | Pan along the camera's screen axes |
| Mouse wheel or right drag | Dolly closer or farther |
| F | Focus the selected object, or frame the visible asset if nothing is selected |
| Up / Down in the stage list | Load the previous / next matching stage |
| Ctrl+O | Open source |

Switching directly between stages preserves the camera target, orbit rotation,
distance, and FOV; clipping bounds update for the new map. Initial stage entry
frames the map, and **Frame asset** or **F** still reframes explicitly.

Selected objects receive a semitransparent yellow highlight across all of their
material batches. The inspector shows the selected part name and a **Clear selection**
button. F fits the current visible geometry of that object and keeps the camera's
viewing direction. Selection clears when loading another asset. Highlighting changes
the preview only; FBX and texture exports omit the selection highlight.

For 3D assets, the **Hierarchy** panel occupies a full-height column to the right of
the preview, beside the inspector. Each object has one row; expand its arrow to inspect multiple material
batches. Selecting an object in the viewport selects and reveals its hierarchy row,
expanding its material list when present. Clicking an object or material name in the
hierarchy selects the same object and highlights all of its visible materials in
yellow. Viewport deselection and **Clear selection** also clear the hierarchy selection.
The adjacent checkboxes control visibility separately: an object checkbox affects
all of its materials, while a material checkbox affects only that batch.
Named character wound-cap materials (`*_cap`) start hidden in intact previews,
including Character Debug. They remain available through the hierarchy and
**Show all** for inspecting dismemberment geometry; gameplay dismemberment states
are not simulated.
**Show all** and **Hide all** are above the hierarchy list. F also works after
selecting an object from the hierarchy. Image previews use the full library height.

**Stage BGM** in the stage inspector starts enabled. Selecting a stage asynchronously
loads its original base-intensity music cue and loops the decoded ADX track using
its authored loop points (or the whole track when no loop is stored). Uncheck the
box to stop playback; the setting also applies when selecting another stage.
The **Volume** slider directly below the checkbox adjusts playback from 0 to
100%, multiplied by the original cue gain. It updates a playing track immediately
and retains its level across stage changes and BGM off/on during the session.
At 0%, playback continues silently. Re-enabling starts the current stage's track again. Switching to a character or
another asset, changing game sources, or closing the viewer stops stage music.
Maps with a shared sound-bank area use that area's music definition. If no source
association exists, the inspector reports **No BGM defined for this stage**.
The supported local disc provided associations for 87 of the 165 indexed stage
meshes; unused/extra resources and other unmapped stages remain silent. This is
base-cue playback; combat intensity changes and scripted music overrides are not
executed. The viewer decodes standard unencrypted mono/stereo ADX inside AFS
archives directly from the supplied game, without extracting or bundling music.
Audio output uses Windows XAudio2 through the Windows SDK; no extra audio codec
package is required. Hidden validation bypasses audio-device initialization.

These mouse controls apply to 3D previews without holding a keyboard modifier.
A click selects on release; dragging orbits without changing the selection.
A drag must start over the viewport;
it continues until the mouse button is released or the window loses focus.
Images use ordinary dragging and the mouse wheel for pan and zoom.
Horizontal orbit drags turn the near side of the model in the drag direction;
horizontal panning moves the image with the pointer.

Use the right hierarchy for visibility, and scroll the adjacent inspector for camera
presets, display options, geometry statistics, and exports.
The **Visible mesh + textures (.fbx)** button and File export command write binary
FBX 7.4. Each visible material batch becomes a mesh with its current posed vertices,
normals, UVs, vertex RGBA colors, and a material. UVs include source diffuse transforms
and the current texture scrolling. Diffuse textures are separate PNG files referenced
by relative names; keep them beside the FBX. Opaque diffuse variants use separate
PNGs without changing source pixels. Selection highlighting is omitted.
Coordinates convert to right-handed Y-up, preserving their source magnitudes;
the FBX centimeter unit declaration does not calibrate game units to physical meters.
Export captures a static pose, without skeletons, animation, generated debug floors,
or particle sprites. Reflection/gloss passes, game-specific blend/depth state, lighting,
fog, and bloom are not reproduced by the FBX materials. Viewport and individual
texture exports use PNG.

The viewer does not access saves, game settings, or physical controllers.
Stage BGM uses the original music archives and XAudio2 as described above.
It writes only exports and explicitly requested diagnostic reports or captures.
Original game data is excluded from source and viewer packages.

## Preview limits

Character meshes and motion use the original archive data. The viewer preserves
authored vertex normals, rotates them with the weighted animation joints, and
interpolates them across triangles for smooth lighting. Static stage rotations
also transform normals. Compact scenery without stored normals uses generated normals,
smoothed across coincident vertices and UV/material seams within each source object.
Creases over 60 degrees remain sharp, and authored normals are preserved. This does
not recover normals absent from the source data; some faceting is intrinsic to the mesh.
The geometry inspector reports authored and generated normal counts.
Material rendering supports the modern source diffuse, reflection and gloss
channels. Reflection coordinates use a sphere-map approximation with the recovered
half-strength blend and a gloss mask; this does not reproduce every original
vertex/pixel shader. Character texture alpha is treated as opaque because many
diffuse maps use it for lighting information. Substantial masks with at least 95%
fully opaque/transparent texels retain alpha-tested cutouts, allowing filtered robe
hems such as Priest [6303] to render holes and cast masked shadows. Gradient lighting
alpha remains opaque. Additive light/fog blending previews
the stored mesh flags. Optional bloom uses a viewer emission mask and blur;
the full game post-processing and tone mapping are not reproduced. Transparent
foreground layers attenuate the emission mask in depth order. FBX exports
do not encode the viewer's blend equations or depth-write states.
The particle preview reads the supported disc's shared command and sprite bank.
It evaluates source lifetime, emission delay/burst count, maximum count, flagged
size/color changes, interpolation delay, alpha ease-in, rotation, and supported
origin/life/size/speed/direction/fade variation flags. Supported velocity and gravity
use the recovered hundredths scale and discrete 60 Hz integration; an isolated
original-x86 comparison checked 120 positions across both native update paths.
Sprites face the camera, use per-particle source texture animation, and support
alpha, additive and reverse-subtractive blending. Opaque depth occludes particles;
transparent foreground layers attenuate their glow. Additive particles feed bloom.
Particle color works independently of the vertex-color and lighting switches.

**Particles (preview)** starts enabled. Compact, warm emissive flame meshes receive
inferred anchors; other stages can have zero automatic emitters. These anchors and
their chosen command are viewer inferences, not recovered gameplay placements.
Select a stage object and use **Add at selected object** to preview another command
at its upper center. Select its **Particle emitter** hierarchy entry to change the
command with **Apply command**, hide it, or **Select anchor**. **F** frames the anchor.
Viewport clicks pass through particles to the original stage objects.
The scenery timeline controls particle playback, pause, rate, restart, and seeking;
**Static pose** hides particles. Random samples are deterministic when seeking.
Anchors follow the current animated mesh bounds. Preview additions last until the
stage is reloaded; they do not modify the game files. Captures include particles;
FBX exports omit these generated camera-facing quads.

This remains a bounded billboard preview, not the complete original emitter engine.
The source 60 Hz velocity/gravity step is verified for the tested paths; full
gameplay emission scheduling is not. The budget is 64
emitters per stage and 64 particles per emitter, with at most 4096 spawn candidates
evaluated per emitter per sample. Collisions, bounce, friction, attachment history,
script-triggered births, specialized draw modes, velocity scaling modes and the
remaining command flags are not reproduced. Existing
flame and incense-smoke scenery meshes continue their material animation separately.
The viewer does not execute animation events, gameplay-controlled attachments,
complete gameplay cloth behavior, or every original lighting/material pass.
Dynamic flap joints use recovered default spring integration. Gameplay collision
volumes, wind fields, and scripted spring overrides remain unsupported.
Some detached parts, weapons,
and transformation meshes are initially hidden; use the part controls to inspect them.
Centered playback estimates travel from the clip endpoints; it does not apply
foot locking or reproduce actor movement supplied by gameplay. Walks authored
in place can still slide against a stationary grid. Enable **Root motion** to
inspect the complete movement stored in the clip.

Variants can reuse clips from a matching skeleton; the inspector identifies shared banks.
Legacy bone index mappings are marked as preview mappings. Complete visual verification
of every clip, facial rig, variant, and supported disc region remains outstanding.
Unused resources can have generic names and placeholder textures. Stage animation
uses the original version 4 map banks accepted by the supported executable.
Legacy version 2/3 map banks remain static and are identified in the inspector;
their texture scrolling can still play. Procedural shader animation, non-particle
texture flipbooks, random-offset UV modes, complete original particle simulation,
gameplay objects, triggers, collision behavior, event scripts and procedural sky changes
are not reproduced. Some animation tracks target objects outside the exported
mesh collection and cannot move a visible part in this preview.
Some helper geometry can remain visible when it has no identifying metadata;
use the part controls to inspect it. Other prop placement remains approximate.
Flagged sky domes render as camera-relative backgrounds. Unflagged distant mountain
planes retain their stored world positions; some legacy backdrops can still appear
detached. Fog previews use a manually selected global preset; automatic per-object
or region-based preset changes and scripted atmosphere transitions are not reproduced.
The sky pass does not reproduce every original material or procedural sky effect.
Lava-textured barriers and light/shadow overlays can also differ from gameplay when
they depend on event state or material passes outside the diffuse preview.

## Build

The viewer uses C++20, native Win32, Direct3D 11, Dear ImGui, miniz, and
XboxXBERecomp's read-only disc reader. It does not link the game runtime or SDL.
Use the pinned SDK and dependency checkout described in [BUILDING.md](BUILDING.md)
and [UPSTREAM.md](../UPSTREAM.md). Configure the main project first to obtain
the existing pinned ImGui and miniz sources; a full game build is unnecessary.

From the repository root:

```powershell
python tools/build_recomp.py --xbe "C:/Games/ShaolinMonks/default.xbe" --sdk third_party/XboxXBERecomp --llvm "C:/Program Files/LLVM" --configure-only
python tools/build_viewer.py --test
python tools/package_viewer.py --exe build/viewer/ShaolinMonksViewer.exe
```

The viewer build loads the Visual Studio C++ environment automatically and verifies
the SDK revision. `--sdk`, `--deps`, and `--build-dir` override local input/output paths.
The game executable and the main Windows release package are unchanged.
The separate viewer ZIP and its checksum are written to `dist/` using an explicit file list.

## Validation

`ctest --test-dir build/viewer --output-on-failure` runs synthetic mesh, texture,
skeleton, compressed animation, skin interpolation, and malformed input checks.
Particle checks cover malformed command input, inferred-anchor exclusions,
deterministic seeking and random variation, camera-facing sprites while paused,
source lifetime/delay/size/alpha, moving and hidden anchors, unchanged source
geometry framing/counts, and command changes. Windowless WARP checks verify actual
particle pixels, unlit color, source-frame changes and seeking, additive bloom,
subtractive blend flags, discrete gravity, opaque occlusion and selection pass-through.
Stage rendering checks cover channel/light decoding, malformed pointers, legacy
layout exclusion, source UV transforms, independent wrap/clamp, reflection/gloss
pixels, baked-light preservation, source point lights and subtractive compositing.
Glow checks include attenuation through transparent foreground geometry.
Character debug checks cover authored/common action precedence, camera-relative
movement, diagonal normalization, render-rate-independent travel, running, jump and
landing states, quaternion pose transitions, source-model restoration, floor UV
scale, framing/count exclusions, and forced black fog through actual WARP pixels.
They also cover short gait rejection, legacy fallback precedence, attack grouping,
buffered sequences, one-shot completion, mixed/UI attack execution, right-side
camera initialization, shared-rig limb lengths, character cutouts versus lighting
alpha, and loop-tail pose/root continuity. Hidden keyboard tests use synthetic
ImGui events and never send physical input.
Root checks cover remapped tracks, multiple roots, centered start positions,
preserved hip sway and height, planted-foot compensation, retained travel,
deterministic seeking, bind-pose restoration, and a fixed rest grid.
Geometry checks cover float and packed normals, coordinate conversion, stage
normal rotation, weighted animated normals, and fallback normals. FBX checks cover
binary node lengths, visible posed geometry, normals, winding, UVs, vertex colors,
material connections, relative PNG paths, and opaque/alpha texture variants.
They also cover both strip windings, alternating triangle orientation, and winding
and parity resets after strips with even or odd lengths in stage and other mesh previews.
Additional checks cover adjacent legacy surface repair, open prop shells and their
detached details, smoothing across material seams without rounding sharp corners,
authored normal preservation, the source-orientation toggle,
per-object face/normal reversal, render-rate-independent animation timing,
and secondary joint length, skin deformation, pause, scrub, and disable behavior.
Legacy secondary motion checks also cover rapidly translating/rotating heads at
15, 30, 60, and 120 updates per second, bounded bending, parallel-tail separation,
fixed attachments, and independence from unrelated model bounds. Dynamic joint
checks cover source-parameter decoding, an independent native spring trace,
render-rate independence, pause/disable behavior, actor inertia, scalp-side skin
attachment, and inherited child orientation through moving heads and loop seams
at 144 render updates per second. WARP checks
cover directional floor shadows, light direction, hidden casters and transparent
texels. Bone-table fixtures cover mixed track counts and facial order.
Camera checks cover projected orbit/pan direction, screen-aligned pan, viewport scaling,
stage framing, dolly limits, and depth precision after dollying away from a tiny selection.
Music checks use synthetic archives to cover base-cue selection, shared-area
association, ADX stereo predictor histories, version-three/four headers, partial
blocks, loop points, invalid/truncated input, asynchronous stage switches, and
mute/re-enable behavior. Playback checks use a silent output path and do not
validate physical speakers or audio-device recovery.
The stage checks use synthetic archives to cover map selection, missing labels,
resource boundaries, static rotations, translations, packed render attributes,
ordinary versus additive flags, depth writes, and hidden collision/helper visibility.
The windowless WARP material check exercises actual GPU output for low-alpha fades,
additive compositing, opaque occlusion, overlapping effects, far-to-near alpha
ordering, animated sorting, and independence from preview lighting.
It also checks multiplicative shadow surfaces, neutral white texels, continuous
texture alpha classification, and vertex transparency with RGB tint disabled.
It also checks sparse diffuse alpha inference and exclusions, filtered mask-edge
colors, texture-alpha overrides with vertex fades, and one-sided/two-sided picking.
Geometry fixtures preserve authored-normal and closed-solid culling while checking
the open legacy shell fallback and original-orientation comparison.
The windowless WARP glow check covers planar shadow inference and fog fading,
emission-only bloom, threshold/strength, opaque occlusion, transparent texels,
unlit alpha emission, viewport resizing, and unchanged selection/picking and flat previews.
Stage fixtures also check additive emission and constrained eye/glow-name inference.
They also cover light-shaft mask inference, opaque effect-flagged props, source
opacity-flicker properties, timeline seeking/toggles, and requested lighting/bloom defaults.
Selection checks cover nearest-surface IDs, hidden geometry, texture alpha holes,
current animated geometry, grouped yellow highlights, deselection, and object framing
in narrow viewports. The hidden WARP interaction check uses synthetic archives and
ImGui input events to exercise click/toggle/empty-space selection, F focus, mouse-only
orbit/pan/dolly, drag ownership, focus loss, filtered stage Up/Down loading, and image
pan/zoom. It does not show a window, move the OS pointer, or poll physical controllers.
Hierarchy checks cover selection in both directions, the yellow viewport overlay,
placement to the right of the viewport,
independent visibility toggles, grouping material batches, and automatic expansion
and scrolling through hundreds of objects at fractional display scaling.
Atmosphere checks cover the original five-preset block, source association, invalid
values, reversed/empty ranges, and sky render flags. Windowless WARP checks cover
fog depth and opacity, source/custom background colors, additive attenuation,
camera-relative skies, sky layer ordering and scrolling, foreground occlusion, lighting independence, visibility,
framing bounds, and unchanged selection IDs and flat image previews.
Scenery checks cover original property chains, short and signed U/V rates,
map tracks and groups, quaternion interpolation, nonuniform-scale normals, parent
motion, loop/one-shot playback, deterministic seeking, independent channel toggles,
static reset, and invalid data. FBX checks include the animated texture offsets.
Artwork checks cover collection boundaries, category numbering, full-size image
selection, nested thumbnail banks, missing previews, and source integrity.
Read-only real-disc diagnostics can run without showing a window, audio, or controller polling:

```powershell
build/viewer/ShaolinMonksViewer.exe --source "C:/Games/ShaolinMonks/game.iso" --validate --report "C:/Temp/viewer-report.json"
build/viewer/ShaolinMonksViewer.exe --source "C:/Games/ShaolinMonks/game.iso" --smoke-test --asset 6029 --clip 0 --frame 20 --capture "C:/Temp/liu-kang.png"
build/viewer/ShaolinMonksViewer.exe --source "C:/Games/ShaolinMonks/game.iso" --ui-smoke-test --capture "C:/Temp/viewer-ui.png"
build/viewer/ShaolinMonksViewer.exe --source "C:/Games/ShaolinMonks/game.iso" --smoke-test --stage 97 --capture "C:/Temp/stage.png"
build/viewer/ShaolinMonksViewer.exe --source "C:/Games/ShaolinMonks/game.iso" --smoke-test --stage 5 --time 1.1 --capture "C:/Temp/animated-stage.png"
build/viewer/ShaolinMonksViewer.exe --source "C:/Games/ShaolinMonks/game.iso" --ui-smoke-test --stage 97 --capture "C:/Temp/stage-ui.png"
build/viewer/ShaolinMonksViewer.exe --source "C:/Games/ShaolinMonks/game.iso" --ui-smoke-test --asset 513 --capture "C:/Temp/concept-art.png"
```

`--validate` checks mesh and texture decoding, decodes animation banks, and samples
each available clip at its start, middle, and end for finite joint and vertex positions
and normalized vertex normals. It also samples each scenery timeline at its start,
middle, and end, including texture offsets and animated mesh normals.
Reports list scenery clips, moving/scrolling part counts, unsupported bank versions,
atmosphere availability/status, primary fog distances/opacities, sky part counts,
original and generated normal counts,
artwork and thumbnail validation counts, stage IDs, clip counts,
and mapping limitations. `--stage` accepts the
original numeric map ID; `--asset` accepts the archive resource ID shown in brackets.
Both can also select the initial preview during normal interactive use.
In hidden scenery renders, **--time** selects seconds and **--clip** selects the
scenery dropdown index (0 is Ambient). Character renders retain their frame/clip options.
A geometry pass does not establish
that every motion or material matches gameplay. Hidden WARP captures and visual review
of representative characters are separate from physical hardware and full campaign tests.
