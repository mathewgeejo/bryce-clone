# Bryce 7.1 feature gaps

This backlog tracks missing and partially implemented capabilities identified by comparing Brycescape's source with Bryce 7.1 Pro documentation. Completed work is recorded in [CHANGELOG.md](CHANGELOG.md). Scores range from **10** for the greatest impact on core landscape creation to **1** for specialized workflows. Items marked **Investigate** need a closer comparison before their exact scope is established. This is a working inventory, not a claim that every Bryce feature has been audited.

## Priority 10

- [ ] **Instancing and vegetation scattering** — Add a surface-painting workflow for distributing linked objects over terrain and other surfaces, with density, scale, rotation, alignment and reproducible randomness. Support editing the source and efficient rendering of large populations. Linked geometry copies already exist. [Bryce instancing guide](https://www.daz3d.com/forums/discussion/3381/how-to-use-the-instancing-lab-in-bryce-7-1-pro-a-complete-tutorial)
- [ ] **HDR image-based lighting** — Load HDR environment maps and use them for background, illumination and reflections, with rotation, intensity and sampling controls. Brycescape currently has procedural sky lighting. [Daz Bryce overview](https://www.daz3d.com/bryce-overview)
- [ ] **Deep procedural texture editing** — Extend the existing layered materials toward a richer component editor with procedural functions, compositing and controls for terrain-driven materials. Investigate the individual Bryce 7.1 functions and mappings before specifying exact parity. [Bryce terrain and material examples](https://free.downloads-cache.daz3d.com/bryce-pro-landscapes)

## Priority 9

- [ ] **Terrain editing depth** — Add terrain blending using masks and heightmaps, improved selection and filter controls, and high-resolution workflows. Investigate 16-bit heightmap precision and 4096-square terrain support, including memory and performance limits. Existing terrain tools support up to 2049-point grids. [Daz Bryce product page](https://www.daz3d.com/bryce-7-pro?_m=d), [Bryce 4096-square heightmaps](https://www.daz3d.com/bryce-7-pro-tileable-high-resolution-terrains-and-matched-materials)
- [ ] **Bryce file and preset compatibility** — Investigate reading Bryce scene files and material, object and sky presets, including `.br7`, `.mat`, `.obp` and `.bsk`. Document format limitations and validate imports against real files. Brycescape currently uses its own scene and library formats. [Daz preset installation guidance](https://bugs.daz3d.com/hc/en-us/articles/207528793-How-to-Install-Bryce-7-Pro-using-installers-from-Product-Library)
- [ ] **Advanced lighting** — Investigate and implement comparable true ambience, diffuse lighting, light domes, clusters and fill lights. Compare indirect illumination, shadow quality and render speed with reference scenes. Basic lights, sky light and soft shadows already exist. [Daz Bryce overview](https://www.daz3d.com/bryce-overview)
- [ ] **Desktop validation** — Exercise core editing, saving, rendering and recovery on Windows and on Linux Mint, Ubuntu and Arch, including Wayland and X11 where available. The Windows core suite passed and Linux source passed syntax checks. Live Linux desktop behavior remains unverified.

## Priority 8

- [ ] **Vegetation editor and library** — Expand the existing tree generator with a broader range of species, foliage shapes, branching controls, presets and reusable assets. Compare Tree Lab controls and generated results with Bryce 7.1. [Daz vegetation resources](https://www.daz3d.com/bryce-7-1-pro-vegetation)
- [ ] **Animation and motion editing** — Audit the Advanced Motion Lab and extend the existing motion editor where needed, including camera path visualization, terrain filter animation and additional light and material parameters. Verify interpolation and parented motion with reference scenes. [Daz Bryce help topics](https://bugs.daz3d.com/hc/en-us/sections/201707363-Bryce)
- [ ] **Atmosphere and cloud fidelity** — Improve cloud layers, horizon perspective and volumetric quality, then compare images and controls against Bryce 7.1. Altitude, thickness, tint, shadows and basic volume materials already exist. [Daz Bryce overview](https://www.daz3d.com/bryce-overview)
- [ ] **Material and terrain integration** — Add more capable slope- and altitude-driven material blending and workflows for coordinating terrain heightmaps with materials. Verify existing altitude textures before defining the remaining controls. [Daz terrain material examples](https://free.downloads-cache.daz3d.com/bryce-7-pro-ground-texture-tiles-and-matching-terrain)
- [ ] **Large scene performance** — Profile and improve high-resolution heightfield traversal, dense imported meshes, many linked objects, volume rendering and cancellation responsiveness. Test memory usage and cache invalidation under sustained editing and rendering.

## Priority 7

- [ ] **Water editing** — Compare Bryce's water workflow with the existing water primitive and wave materials. Add missing controls and presets for shaping and shading water based on that comparison. [Daz landscape tools](https://www.daz3d.com/blog/bryce-7-pro-and-ultrascenery-2)
- [ ] **Content library** — Create additional original terrain, material, sky, vegetation and scene presets with previews. The existing asset library and examples provide the starting point.
- [ ] **3D interchange** — Compare Bryce 7.1's supported import and export formats with Brycescape's OBJ and ASCII DXF support. Prioritize useful missing formats and preserve materials, hierarchy and scale where supported. [Daz export guidance](https://bugs.daz3d.com/hc/en-us/articles/207528323-What-3D-Object-File-Formats-can-Bryce-Export)
- [ ] **Rendering quality and output validation** — Compare representative glass, volumetric, soft-shadow, reflection and animation renders against reference scenes. Improve sampling and blurred transmission as findings require. Verify alpha, depth and object ID passes on complex scenes.
- [ ] **Camera and viewport workflows** — Investigate scene camera objects, camera path display and crop overlays. Named cameras, projection options, editor-view matching, look-at and safe frames are already implemented.
- [ ] **Undo and recovery coverage** — Exercise terrain strokes, material and sky edits, library operations, imports, groups, parenting and autosave recovery through undo, redo and restart. Fix any inconsistent or incomplete history.

## Priority 6

- [ ] **Daz Studio interoperability** — Investigate a practical import or bridge workflow for compatible characters and props, including materials and animation where feasible. Confirm platform and version constraints before implementation. [Daz Bryce overview](https://www.daz3d.com/bryce-overview)
- [ ] **Distributed animation rendering** — Add a coordinator and worker workflow for rendering frames across multiple machines, with job tracking and recovery. The existing queue runs locally. [Daz Bryce Lightning documentation](https://bugs.daz3d.com/hc/en-us/articles/207528793-How-to-Install-Bryce-7-Pro-using-installers-from-Product-Library)
- [ ] **Interface and discoverability** — Review palette behavior, tooltips, shortcuts and object controls against the documented workflows. Refine layout and accessibility while retaining Brycescape's own visual identity.

## Priority 5

- [ ] **Exhaustive compatibility audit** — Work through the Bryce 7 Artist Guide and Daz help topics feature by feature, record missing controls and formats, and split this backlog into independently testable tasks. Check availability and behavior against an installed copy when possible. [Bryce 7 Artist Guide](https://digiacademy.org/MM/Ch6/bryce7/bryce_7_artist_guide_wip0913.pdf)
