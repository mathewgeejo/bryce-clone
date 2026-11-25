# Bryce 7.1 feature gaps

The scores reflect impact on landscape work, from **10** for major workflow gaps to **1** for specialized features. Completed changes are recorded in [CHANGELOG.md](CHANGELOG.md). The comparison uses the Daz feature overview, help topics and Bryce 7 Artist Guide. Side-by-side verification with an installed Bryce 7.1 Pro remains open.

## Priority 9

- [ ] **Native Bryce files and presets.** Establish reliable format documentation and obtain small, versioned fixtures for `.br7` scenes and `.mat`, `.obp` and `.bsk` presets. The public BR7 test scene starts with `Bryce_6.0_File` and contains compressed records. The object and material record layouts still need verification. [Daz preset guidance](https://bugs.daz3d.com/hc/en-us/articles/207528793-How-to-Install-Bryce-7-Pro-using-installers-from-Product-Library), [BR7 example shared by its author](https://www.daz3d.com/forums/discussion/627301/game-on-5-random-objects/p2)
- [ ] **Desktop validation.** Exercise editing, scene saves, HDR assets, scattering, terrain output and recovery on Windows, Linux Mint, Ubuntu and Arch. Include Wayland and X11 where available. Confirm behavior in a native desktop session.
- [ ] **Deep Texture Editor parity.** Compare Bryce's individual noise functions, component outputs and mixing operations against Brycescape's layer editor. Separate alpha and bump outputs within a component and selectable individual components for terrain baking remain open. Material-to-height baking uses the composed material. [Daz terrain and material examples](https://free.downloads-cache.daz3d.com/bryce-pro-landscapes)

## Priority 8

- [ ] **Lighting and atmosphere fidelity.** Compare True Ambience, fill and dome behavior, cloud layers and atmospheric perspective with Bryce reference scenes. Brycescape has HDR light sampling, one diffuse bounce, grouped light rigs and basic volume materials. Exact light controls and appearance remain to be measured. [Daz Bryce overview](https://www.daz3d.com/bryce-overview)
- [ ] **Large-scene performance.** Measure render time, memory and cancellation latency on dense scatter scenes, imported meshes, terrain edits and volumes. Verify cache invalidation and prolonged editing. High-resolution terrain now calculates normals on demand, and history has a terrain memory budget.
- [ ] **Animation tools.** Audit the Advanced Motion Lab's key timing and curve editing. Compare supported appearance channels and add terrain heightfield morphing and remaining animated filters where it materially improves the workflow. [Daz Bryce help topics](https://bugs.daz3d.com/hc/en-us/sections/201707363-Bryce)
- [ ] **Terrain workflow depth.** Compare Bryce's custom brushes, clipping, masks and filters. Verify sustained editing at 4097 points and investigate tiled maps and multi-terrain workflows beyond blending an image into one terrain. [Daz terrain examples](https://www.daz3d.com/bryce-7-pro-tileable-high-resolution-terrains-and-matched-materials)
- [ ] **Instancing and vegetation depth.** Compare terrain paint behavior with Bryce and test very large populations. Investigate palette-based multi-species painting, erase brushes, surface normal updates after sculpting and further Tree Lab controls. [Bryce instancing tutorial](https://www.daz3d.com/forums/discussion/3381/how-to-use-the-instancing-lab-in-bryce-7-1-pro-a-complete-tutorial)

## Priority 7

- [ ] **3D interchange.** Add OBJ material-library round trips and consider FBX, COLLADA and 3DS next. Daz also lists LightWave, VRML 1.0, Direct3D X, 3DMF, TrueSpace, VideoScape, NFF, RayShade heightfields and DEM-related terrain formats. Brycescape now handles OBJ normals and UVs, ASCII DXF mesh entities and PGM heightmaps. [Daz import formats](https://bugs.daz3d.com/hc/en-us/articles/207528313-What-3D-Object-File-Formats-can-Bryce-Import), [Daz export formats](https://bugs.daz3d.com/hc/en-us/articles/207528323-What-3D-Object-File-Formats-can-Bryce-Export)
- [ ] **Render and output validation.** Compare representative glass, soft shadows, volumes, water and animation with measured reference images. Validate blur, alpha, depth and object IDs across multiple effects. Test shared-folder rendering and interrupted worker recovery on a real network share.
- [ ] **Camera and viewport parity.** Compare camera navigation, controls and display modes. Investigate showing multiple scene cameras and their paths in the viewport. Active camera paths and a crop overlay are implemented.
- [ ] **Undo and recovery coverage.** Test terrain strokes, materials, sky changes, library edits, grouping, imports, parenting, scatter and recovery after restarting the application. Fix inconsistencies found in those workflows.

## Priority 6

- [ ] **Daz Studio bridge.** Investigate a direct import or bridge with pose, material and animation support. OBJ interchange provides the current static mesh route. [Daz Bryce overview](https://www.daz3d.com/bryce-overview)
- [ ] **Workflow and accessibility review.** Compare the main navigation and palette actions to documented Bryce workflows. Verify keyboard access, tooltips and clear feedback throughout Brycescape's original interface.
- [ ] **Specialized effects from Bryce's examples.** Investigate particle emitters, one-way surfaces, object constraints along a path and stereo anaglyph output. [Daz example scene inventory](https://docs.daz3d.com/public/read_me/index/13196/file_list)

## Priority 5

- [ ] **Feature-by-feature compatibility audit.** Compare actual Bryce 7.1 Pro behaviors against Brycescape and record any additional gaps. Check platform-specific export and legacy codecs against working installations. [Daz Bryce resources](https://www.daz3d.com/bryce-resources), [Bryce 7 Artist Guide](https://digiacademy.org/MM/Ch6/bryce7/bryce_7_artist_guide_wip0913.pdf)
