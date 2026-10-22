Shaolin Monks Recomp - 3D Viewer

Run ShaolinMonksViewer.exe and choose Open game source.
Select your original USA/Europe Xbox ISO/XISO, GAMEDATA.WAD, or default.xbe.
For an extracted copy, retain default.xbe and all eight GAMEDATA.WAD-WAK banks
in the same folder. No game files are included in this package.

Stage BGM starts enabled in the stage inspector. Selecting a stage plays its
original area's base music cue with the source ADX loop points. Uncheck Stage BGM
to stop it, including across subsequent stage selections. Re-enable to restart.
The Volume slider below the checkbox adjusts playback from 0 to 100%, preserving
the original cue gain. It updates live and retains its level across stage changes
and BGM off/on during the session. At 0%, playback continues silently.
Changing to another asset or source stops music. Unmapped stages report No BGM
defined for this stage and stay silent. The supported local disc maps music for
87 of 165 indexed stage meshes. Combat intensity changes and scripted overrides
are not simulated. Playback reads music directly from the original archive;
Windows XAudio2 supplies audio output without an extra codec installation.

The Characters category opens first. Select a model, choose a named animation,
and use Play, Restart, Frame, and Rate to inspect its motion. Select Bind pose
to inspect the original skeleton. Show joints draws the skeleton through the
mesh; selecting a joint highlights it. Clips start at the root over the grid
center. Centered playback preserves hip sway and vertical motion while removing
overall horizontal travel. Root motion retains the full horizontal movement
relative to that starting point. The grid stays fixed at the original rest floor
while panning, reframing, scrubbing, or changing clips.
The preview loops motion at 30 frames per second by default. Playback FPS sets
the clip clock independently of Rate; 30 FPS / 1x restores defaults. The controls
show elapsed seconds and clip duration. Playback follows elapsed time even at
low rendering rates. Gameplay speed overrides are not executed or verified.
Dynamic joints animate original flap chains with spring stiffness, damping,
mass, gravity, stretch and environment scales read from the supplied executable.
The recovered integration runs at 60 Hz, and Character Debug movement and turning
drive inertia. Gravity lets tails settle instead of holding them horizontally.
Hair and clothing roots preserve their animated transforms; simulated children
inherit the root orientation so the hair base stays attached to the head.
Pause freezes motion, scrubbing resets it, and disabling restores clip-only
motion. Isolated original-x86 traces checked spring steps and attachment matrices.
Gameplay collision volumes, wind and scripted per-joint overrides remain unsupported.
Unsupported executable layouts retain the older bounded Secondary motion preview.

Character previews include a directional light with filtered shadows, enabled
with Lighting by default. Directional shadows include alpha masking and respects
hidden geometry. Light azimuth and Light elevation adjust its direction. Characters
cast shadows onto themselves and the Character Debug checker floor. This is viewer
lighting; stage source lights and baked overlays use separate preview paths.

Short body clips and extended animation tracks use separate bone-table mappings,
including character-specific facial/finger order from the executable registry.
Variants with only a static shared reference frame default to a compatible fallback
idle. The original reference clip remains available in the animation list.

Character Debug
The Characters library replaces the Stage dropdown with an ON/OFF Character Debug
toggle. Enable it for an original checker-textured floor and black distance fog.
One checker square is one debug meter (512 source coordinate units; this is a
viewer calibration, not a verified original engine unit). Fog stays on and hides
the floor boundary. The floor follows travel while its texture stays world-aligned.
Its neutral diffuse tint subdues the checker without changing source texture pixels.
Click the canvas, then use WASD/arrows to move relative to the camera, Right Shift
to run, and Space to jump. Mouse navigation remains available. Release movement
to idle. Focus loss freezes the controller; library focus and text input do not
move the character. The camera follows the actor.
The starting debug camera looks from the character's left toward the right.
J/K/L send quick/power/launch inputs; U throws. Hold E with J/K/L for specials,
or use I for E+J. Buttons follow the selected character's original
ERG.DT2 input graph and ERG.DAT action IDs when compatible data is available.
Source child/sibling links, press/hold/forbidden masks, input timeouts, minimum
execution frames, start frames and blend durations replace name-sorted sequencing.
Repeat buttons to chain; holding does not create extra presses. Up to nine inputs
can wait for their source execution frames. Ground and airborne roots are distinct.
Authored attack root travel persists after an attack instead of snapping back.
The inspector buttons feed this input path; the move browser previews clips directly.
Target, hit, unlock and world predicates without a simulated result fail closed.
Some branches therefore remain unavailable on the empty floor. Paired victims,
hit detection, damage, collision, progression and attack effects are not implemented.
This is a recovered controller subset, not verified full gameplay parity.
Assets without a compatible graph retain labeled clip-only previews and O sequences.
Fallback and legacy entries are labeled; borrowed clips do not replace graph targets.
Use the left library to change characters without leaving debug mode. Reset
character restores the origin. Movement clips identifies each action and fallback.
Modern actor clips take priority. Short walk transitions are excluded when a full
gait exists. Legacy banks prefer retargeted fallbacks; their original clips remain
available for inspection, with legacy track mapping still unverified. Run/jump use
the shared bank. Missing unarmed idle/walk and attacks use a compatible authored
humanoid bank from the supplied game, with verified bone-name order. Shared weapon
poses are not used as unarmed movement. Fallbacks preserve the target limb lengths
and foot contact instead of stretching the rig or grounding it by hanging garments.
Missing actions on nonstandard rigs are reported:
ground actions hold idle/bind pose, and an unavailable jump is disabled.
Ground transitions blend joints over 0.16 seconds. Speed estimates use root travel or
planted-foot motion, with nominal fallbacks. Height calibration reduces floating.
Compatible input graphs use fixed 60 Hz controller updates, the source action frame
clock and the executable's ground turn limit. Movement rate starts at 1x in that
mode; clip-only fallbacks retain 2x. The slider scales walk/run travel and cadence.
Normal jumps read launch, integer gravity, early-release, and air-steering parameters
from the supported original executable. Physics runs at fixed 60 Hz; landing starts
on floor contact and accepts movement after the source cancel frame. Hold Space
for a full jump; early release shortens standing jumps. Running jumps use their own
profile. Missing source metadata disables jumping. No executable code is run.
Isolated native-routine comparisons validate normal jump motion, not full gameplay
parity. Ground gait selection, speed estimation and locomotion blends remain viewer
behavior. Gameplay combat resolution,
double/wall/long jumps, upgrades, world collisions, and articulated foot IK are not
implemented in Character Debug.
The portable button matcher passed 347,680 comparisons against the supplied game's
native x86 routine. Synthetic graph tests cover held input, buffered links, source
start frames/blends, unavailable predicates, retained attack travel and equal travel
and animation frames at 30/60/120/144 host FPS. This is not a full-game comparison.
Loops interpolate the remaining tail interval back to the first key, preserving
in-place root rebasing and secondary motion. One-shot attacks/jumps/landings hold
their last pose instead of looping.
Disabling Character Debug restores the ordinary clip/time/camera. Temporary floor
geometry and common fallback clips do not change the game files. The floor is
omitted from hierarchy selection, character framing, source counts, and FBX exports.

Select the Stages category to show the Stage dropdown for campaign maps,
versus arenas, and extra maps.
Particles (preview) reads original shared sprites and emission commands. It starts
enabled and adds inferred anchors to compact, warm emissive flame meshes. A map
without a suitable flame mesh can have no automatic emitters. Select another stage
object, choose a numbered command, and use Add at selected object to preview it.
Select its Particle emitter entry in the hierarchy to Apply command, hide it, or
Select anchor. F frames the anchor. Viewport clicks pass through particle sprites.
Scenery Play, Restart, Time, Rate, and Loop also control particles; Static pose
hides them. Seeking is deterministic and paused sprites continue facing the camera.
Additive particles contribute to bloom. Opaque geometry occludes particles and glow.
Anchors follow current mesh bounds. Preview additions are discarded when reloading
the stage; game files stay unchanged. Viewport PNGs include particles; FBX exports
omit generated particle quads.

Type in its filter to find a name or resource ID. Selecting a stage opens the
Stages category and frames the scenery from above. Empty map slots are omitted;
unnamed maps retain numeric stage IDs. Select Characters to return to the roster.
Click a stage in the left list, then use Up/Down to load the previous/next
matching stage automatically. Navigation follows the current search filter.
Stage helpers reveals temporary volumes, hidden collision objects, and legacy
backgrounds. Collision barriers can reuse wall textures and look like
solid curtains when revealed. Original triangle-strip winding is preserved for back-face culling.
Compact legacy exports without strip-direction bits or stored normals receive
connected-surface orientation
repair: broad terrain faces up, closed meshes face outward, and other components
retain the majority source orientation.
Open volumetric props can also face outward using opposing surfaces and radial
evidence; small detached details follow their shell. This is inferred orientation.
Repair legacy winding in Geometry toggles it to compare the source.
Flip object faces in Object details reverses the selected
object and its normals. These affect preview/FBX only, not original game data.
Cull back faces helps inspect interiors. Use the mouse wheel to move closer.
Open normal-less legacy shells also receive a two-sided solid preview fallback.
Authored-normal meshes, closed solids, and translucent effects keep ordinary culling.
Two-sided object preview in Object details overrides this per object. Turning off
Repair legacy winding also disables the inferred fallback for source comparison.
This does not recover the original front side or change the exported FBX faces.
F focuses the selected object or frames the visible asset when nothing is selected.

Atmosphere enables Sky domes, Distance fog, and Stage background by default.
Flagged sky meshes use their original textures, follow camera translation, and
draw behind the map. Sky visibility is separate from collision/helper visibility.
Fog preset exposes the five original presets, starting with Primary. Fog keeps
the authored color, distance range, and minimum/maximum opacity. Some presets
intentionally have no fog; missing settings are identified in the inspector.
Additive effects fade under fog without acquiring colored halos.
The current fog color supplies the fallback stage background, or black when
there are no settings. Background allows a custom color; Reset atmosphere
restores Primary, the original color, and enabled atmosphere toggles. A new
asset restores its own primary preset and clears the custom background color.
Disable Distance fog when it obscures distant geometry during inspection.
Sky rows are marked (sky) in the hierarchy. They support selection and visibility,
but sky backgrounds do not intercept viewport clicks or expand map framing.
F on a sky row frames the scenery. FBX keeps sky geometry at its stored world
position without fog; viewport PNG exports include the rendered atmosphere.

Scenery Animation starts with Ambient playback. Play pauses or resumes; Restart
returns to the start. Time scrubs in seconds, Rate changes speed, and Loop preview
repeats the timeline. Animate textures and Animate meshes work independently.
Static pose restores the original geometry and texture coordinates.
Select a named animation group to preview doors, destructible scenery, or
cutscene props while ambient tracks continue. Groups are selected manually;
the viewer does not execute gameplay triggers. The timeline uses 60 source
frames per second at 1x, with original track looping/one-shot/bounce behavior.
Additive light shafts and fog use their original texture, vertex tint, and smooth
alpha fade. Their depth-write flags are preserved, solid scenery blocks them,
and effect layers sort from far to near after the solid geometry. Gradient alpha
uses continuous blending without the cutout threshold. Ordinary translucent
layers disable depth writes; additive layers retain their source depth flags.
Vertex alpha stays active when Vertex colors disables the RGB tint. Additive layers
stay unlit under preview lighting. Hover a part for its blend/depth-write state.
Solid scenery with opaque vertices and at most 5% binary, dark transparent texels
receives an inferred RGB-only preview, preventing sparse black detail from becoming
holes in stone walls and trims. Substantial cutouts, gradients, skies, and effects
retain alpha. Use texture alpha in Object details overrides the selected material.
Transparent textures filter premultiplied colors to avoid colored edge fringes.
These controls never modify source textures. FBX preserves scalar opacity and
diffuse alpha selection, but does not recreate game-specific shaders.
Recognized light-shaft names and matching grayscale masks receive an inferred
additive preview. Fully opaque, depth-writing volumetric props retain an opaque
base even when they share an effect flag, fixing see-through fire bowls.
The inspector labels both inferences. Ordinary solid props are excluded from bloom.

Source FLICKERMIN/MAX/SPEED properties animate supported background opacity.
This keeps the scrolling fog sheet in Soul Tombs Map 1 [913], Part 109 translucent.
Animate textures controls this material animation too. Static pose or disabling
it restores phase-zero opacity. Object details reports current opacity.
Random flicker perturbations and gameplay overrides are not simulated.

Separate eye/glow masks are intentional layers in some stages. Recognized
no-depth-write StatueEyes/glow materials stay unlit and retain their source
blend modes. The inspector labels these Glow source (preview); name-based
recognition is an inference. Lighting and Bloom (preview) start enabled, with
Glow strength 0.8, Glow threshold 0, and Glow radius 1.5. These toggles remain
user-controlled across asset changes. Bloom adds a halo from these layers,
mesh-based additive fire, and recognized light shafts. Glow strength,
Glow radius, and Glow threshold adjust the effect. Opaque scenery blocks its
emission mask. Ordinary bright scenery, skies, selection highlights, and flat
image previews are excluded. PNG viewport exports include enabled bloom.
Original full-screen bloom behavior is unverified; this is a viewer approximation.

Choose Concept Art in the category menu to browse the original artwork gallery.
Select a thumbnail, filter by group, or search by page number or resource ID.
Previous and Next follow the filtered pages. Hover the image and use left/right
arrows to change pages. Drag to pan, use the wheel to zoom, and press F or choose
Fit image to see the complete page. Save artwork (.png) exports the original
decoded image at its stored resolution, including its original gallery framing.
The gallery includes character art, character renders, environments, storyboards,
and design studies. Team photos, move instructions, loading screens, and menu
atlases remain outside this section. All images come from your supplied game.

Left-click an object to highlight it in semitransparent yellow. Click it again
or click empty canvas to deselect it. F focuses the selected object's current
visible geometry while keeping the viewing direction. Clear selection in the
inspector also deselects. Loading another asset clears the selection.
Selection highlights do not change exported meshes or textures.

For 3D assets, Hierarchy occupies a full-height column to the right of the
preview, beside the inspector. The library uses the full height on the left.
Viewport selection selects and reveals the matching hierarchy row. Click an
object or material name in the hierarchy to select and highlight it in the
viewport, then press F to focus it. Expand object arrows to see material batches.
Checkboxes control visibility independently of selection: object checkboxes
affect all of their materials, while material checkboxes affect one batch.
Show all and Hide all are above the hierarchy. Clearing selection in the
viewport or inspector also clears it in the hierarchy.

3D navigation: left drag orbits, middle drag pans, and the wheel or right drag
dollies closer or farther. These controls need no keyboard modifier.
F frames the visible asset when nothing is selected. Start dragging over the
viewport; releasing the mouse button or losing window focus ends the drag.
Images use ordinary dragging and the wheel for pan and zoom.
Horizontal orbit turns the near side with the drag; panning moves the image
with the pointer.
Use the right hierarchy for visibility. The adjacent inspector scrolls to reveal
display, camera, geometry statistics, and export controls.
Switching directly between stages preserves camera position, orbit rotation,
zoom, and FOV. Initial stage entry frames the map; F or Frame asset reframes it.
Export writes binary FBX 7.4 with the current visible pose, normals, UVs, vertex
RGBA colors, materials, and separate diffuse PNG textures. Keep the PNG files
beside the FBX so relative texture paths resolve. UVs include source transforms
and current scrolling. Opaque diffuse variants use separate PNGs; source pixels
stay unchanged. Coordinates convert to right-handed Y-up with source magnitudes;
the centimeter unit declaration does not calibrate game units to physical meters.
Skeletons, animation, generated debug floors, and particle sprites are omitted.
FBX materials do not reproduce reflection/gloss passes, game-specific blend/depth
states, lighting, fog, or bloom. Selection highlighting is omitted.
Object details shows the role, object ID, render/object flags, texture slot, and
vertex-alpha range. Flagged shadow receivers use a multiplicative preview: white
preserves the floor below and dark texels darken it. Planar black vertex-alpha
shadow overlays are inferred from their vertex data, including Soul Tombs Map 2
[933], Part 146 / PillerMid. Their tint stays active with Vertex colors off;
fog fades their opacity without coloring them orange. Stored positions remain
unchanged and dynamic shadow projection is not reproduced.
Hierarchy labels identify (shadow), (camera), and (helper)
objects when metadata is available. Unflagged colored boxes/effect panels remain
unclassified; shape or color alone does not establish a collider/trigger role.
Raw texture export preserves the original alpha channel. Character diffuse
exports normally use opaque alpha because the game also uses that channel for lighting.
Substantial masks with at least 95% fully opaque/transparent texels retain cutouts,
including the filtered torn hem on Priest [6303]. These cutouts cast masked shadows;
gradient lighting alpha remains opaque. Source texture PNGs remain unchanged.
Original vertex normals provide smooth lighting and follow animated joints.
Compact scenery without stored normals uses generated normals smoothed across
UV/material seams within each object, preserving creases over 60 degrees.
This does not recover absent source normals. The inspector reports
original and generated normal counts.

Stage lighting and material channels
Modern source materials provide diffuse/reflection/gloss textures, independent UV
offsets/scales and U/V wrap/clamp. Textures use up to 8x anisotropic filtering and
generated mipmaps; alpha filtering uses premultiplied color. Particle frame atlases
keep separate gutters and level zero. Original exported texture pixels do not change.
Reflections use sphere-map coordinates, a half-strength blend and a gloss mask;
these coordinates and the full Xbox shader pipeline are not exact reconstructions.
Object details reports channel slots, source flags, addressing and lighting mode.

Stage lighting lists original point, spot and directional lights. Source lights
starts on; Light records can disable individual lights, and their tooltips show
position, color and near/far ranges. Show light positions draws colored markers.
Normal-bearing scenery uses up to four nearby point/spot and two directional lights
per batch. Selection, cone shading and ambient response remain preview approximations.
Compact scenery retains baked vertex illumination without an extra generic light.
Disable both Lighting and Vertex colors to inspect untinted diffuse textures.
Original light records do not generate new stage shadow maps. Baked shadow overlays
and character directional shadows retain their separate rendering paths.

Preview limits
- This is an asset inspection tool. It does not execute gameplay,
  animation events, complete gameplay cloth behavior, or every original shader pass.
  Dynamic flap joints use recovered spring defaults; gameplay collision volumes,
  wind fields, and scripted spring overrides remain unsupported.
- Particle playback is a bounded billboard preview of source command settings,
  with inferred or manually selected placement. Supported velocity/gravity uses
  the recovered hundredths scale and discrete 60 Hz steps, compared with 120
  isolated original-x86 positions across both update paths. Flagged size/color,
  delay/ease-in, supported variation and per-particle texture frames are handled.
  Alpha, additive and reverse-subtractive blends are supported. Full gameplay
  emission scheduling is not verified. The budget
  is 64 emitters and 64 particles per emitter, with at most 4096 spawn candidates
  per emitter per sample. Gameplay emitter scripts, attachment history,
  collision/bounce/friction, velocity scaling modes, specialized draw modes
  and remaining command flags are not reproduced. Existing fire/smoke
  mesh animation continues separately.
- Alternate models can use a shared skeleton's animation bank. These are labeled.
- Centered playback estimates travel from the clip endpoints. It does not lock
  feet or reproduce actor movement supplied by gameplay. Walks authored in place
  can still slide against the grid; Root motion shows all stored clip movement.
- Bone index mappings for two legacy rigs are marked as preview mappings.
  Their entire clip sets have not been visually verified.
- Some unused characters have placeholder textures or generic resource names.
- Detached parts, weapons, and transformation meshes can be hidden initially.
  Use the part checkboxes or Show all to inspect them.
- Stage previews animate original U/V scrolling and version 4 map mesh tracks.
  Legacy version 2/3 map banks remain static and are identified in the inspector;
  their texture scrolling can still play. Some tracks target objects outside
  the exported mesh collection. Procedural shader animation, non-particle texture flipbooks,
  random UV modes, complete original particle simulation, gameplay triggers and objects, event scripts,
  procedural sky changes and complete original lighting are not reproduced. Some unidentified
  helper geometry can remain visible; use the part controls to inspect it.
- Flagged sky domes render around the camera. Unflagged mountain backdrops
  retain their stored world positions and can still appear detached.
  Automatic per-object/region fog changes and scripted atmosphere transitions
  are not reproduced; fog previews use the manually selected global preset.
  Sky rendering does not reproduce every original material or procedural effect.
  Lava barriers and light/shadow overlays can differ from gameplay because event
  state and some original material passes are outside the diffuse preview.
- Other prop placement is approximate. Some material passes remain simplified.
- Additive light/fog blending previews the stored mesh flags. Optional bloom uses
  a viewer emission mask and blur. Full game shaders, lighting, post-processing,
  and tone mapping are not reproduced. Transparent foreground layers attenuate
  the glow mask in depth order. FBX does not encode the
  viewer's blend equations or depth-write states.

The viewer reads the source directly without extracting or changing it.
It does not read or write game saves, settings, or controller assignments.
Only explicitly selected exports and diagnostic output files are written.
Windows x64 and Direct3D 11 are required. Software rendering is a fallback.

Tools and licensing
The viewer uses C++20, Windows APIs, Direct3D 11, Dear ImGui, miniz, and the
read-only disc reader from the project's pinned XboxXBERecomp revision.
Project source is licensed GPL-2.0-or-later. See licenses/LICENSE.txt and the
included third-party notices. The project license does not grant rights to
Mortal Kombat: Shaolin Monks, its executable, names, artwork, or other assets.

Source: https://github.com/OAleex/ShaolinMonksRecomp
Build instructions: docs/VIEWER.md in the source repository.
