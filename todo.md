Here’s the Bryce 2 parity backlog, ordered by how much each item affects the feeling and capability of a real clone. This is based on the current source implementation, not a runtime test.

## P0 — Missing Bryce 2-defining features

1. Metaballs
   - Missing entirely from ObjType.
   - Bryce 2 introduced metaballs as a major new creative object type.
   - Need: positive/negative metaballs, blending strength/radius, grouped-field evaluation, editable placement, material assignment, and rendering/Boolean interaction.
   - This is probably the single most visible feature gap.
2. Picture objects / 2D image planes
   - Current BMP support is only for material images and terrain heightmaps.
   - Bryce 2 supports freestanding 2D picture objects in a scene—useful for signs, trees, clouds, background cards, cutouts, etc.
   - Need: picture object type, image import, transparency/alpha or chroma handling, billboard/plane modes, aspect preservation, shadow/reflection controls.
3. Deep Texture Editor-style materials
   - Current materials are a good “single procedural texture + surface parameters” system: TexType, two colors, mapping, bump, reflection, transparency, etc. scene.h (src/scene.h:15)
   - Bryce’s defining material workflow is deeper: layered texture components/channels that can independently drive color, bump, specularity, reflection, transparency, ambience, and blending.
   - Need:
     - Texture component graph/layers rather than one TexType.
     - Per-channel texture influence.
     - Blend/mix operations and masks.
     - Component transforms and coordinate sources.
     - Saveable texture presets separate from materials.
     - A material preview that identifies which channels a component affects.
4. Bryce-style direct object manipulation (“metatools”)
   - The current editor has normal transform tools, object list, side panel controls, and camera navigation.
   - Bryce’s identity comes largely from controls around the selected object: visual move/rotate/resize widgets, object-specific mini buttons, and the unusual palette-first interface.
   - Need:
     - Selection bounding box with direct manipulators.
     - Visible on-canvas movement/rotation/scale controls.
     - Contextual A, M, and E object controls.
     - Object-centric controls rather than primarily form-style side panels.
     - Bryce-like navigation/display palette behavior.
5. Object-specific editors
   - Terrain and lattice have real editing support, which is excellent. main.cpp (src/main.cpp:1631)
   - The original workflow expects certain objects to open dedicated editors.
   - Need dedicated editors for:
     - Stones: irregularity, seed, shape, smoothing.
     - Torus: major/minor radius and section controls.
     - Lights: dedicated light controls and visual handles.
     - Imported geometry: basic smoothing/material treatment and object properties.
     - Picture objects.
     - Metaballs.

## P1 — Major feature-parity gaps

6. DXF import compatibility
   - The app currently imports Wavefront OBJ only. mesh.cpp (src/mesh.cpp:113)
   - Bryce 2’s documented model-import workflow centered on DXF.
   - Need: at least ASCII DXF mesh import, layer-to-object handling, scale/orientation options, and useful import diagnostics.
   - Keep OBJ; add DXF for historical parity.
7. Geometry export
   - There is no model export path.
   - Need: export selected objects/groups or converted Boolean results to OBJ; DXF export if strict Bryce 2 compatibility matters.
   - This is lower priority than import, but important for an actual production tool.
8. True object hierarchies / parenting
   - Current grouping supports selections and Boolean grouping, but not a persistent transform hierarchy.
   - Need:
     - Parent/child relations.
     - Nested groups.
     - Transform propagation.
     - Hierarchy panel/tree.
     - Parent-aware animation.
   - This becomes necessary for rigs, orbiting objects, grouped motion, and complicated assemblies.
9. Broader animation coverage
   - Current keyframes cover object transforms, camera, and sky. scene.h (src/scene.h:101)
   - Missing or weak animation targets:
     - Material values and texture transforms.
     - Light color, intensity, shadow properties, and spot settings.
     - Terrain shape/filter parameters.
     - Object visibility.
     - Picture-object content/placement.
     - Parent-child animation.
     - Editable motion paths.
     - Interpolation choices/easing.
   - Need an actual motion editor, not only a timeline and key buttons.
10. Terrain editing parity

- This is one of the strongest areas already: painting, erosion, smoothing, sharpen, noise, terraces, crater, import/export heightmaps, and several generators. main.cpp (src/main.cpp:1667)
- Improvements needed:
  - Larger resolutions for very large scenes.
  - More terrain selection/masking tools.
  - Copy/paste regions.
  - Undo at terrain-stroke level.
  - More terrain-specific fractal controls, not just named generator presets.
  - Better erosion controls and deterministic settings.
  - Terrain clipping/base/thickness controls.
  - A more explicit symmetric-lattice workflow.

11. Boolean workflow refinement

- Current Boolean modes and grouped render-time CSG are present, which is very Bryce-like. scene.cpp (src/scene.cpp:468)
- Improve:
  - Clear visual display of positive, negative, intersecting, and neutral parts.
  - Nested Boolean groups.
  - Boolean support for more relevant object types, especially metaballs/lattices where appropriate.
  - “Convert to mesh” or bake Boolean output.
  - Better diagnostics for invalid/empty Boolean groups.
  - Material behavior rules at Boolean intersections.

12. Scene/object libraries

- Materials and skies can be saved to the local library.
- Missing equivalent libraries for:
  - Objects.
  - Boolean assemblies.
  - Terrain presets.
  - Trees/plants.
  - Complete scene fragments.
  - Imported meshes.
- Need thumbnail-driven browse/import/export workflows.

## P2 — Rendering and visual-quality improvements

13. Volume materials

- Current cloud handling exists, but it is not a general volume-material system.
- Need volumetric density, scattering, absorption, soft volumetric shadows, and objects such as fog banks/smoke/cloud volumes.
- This is important to Bryce’s atmospheric look.

14. Lighting fidelity

- Point, spot, and directional lights exist. scene.h (src/scene.h:78)
- Improve:
  - Light falloff choices.
  - Light visibility controls.
  - Better area-light treatment.
  - Colored/translucent shadows.
  - Light gels/projected images.
  - Per-light shadow quality.
  - Ambient/sky-light separation.
  - More reliable soft-shadow sampling.

15. Reflections, refraction, and transparency

- The renderer has reflections, IOR, blur, and transparency. render.cpp (src/render.cpp:306)
- Improve:
  - Nested transparent-object handling.
  - Total internal reflection.
  - Fresnel reflection.
  - Better absorption/tint through glass.
  - Transparent-shadow controls.
  - Reflection/refraction depth limits configurable by the user.
  - More predictable blurred reflection/transmission quality.

16. Atmosphere and clouds

- Sky controls are already broad: haze, fog, clouds, stratus, stars, moon, rainbow, sun, and ambient controls. scene.h (src/scene.h:28)
- Improve:
  - More convincing cloud layers and depth.
  - Separate cloud altitude/thickness controls.
  - Cloud shadowing.
  - More natural horizon/atmospheric perspective.
  - Better moon/planet controls.
  - Independent sun and cloud coloration.
  - HDR-like sky/environment contribution if modernizing beyond strict Bryce 2.

17. Anti-aliasing and production rendering

- Draft/Normal/Premium modes, progressive rendering, and ray-per-pixel control are already good foundations.
- Improve:
  - Adaptive sampling based on pixel variance.
  - Denoising or cleaner accumulation.
  - Render-region support.
  - Render queue/batch rendering.
  - Render cancellation/resume.
  - Deterministic seeds.
  - Better status estimates and remaining-time display.
  - More output formats than BMP.

18. Render output

- The project currently centers on BMP output.
- Need: PNG at minimum; ideally TIFF for lossless production output.
- Add alpha-channel output, naming templates for animation frames, and optional depth/object-ID masks.

## P2 — Interface and workflow improvements

19. Closer visual/UI fidelity

- The app uses familiar Bryce labels—Create, Edit, Sky & Fog—but it does not yet evoke the historical interface strongly enough.
- Improve:
  - Compact floating palettes.
  - Bryce-like icon language.
  - Context-sensitive palette expansion.
  - Better thumbnail browsers.
  - Distinct object control widgets.
  - Less conventional “property panel” presentation.
- The original interface is a major part of what users remember.

20. Viewport display modes

- Need clearly selectable:
  - Wireframe.
  - Solid/shaded preview.
  - Texture preview.
  - Rendered preview.
  - Depth-cued wireframe.
  - Bounding-box display.
  - Object visibility/lock indicators.
- Add useful display-quality controls so dense scenes remain responsive.

21. Camera workflow

- Current camera supports orbit/pan/zoom and saved view memories.
- Improve:
  - Explicit camera objects.
  - Camera list and active-camera choice.
  - Orthographic/perspective options in the same camera model.
  - Focal length/field-of-view presets.
  - Safe-frame/crop overlays.
  - Camera animation paths.
  - Camera matching/look-at constraints.

22. Selection and scene management

- Improve:
  - Search/filter object list.
  - Rename in place.
  - Drag/drop ordering and hierarchy management.
  - Isolate/hide selected.
  - Select by type/material/group.
  - Lock and visibility batch controls.
  - Object statistics and memory/triangle reporting.
  - Duplicate/link instances.

23. Undo/redo robustness

- The UI advertises undo, but a Bryce-scale editor needs granular, reliable history.
- Need undo/redo for:
  - Transform drags.
  - Terrain strokes.
  - Material edits.
  - Sky changes.
  - Library operations.
  - Object creation/deletion/import.
  - Boolean/group changes.
- Add a change description so undo history is understandable.

24. Keyboard shortcuts and discoverability

- Need a documented shortcut map, tooltips that explain modifiers, and a command/menu structure.
- Especially useful for group, Boolean mode, transform mode, views, render, terrain brush size, keyframes, and labs.

## P3 — Robustness, compatibility, and content

25. Broader image support

- Current image loading is BMP-focused.
- Need PNG/JPEG/TGA at minimum for materials, picture objects, and terrain maps.
- Preserve image path handling, missing-file reporting, and optional embedded assets.

26. Scene-file resilience

- .c3d persistence is already implemented.
- Improve:
  - Versioned migrations.
  - Relative asset paths.
  - “collect assets” packaging.
  - Missing-asset relinking.
  - Autosave/recovery.
  - Scene thumbnail/metadata.

27. More authentic content libraries

- Existing presets are useful, but Bryce is also remembered for immediately explorable libraries.
- Add:
  - More terrain families.
  - Boolean construction examples.
  - materials categorized by use and look.
  - sky/environment presets.
  - picture-object examples.
  - scene templates modeled after classic Bryce workflows.

28. Performance work

- Large terrain meshes, high-res heightfields, soft shadows, reflections, and imported OBJ meshes will expose limits quickly.
- Need:
  - Acceleration structure updates for meshes and scene objects.
  - Efficient heightfield traversal.
  - Render-thread cancellation safety.
  - Cache invalidation rules.
  - Preview-quality reductions independent of final-render settings.

## Recommended implementation order

1. Metaballs.
2. Bryce-style selection/metatools and object-specific editors.
3. Deep/layered material system.
4. Picture objects and broader image formats.
5. DXF import and mesh export.
6. Hierarchy + expanded animation/motion editor.
7. Terrain/editor refinements.
8. Renderer fidelity: volumes, transparent shadows, better refraction.
9. UI/palette fidelity and libraries.
10. Production workflow: PNG/TIFF, queues, autosave, asset packaging.

The project is already strongest where Bryce 2 was strongest—terrain, sky, materials, Boolean scenes, and rendering. The biggest thing separating it from a true clone is not “more primitives”; it is recreating Bryce’s distinctive metaball + deep-material + direct-manipulation palette workflow.
