# Implementation log

## Skeletal rigging and skinning

- Added a rig editor for creating and naming joints, changing their parent and rest positions, posing, setting keys, and binding and editing skin weights.
- Connected articulated meshes to the viewport, scene bounds, ray tracing, mesh export, linked instances, and timeline. Added native rig persistence and joint tracks in the Curve Editor.
- Passed the Windows core suite and Linux-target syntax checks. Verified skeleton creation, binding, pose edits, and joint curve keys on the Windows desktop. Rendered different poses from two frames of a reloaded scene.

## Mesh components and animation curves

- Added a mesh editor with vertex, edge, and face selection, local transforms, face region extrusion, subdivision, face deletion, winding reversal, and planar UV projection.
- Added procedural solid conversion, including exact cube topology, and connected mesh edits to scene undo, rendering, and persistence.
- Added a transform curve editor with graph scrubbing, key value dragging, retiming, copying, and interpolation controls. The editor samples the same interpolation as playback.
- Passed the Windows core suite and Linux-target syntax checks. The Windows desktop check converted a cube, extruded a face, subdivided the mesh, and saved two transform keys after a graph edit.

## Simulation presets and animation clips

- Added Water, Oil, Syrup, and Lava with distinct materials and flow settings. Added a 512-particle detail mode and faster liquid neighbor searches.
- Added rigid-body materials, stronger collision detail settings, planetary gravity, custom direction, zero and reverse gravity, and adjustable radial attraction and repulsion.
- Added starter objects, background baking, baking from the current frame, and a one-click playback preview that ends at the last baked frame.
- Preserved surrounding rigid-body animation keys and delayed liquid visibility until the chosen start frame. Improved rolling orientation and elapsed-time playback.
- Extended the agent recipe schema to expose presets, clip timing, gravity fields, and tuning.
- Passed the Windows core and MCP checks, plus Linux-target C++17 syntax checks. Verified a 512-particle desktop bake from frame 18 through 37 and rendered a saved clip before, at, and after its start.

## Physics and liquid simulation

- Added a rigid-body solver with gravity, sphere and box contacts, terrain collision, bounce, friction, and rolling spheres.
- Added a particle liquid solver with adjustable viscosity and a ray-traced metaball surface. Added group bounds to skip liquid intersection work on rays that miss the volume.
- Added **Edit → Simulation** and recipe operations for both solvers. Each bake produces editable animation keys, supports scene undo and save, and works with timeline playback and animation rendering.
- Passed the Windows core suite, desktop bake checks, MCP composition, and Linux-target C++17 syntax checks. Rendered distinct first and later frames from a saved simulation scene.

## Viewport transforms and symmetry

- Added axis constraints, automatic Move axis locking, grid and surface snapping, precision dragging, and cancellable Move, Rotate, and Scale gestures.
- Added five pivot choices, a placeable 3D cursor, and selection centering and grid alignment commands.
- Added mirrored object duplicates around the chosen pivot, corrected mirrored mesh export winding, and added bidirectional terrain symmetry with local undo.
- Passed the Windows core suite, desktop input checks for snapping, cancellation, mirroring and centering, and Linux-target C++17 syntax checks.

## Agent authoring and priority 9 and 8 work

- Added scene recipes, a discoverable catalog, scene inspection, and rendering through both a Python CLI and local MCP. Added an editable example recipe and an integration smoke test.
- Added independent bump and transparency sources for texture layers and material baking from an individual layer.
- Added up to four diffuse bounces and the original Sundial Basin example with an embedded HDR environment, layered rock, directional waves and camera animation.
- Added terrain shape keys, key retiming, seamless border blending, and linked terrain grids through recipes.
- Added mixed-species scatter, instance erasing and surface alignment that follows terrain edits.
- Limited smooth and erosion brush copies to the affected terrain patch. A Windows run exercised 24 strokes on a 4097-point grid and benchmarked a 2,062-object render plus cancellation.
- Added a BR7 container diagnostic and checked it against a public sample. Native scene and preset decoding still require verified record layouts and reference fixtures.
- Added version 6 scene and library records and version 3 texture presets. Older formats still load.
- Passed the Windows core suite, MCP integration test, desktop smoke test, and Linux-target C++17 syntax check. Rendered the embedded HDR example and a version 5 scene.

## Bryce 7.1 workflows

- Refreshed the remaining backlog with concrete compatibility, fidelity, library and validation gaps and recorded the checks already performed.
- Added a reproducible Scatter Brush with density, radius, rotation, scale variation and normal alignment. Copies share source geometry and follow receiver transforms. Terrain placements also follow height edits.
- Added Radiance RGBE maps for HDR backgrounds, reflections and importance-sampled illumination. Sky Lab controls rotation, intensity, sample count and background visibility. Scene packaging and embedding preserve HDR assets.
- Added indirect diffuse lighting, grouped dome, cluster and fill rigs, and per-light controls for diffuse strength, specular strength and group influence.
- Expanded texture layers with ridged and cellular functions, coordinate warp, noise octaves, level shaping, new blend operations and elevation and slope masks.
- Added terrain image blending with grayscale masks, material-to-height baking, 16-bit PGM and grayscale TIFF, a 4097-point grid and keyable surface smoothing. Large heightfields calculate normals on demand, and terrain history has a memory budget.
- Expanded appearance keys to cover light settings and material absorption and volumes. Added camera paths using the same camera interpolation as playback, plus a crop overlay in the safe frame.
- Added Pine, Oak and Willow starting shapes with adjustable branch lift, taper and leaf geometry. Added the original Pines at the Inlet example.
- Added a water editor with level, directional animated ripples, wavelength, wave height and presets.
- Preserved OBJ UVs and normals through import, scene persistence and export. Added Mesh UV material mapping.
- Added shared-folder animation rendering with per-frame claims, PNG output, optional render passes and restart support.
- Added scene and library version 5 and texture preset version 2 for these settings. Legacy files remain readable.
- Verified the Windows regression suite, desktop startup, a legacy v4 render, the new forest example, and two concurrent animation workers. A local shared-folder test rendered 2,058 linked objects with alpha, depth and object ID outputs and resumed an interrupted frame. Linux x86-64 C++17 syntax checks passed.

## Brycescape

- Renamed the studio, build targets, launcher and project documentation to **Brycescape**. Linux migrates the existing material and sky library on first startup.
- Replaced the original backlog with a scored Bryce 7.1 feature-gap inventory, retaining partial and unverified work and linking completed changes to this log.

## P1 · 6. DXF import

- Added ASCII DXF imports for 3DFACE and polyface geometry with layered objects, unit scaling, up-axis conversion and file diagnostics.

## P1 · 7. Geometry export

- Added **Edit → Export Mesh** for OBJ and ASCII DXF. It exports world-space selected geometry, samples implicit and Boolean surfaces, and triangulates terrain and lattices.

## P1 · 8. Hierarchies

- Added parent links and an indented scene hierarchy. Parenting preserves the current pose and propagates parent transforms through nested objects and animation.
- Scene loading checks for invalid or cyclic links. Merge, duplicate and delete remap hierarchy links and preserve detached objects' world poses.

## P1 · 9. Motion editing

- Added a Motion Editor for object and camera keys, editable path points, and per-key spline, linear, smooth and hold interpolation.
- Added appearance keys for layered materials, lights, terrain shape, visibility and picture content. The viewport displays object motion paths.

## P1 · 10. Terrain editing

- Added 1025 and 2049 point grids, a rectangular brush mask, terrain region copy/paste, and local undo/redo for strokes and terrain edits.
- Added seeded fractal frequency, octave and erosion density controls, plus height gain, low-height clipping and a solid terrain base.
- Symmetric lattices have their own editor title and fractal workflow. Brush updates recompute normals near the changed region.

## P1 · 11. Boolean workflow

- Added nested Boolean groups with positive, negative or intersection roles, lattice operands, and a cutter-material option.
- Added **Bake Mesh Copy** for selected geometry and Boolean assemblies. Empty results report a diagnostic.

## P1 · 12. Scene and object library

- Added a thumbnail library for object selections and scene fragments, including terrain, trees, imported meshes and Boolean assemblies.
- Assets can be imported, inserted into the scene, or exported as standalone scene files.

## P2 · 13–16. Lighting, volumes and atmosphere

- Added volume density, noise, absorption and single scattering for closed primitives, with volumetric attenuation on shadow rays.
- Added per-light falloff, range, shadow settings, disk sampling, emitter visibility and spotlight gels.
- Added nested dielectric handling, total internal reflection, absorption and configurable ray depth and transparent shadows.
- Added cloud altitude, thickness and shadow controls, moon and stratus tint, and independent sky lighting.

## P2 · 17. Production rendering

- Added variance-based premium sampling, an optional edge-aware cleanup pass, adjustable seeds and render regions.
- Added resumable interactive still renders, progress estimates, and manifest queues in the desktop and headless renderer.

## P2 · 18. Render output

- Added PNG and TIFF exports with optional alpha, depth and object ID output. Animation filenames accept a `{frame}` placeholder.

## P2 · 19–21. Viewport and cameras

- Added a floating display palette, contextual controls, and a framed viewport style.
- Added depth-cued wireframe, bounding boxes, solid and texture previews, render display, and an independent preview-resolution control.
- Added named cameras, active-camera selection, orthographic projection, field-of-view presets, editor view matching, look-at constraints and a safe-frame overlay.

## P2 · 22–23. Scene management and history

- Added object filtering, inline hierarchy renaming, drag reorder and Alt-drop parenting, matching selections, isolation, geometry statistics and persistent linked geometry copies.
- Added named scene undo/redo entries and persistent undo/redo for changes to the materials and skies library.

## P2 · 24. Command discovery

- Added an **F1** command palette and modifier hints for common tools.

## P3 · 25. Image compatibility

- Added PNG, JPEG, TGA and TIFF loading for picture cards, materials, height maps and light gels. Picture cards use imported alpha as well as chroma keys.

## P3 · 26. Scene persistence

- Added v4 scene records with legacy loading, atomic saves, relative image references, missing-asset relinking, and image reload.
- Added autosave recovery, scene titles and descriptions, and thumbnail sidecars. Scenes can collect images in a portable folder or embed them in the scene file.

## P3 · 27. Presets and example scenes

- Added badlands, salt flats, and glacial bowl terrain families, a cloud shelf sky, and three original example scenes with a cutout marker, nested Booleans, and mist.
- Added Salt Crust under Terrains and Basin Mist and Volcanic Smoke under Atmosphere.

## P3 · 28. Performance

- Added a scene BVH, nearer-first mesh traversal, heightfield range rejection, and local brush-normal updates.
- Added resumable row tracking, adaptive sampling, and lower-cost viewport previews. Scene image caches can be refreshed from Commands.

## Verification

- The Windows core suite passed with zero failures. The renamed launcher rendered the new examples to PNG and exported a queued render to TIFF.
- All Linux source files passed x86-64 C++17 syntax checks against GTK 3 and LibTIFF headers. A live Linux launch remains unverified.

## Native Linux desktop

- Added a GTK 3 desktop backend for the shared editor on Linux. It handles the canvas, keyboard and mouse input, and native file and color dialogs.
- Added CMake build and test targets, plus build instructions for Linux Mint, Ubuntu, and Arch Linux.
- Shared the headless renderer entry point across both platforms. Quoted paths with spaces work in command-line renders and scene exports.
- Store the Linux material and sky library in the user's XDG config directory.

## Project name and README

- Named the studio **Two Hoodoos and a Floppy** in the README and desktop window. The README covers setup, editing, rendering, and project checks.

## P0 · 1. Metaballs

- Added a creatable metaball with editable placement, influence radius, strength, sign, and material.
- Grouped balls form a shared surface. Subtractive balls carve the field, and existing Boolean operands can add to, cut, or clip the result.
- Scene files save the new settings. Legacy v1 and v2 scene files retain their original object IDs.
- Added regression cases for blending, subtraction, clipping, and scene round trips.

## P0 · 2. Picture objects

- Added **Create → Picture** to import a BMP as a two-sided card at its original aspect ratio.
- Added a fixed-plane or camera-facing mode, adjustable chroma cutouts, and separate shadow and reflection visibility.
- Scene files retain the image path and card settings. The Picture Editor can replace or relink the source BMP.
- Added regression cases for cutouts, aspect ratio, billboard orientation, ray visibility, and scene round trips.

## P0 · 3. Layered materials

- Added ordered texture layers with per-layer mapping, scale, offset, rotation, opacity, and Mix, Multiply, or Add blending.
- Layers can use their luminance, inverse luminance, or the previous layer as a mask. Each layer has independent Color, Bump, Specular, Reflection, Transparency, and Ambient influence.
- The Materials Lab previews the composed material and lists the selected layer's affected channels. Individual layers can be saved and loaded as `.c3tex` presets.
- Scene files and the material library store layers. Added regression cases for blending, masking, and persistence.

## P0 · 4. Direct object manipulation

- Added a selection cage with viewport drag handles for Move, Rotate, and Scale. Handles use the same transforms and undo history as the Edit tools.
- Added contextual **A** Attributes, **M** Material, and **E** Edit controls and keyboard shortcuts.
- Added **Frame Sel** and **Frame All** to the view palette. Alt-click can select one grouped object, including a metaball by its center.

## P0 · 5. Object-specific editors

- Added a Stone Editor for seed, irregularity, blockiness, and smoothing, plus a Torus Editor for both radii, tube height, and capped arcs.
- Added a Light Editor for color, power, shadow size, and spotlight cone settings. Spot and directional lights have a viewport aim handle.
- Added an imported-mesh editor for flat or smooth shading, base color, diffuse strength, and mesh counts.
- Connected the picture and metaball editors to the same **E** control. Added regression cases for the new geometry, shading, and scene settings.
