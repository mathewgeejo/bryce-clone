# Bryce 7.1 feature gaps

## Production modeling and animation

- [ ] Reusable component selections and large mesh editing.
- [ ] Expanded topology operations including bevel, symmetry, and cleanup. Convex face inset, quad loop cuts, vertex welding, planar boundary filling, equal-count boundary bridging, and edge-to-edge knife paths are implemented.
- [ ] Extend the bridge to boundaries with different edge counts and add drag-based knife cuts with optional cut-through.
- [ ] Modifier evaluation, richer UV editing, multiple material assignments, and interchange of these attributes.
- [x] Skeleton creation, joint posing, skin binding, weight editing, and deformation through playback, save, and render.
- [ ] Rig editing depth including weight painting and mirroring, bone tools, joint-local orientation controls, and larger deformation benchmarks.
- [ ] Character animation with additional constraints and retargeting between different skeleton conventions. Two-joint IK, action capture, timeline clip blending and baking, and same-hierarchy action transfer are implemented.
- [ ] Independent animation channels, curve handles, graph navigation, multi-key operations, and curve validation.

The scores reflect impact on landscape work, from **10** for major workflow gaps to **1** for specialized features. Completed changes are recorded in [CHANGELOG.md](CHANGELOG.md). The comparison uses the Daz feature overview, help topics and Bryce 7 Artist Guide. Side-by-side verification with an installed Bryce 7.1 Pro remains open.

## Priority 9

- [ ] **Native Bryce files and presets.** Obtain small, versioned fixtures and verify record layouts for `.br7` scenes and `.mat`, `.obp` and `.bsk` presets. The `probe-bryce` command identified the public BR7 sample's `Bryce_6.0_File` signature and decoded its first zlib stream at byte 42. Object and material decoding still need fixtures with known contents. [Daz preset guidance](https://bugs.daz3d.com/hc/en-us/articles/207528793-How-to-Install-Bryce-7-Pro-using-installers-from-Product-Library), [BR7 example shared by its author](https://www.daz3d.com/forums/discussion/627301/game-on-5-random-objects/p2)
- [ ] **Desktop validation.** Run interactive editing, HDR relinking, scatter erasing, terrain shape keys and recovery on Linux Mint, Ubuntu and Arch. Include Wayland and X11 where available. Confirm prolonged use of these controls on Windows. Windows startup and automated save, load, render, and agent checks passed.
- [ ] **Deep Texture Editor parity.** Compare Bryce's individual noise functions and mixing operations against the layer editor. Bryce texture and preset compatibility still needs native fixtures. Brycescape now supports independent bump and transparency sources and baking an individual component to a heightfield. [Daz terrain and material examples](https://free.downloads-cache.daz3d.com/bryce-pro-landscapes)

## Priority 8

- [ ] **Lighting and atmosphere fidelity.** Compare True Ambience, fill and dome behavior, cloud layers and atmospheric perspective with Bryce reference scenes. Evaluate HDR sampling noise, cloud depth and volumetric shadows. Brycescape now runs up to four diffuse bounces. [Daz Bryce overview](https://www.daz3d.com/bryce-overview)
- [ ] **Large-scene performance.** Extend the benchmarks to large imported meshes, volume-heavy scenes and long desktop editing sessions. Verify cache invalidation under repeated edits. Windows checks already cover localized 4097-point terrain brush edits, a 640×400 render with 2,062 objects, memory use, and cancellation.
- [ ] **Animation tools.** Compare Advanced Motion Lab curve editing and its appearance channels against Brycescape and review remaining animated filters. Transform and joint curve editing, key retiming, terrain heightfield morphing and shape-key persistence are implemented. [Daz Bryce help topics](https://bugs.daz3d.com/hc/en-us/sections/201707363-Bryce)
- [ ] **Terrain workflow depth.** Compare Bryce's custom brushes, clipping, masks and filters. Verify long interactive sessions at 4097 points and smooth shading at tile seams. Seamless border blending and linked terrain grids are implemented. [Daz terrain examples](https://www.daz3d.com/bryce-7-pro-tileable-high-resolution-terrains-and-matched-materials)
- [ ] **Instancing and vegetation depth.** Compare painting and Tree Lab controls with Bryce. Test larger populations and varied terrain under active editing. Mixed-species painting, erasing and slope updates after sculpting are implemented for new instances. [Bryce instancing tutorial](https://www.daz3d.com/forums/discussion/3381/how-to-use-the-instancing-lab-in-bryce-7-1-pro-a-complete-tutorial)
- [ ] **Library depth.** Expand the original standalone sky, terrain and vegetation presets and their previews. Sundial Basin now demonstrates HDR lighting, animated water, a camera orbit and layered materials.

## Priority 7

- [ ] **3D interchange.** Add OBJ material-library round trips and consider FBX, COLLADA and 3DS next. Daz also lists LightWave, VRML 1.0, Direct3D X, 3DMF, TrueSpace, VideoScape, NFF, RayShade heightfields and DEM-related terrain formats. Brycescape now handles OBJ normals and UVs, ASCII DXF mesh entities and PGM heightmaps. [Daz import formats](https://bugs.daz3d.com/hc/en-us/articles/207528313-What-3D-Object-File-Formats-can-Bryce-Import), [Daz export formats](https://bugs.daz3d.com/hc/en-us/articles/207528323-What-3D-Object-File-Formats-can-Bryce-Export)
- [ ] **Render and output validation.** Compare representative glass, soft shadows, volumes, water and animation with measured reference images. Validate blur, alpha, depth and object IDs across multiple effects. Local concurrent workers and interrupted-frame recovery passed. Test shared-folder rendering and recovery on a real network share.
- [ ] **Camera and viewport parity.** Compare camera navigation, controls and display modes. Investigate showing multiple scene cameras and their paths in the viewport. Active camera paths and a crop overlay are implemented.
- [ ] **Undo and recovery coverage.** Test terrain strokes, materials, sky changes, library edits, grouping, imports, parenting, scatter and recovery after restarting the application. Fix inconsistencies found in those workflows.

## Priority 6

- [ ] **Daz Studio bridge.** Investigate a direct import or bridge with pose, material and animation support. OBJ interchange provides the current static mesh route. [Daz Bryce overview](https://www.daz3d.com/bryce-overview)
- [ ] **Water workflow comparison.** Compare water controls and visual output with Bryce 7.1. Evaluate missing wave shapes, material presets and animation controls beyond the existing directional ripples.
- [ ] **Workflow and accessibility review.** Compare the main navigation and palette actions to documented Bryce workflows. Verify keyboard access, tooltips and clear feedback throughout Brycescape's original interface.
- [ ] **Specialized effects from Bryce's examples.** Investigate particle emitters, one-way surfaces, object constraints along a path and stereo anaglyph output. [Daz example scene inventory](https://docs.daz3d.com/public/read_me/index/13196/file_list)

## Priority 5

- [ ] **Feature-by-feature compatibility audit.** Compare actual Bryce 7.1 Pro behaviors against Brycescape and record any additional gaps. Check platform-specific export and legacy codecs against working installations. [Daz Bryce resources](https://www.daz3d.com/bryce-resources), [Bryce 7 Artist Guide](https://digiacademy.org/MM/Ch6/bryce7/bryce_7_artist_guide_wip0913.pdf)
