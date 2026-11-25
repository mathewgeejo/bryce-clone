# Brycescape

A native Windows and Linux landscape studio for sculpting terrain, staging little worlds, and software ray tracing. For anyone who remembers when a chrome sphere over a checkerboard was a perfectly good evening.

![Six ray-traced scenes, from mirrored spheres and floating islands to a desert arch and a volcanic island](build/t/ex.png)

## Get started

### Linux

Install the C++ build tools, CMake, GTK 3 and LibTIFF development packages.

**Linux Mint and Ubuntu**

```bash
sudo apt install build-essential cmake pkg-config libgtk-3-dev libtiff-dev
```

**Arch Linux**

```bash
sudo pacman -S --needed base-devel cmake pkgconf gtk3 libtiff
```

From the repository root, build and launch the studio.

```bash
cmake -S . -B out/linux -DCMAKE_BUILD_TYPE=Release
cmake --build out/linux --parallel
./out/linux/Brycescape
```

The desktop uses GTK 3 on X11 and Wayland. On other Linux distributions, install a C++17 compiler, CMake, LibTIFF development files, and the GTK package that supplies `gtk+-3.0` to pkg-config. The materials and skies library lives at `$XDG_CONFIG_HOME/brycescape/canyon3d_library.txt` and uses `~/.config` by default.

### Windows

Install Visual Studio 2019 or later with the **Desktop development with C++** workload, or the equivalent C++ Build Tools. From the repository root in PowerShell, run:

```powershell
.\build.bat
.\build\Brycescape.exe
```

Close the studio before rebuilding so the linker can replace `Brycescape.exe`. The Windows desktop uses Win32 and GDI.

### First scene

1. Open **Examples** to load a complete scene, or use **Create** to start placing objects.
2. Use **Edit** to move, rotate, resize, or sculpt. Open **Materials Lab** and **Sky & Fog** to set the look of the scene.
3. Press **Ctrl+R** to render. Use **Save Image** to write PNG, TIFF or BMP, or **Ctrl+S** to save an editable `.c3d` scene.

## What you can make

- Sculpt heightfields and symmetric lattices with brushes, procedural generators, erosion, terraces, and imported heightmaps.
- Fuse positive metaballs and carve the shared surface with subtractive balls or Boolean primitives.
- Add picture cards with PNG alpha or chroma cutouts. Use fixed planes or cards that face the camera.
- Combine primitives with Boolean operations, generate trees, import OBJ or layered DXF meshes, and export selections as OBJ or DXF.
- Parent objects through **Edit Tools → Parent to Active** and inspect the relationships in the scene hierarchy.
- Paint linked vegetation or geometry onto terrain with **F1 → Scatter Brush**. Select the source objects first and Shift-click the receiving surface last. Enable **Mix Selected Species** to distribute the selected groups. Hold **Shift** to erase painted instances and press **Esc** to leave the brush. New terrain instances follow height edits and can track changes in surface angle.
- Layer procedural and image textures across material channels, and save individual texture layers as `.c3tex` presets.
- Use **Texture Layers** to warp procedural coordinates, route separate bump and transparency inputs, and mask layers by elevation or slope. The **Mesh UV** mapping option uses the texture coordinates supplied by an imported OBJ.
- Place point, spot, and directional lights, and tune fog, clouds, sunlight, and moonlight.
- Load a Radiance HDR environment in **Sky Lab** for background, reflections and lighting. Set up to four diffuse bounces, or add a grouped dome, cluster or fill rig.
- Blend heightmaps with grayscale masks, match opposite edges with **Make Tileable**, or bake a complete material or individual layer into the heightfield. The terrain editor supports 16-bit grayscale TIFF and PGM and a 4097-point grid.
- Use the **Terrain Shape Track** in **Motion Editor** to capture or import heightmaps at different frames. The track interpolates between up to 32 keys, each sampled at up to 1025 points per side. Use **Key Timing** to move a key to an unused frame. **Sundial Basin** has an embedded HDR environment, layered rock, directional waves and a camera orbit.
- Animate transforms and appearance tracks, switch between named cameras, and render stills, panoramas or numbered animation frames.
- Save scene fragments in the thumbnail library. Package, embed or relink image assets through **Commands**.

## Edit meshes and animation curves

Select an object and open **F1 → Mesh Editor**. Imported OBJ faces retain their original boundaries, including quads and n-gons. Use **Convert Selection to Mesh** to turn a supported procedural solid into an editable mesh. Cubes convert to six quads with eight shared vertices. On older triangle meshes, use **Enable Polygon Tools** to work with each existing triangle as a polygon.

- Choose **Vertices**, **Edges**, or **Faces**, then click in the wire view. Shift-click toggles components. The 3D, Front, Top, and Side views help reach overlapping components.
- Set local X, Y, and Z values and apply **Move**, **Rotate**, or **Scale**. Rotation and scale use the selection center.
- In **Faces**, extrude a region, delete faces, or reverse their winding. For **Inset Face**, select one convex, planar face and set the inward offset. **Subdivide Mesh** splits convex polygon faces into quads with shared edge midpoints. On legacy triangle meshes it splits each triangle into four.
- In **Edges**, select one edge and use **Loop Cut** to split the adjoining quad strip at **Cut Position**. The cut can end at a boundary or a non-quad.
- Use **Select Boundary** to grow selected edges along their boundaries. **Fill Boundary** caps one closed planar loop. **Bridge Edges** joins two open chains or two closed loops with the same edge count. **Bridge Segments** controls the number of quad rows, and **Bridge Twist** shifts the vertex pairing on closed loops.
- Open **Knife Path** and click each edge the cut crosses. Click near a vertex to snap to it or hold **Shift** to snap to the edge midpoint. Each segment crosses a face. **Apply Knife** commits the whole path, and **Esc** exits Knife mode and discards the pending path.
- Select edges on a closed part of the mesh and click **Bevel Edges**. **Bevel Width** sets the local offset, and **Bevel Segments** makes a flat chamfer with one segment or a rounded profile with up to 16. Reduce the width if the edited faces overlap.
- In **Vertices**, **Weld by Distance** merges nearby selected positions and retains independent face-corner UVs. Legacy meshes can use **Preserve UV Seams** to keep their separate seam vertices. Planar UV projection replaces the current UV coordinates along the chosen axis.

Open **Symmetry** in the Mesh Editor and choose a local axis, plane position, and source side. **Symmetrize Mesh** replaces the opposite side, snaps nearby vertices to the plane, and joins the halves along the seam. Faces wholly on the plane are omitted so a half-mesh cap does not become an internal face. Mirrored faces reuse the source UVs. Enable **Mirror Component Transforms**, then return to component editing to mirror moves, rotations, and scales across matching vertices. Unpaired selections can still move independently, and selected seam vertices stay on the plane.

The editor supports up to 100,000 render triangles per mesh. A polygon can have up to 2,048 corners. Polygon positions stay connected through UV seams. Geometry edits can regenerate normals, and OBJ export keeps polygon boundaries where the posed faces have a valid triangulation. **OK** commits one undoable edit and **Cancel** discards the session.

Open **F1 → Curve Editor** with an object selected to inspect Location, Rotation, and Scale along each axis. Drag the graph background to scrub. Click a key to select it, drag vertically to change its value, or use the controls to retime, copy, remove, and change interpolation. Each transform key shares its frame and interpolation across all nine components. The graph samples the same curves used for playback.

### Rig and pose a mesh

Select a mesh and open **F1 → Rig Editor**. In **Skeleton**, add a root and child joints, select them in the preview, and adjust their rest positions. Click the joint name to rename it. **Bind Skin** assigns weights from joint distances.

Use **Weights** to assign an influence to selected rest vertices. Shift-click toggles vertices. Polygon UV seams share the same vertex weight. On legacy meshes, selection includes coincident copies along a seam. Each vertex supports four influences. The editor adjusts the other weights when you assign one.

In **Pose**, select a joint, set a frame, and adjust its local translation, rotation, or scale. **Set Pose Key** makes the pose part of the animation. Editing an animated joint at another frame sets a key there. The **Curve Editor** can switch between the object and its joint tracks. Click the joint name to cycle tracks and Shift-click to cycle backward.

For inverse kinematics, open **IK**, select the lower joint of a limb, and click **Add Two-Joint IK**. The target positions the tip and the pole sets the elbow or knee bend. Edit their coordinates or use **Place Target** and **Place Pole** to place the control in the preview at its current depth. **IK Influence** blends the solve with the joint pose. Use **Set IK Key** to animate the target, pole, and influence. The controls use mesh space and the solver keeps the limb within its reach.

Scenes with polygon meshes save in `.c3d` version 10. Other rigged scenes use version 7, version 8 with IK, or version 9 with actions. Rendering and geometry export use the current pose. Linked instances share the source pose until you make them unique. Unbind before changing the mesh's topology.

### Reuse character animation

Select a rigged object and open **F1 → Action Editor**. Set the first and last frames and click **Capture Action**. This records the joint and IK motion visible in that range. Click the action name to rename it.

Move the playhead and use **Place at Playhead** to add a clip. Repeat or retime the clip with **Repeats** and **Speed**. Use **Influence**, **Fade In**, and **Fade Out** to blend it over the underlying pose and other clips. **Raise Priority** moves a clip above lower-priority clips. **Apply & Play** commits the changes and previews the selected clip.

Use **Copy Action** and **Paste Action** to transfer it to another rig with unique, matching joint names, parents, and IK chains. Transfer scales local translations and relocates IK controls to the destination proportions. Object-level transforms stay in their own tracks. **Bake Clips to Keys** converts the evaluated clip stack into editable joint and IK keys on timelines up to 1,200 frames. The captured actions stay in the library.

## Simulate physics and liquid

Open **Edit → Simulation** or choose **Simulation** from the **F1** command palette. Select a source object or use **Add Liquid Source** or **Add Drop Body** to place a starter above the scene. Choose the presets, clip length, and detail, then click **Bake & Play**. The preview stops at the last baked frame. **Bake** leaves the timeline at the start of the clip. **Ctrl+Z** undoes the bake.

- **Liquid** releases the selected object's bounding box as **Water**, **Oil**, **Syrup**, or **Lava**. Presets set the appearance, viscosity, and cohesion. **Fine** increases solver precision and uses up to 512 particles. Advanced settings include the particle grid, surface size, and source visibility.
- **Rigid Bodies** uses **Balanced**, **Rubber**, **Steel**, **Ice**, and **Wood** presets for rebound, sliding, and damping. Bodies collide with one another and with passive solids and terrain. Spheres roll and spin. Other solids use fixed, world-aligned bounding boxes.
- **Gravity** offers Earth, Moon, Mars, Jupiter, Zero G, Reverse, Attract, and Repel. Open **Advanced Settings** to enter a custom direction or adjust a radial field's center and falloff radius. Attract and Repel use the 3D cursor as the starting center when it is visible. [NASA planetary gravity reference](https://nssdc.gsfc.nasa.gov/planetary/factsheet/)

The bake uses the visible pose and starts at the current timeline frame by default. Turning off **Start at Current Frame** writes the clip from frame 0. A later liquid clip leaves the source visible until the first baked frame if **Hide Volume Source** is on. Rigid-body bakes replace transform keys inside the clip and retain the keys outside it. Scrub the baked animation, save and reopen it, or render the clip by passing its first and last frames to `--render-frames`.

Passive colliders stay at their starting poses during a bake. The solvers run independently. Each bake supports up to 240 frames and 12 seconds. Rigid Bodies handles up to 96 active objects. The liquid pressure solver uses positional density constraints. [Position Based Fluids](https://mmacklin.com/pbf_sig_preprint.pdf)

The agent CLI and MCP compose tools accept these presets, start frames, and advanced settings. Add the named source objects to the recipe first. Explicit numeric settings override the matching preset defaults.

```json
[
  {"op": "timeline", "frames": 60, "fps": 24},
  {"op": "object", "name": "Ball", "kind": "Sphere", "position": [-4, 5, 0]},
  {"op": "object", "name": "Pool release", "kind": "Cube", "position": [4, 3, 0]},
  {"op": "rigid_bake", "names": ["Ball"], "frames": 60, "preset": "Rubber", "gravity_preset": "Moon"},
  {"op": "liquid_bake", "name": "Pool release", "frames": 48, "start_frame": 12, "preset": "Syrup", "resolution": 6}
]
```

## Render from the command line

### Create and render scenes with an agent

Build Brycescape and use Python 3.10 or newer. The agent tools write editable `.c3d` scenes and can return PNG previews for visual iteration. The included recipe builds a lake landscape with layered terrain and scattered trees.

```text
python tools/brycescape_agent.py catalog
python tools/brycescape_agent.py schema
python tools/brycescape_agent.py compose tools/example_recipe.json "cedar lake.c3d"
python tools/brycescape_agent.py inspect "cedar lake.c3d"
python tools/brycescape_agent.py render "cedar lake.c3d" "cedar lake.png" --width 640 --height 400
```

Recipes run in order. Use exact preset names from `catalog` and unique object names. Place a `base` operation first to edit an existing scene or an `example` operation first to start from a built-in scene. Coordinates are X, Y up, Z. Rotations and camera angles are in degrees. Use `--binary` before the command for builds outside the default locations.

Use `terrain_tiles` on an ungrouped, unparented terrain without animation keys to make a grid that shares one seamless heightfield. Use `terrain_key` to load a heightmap at a particular frame. A tree name can be used as the source of a `scatter` operation to plant both trunk and foliage together.

To connect an MCP client that supports local stdio servers, add a server entry with absolute paths to this script, the built program, and a directory for scenes and assets.

```json
{
  "mcpServers": {
    "brycescape": {
      "command": "python",
      "args": [
        "/path/to/Brycescape/tools/brycescape_agent.py",
        "--binary", "/path/to/Brycescape",
        "--workspace", "/path/to/scenes",
        "mcp"
      ]
    }
  }
}
```

The server exposes `brycescape_catalog`, `brycescape_compose`, `brycescape_inspect`, and `brycescape_render`. File arguments resolve inside the configured workspace. Save to a new filename or explicitly set `overwrite`. The agent integration smoke test is `python tools/check_agent.py PATH_TO_BRYCESCAPE`. [MCP stdio transport](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports#stdio)

### Render existing scenes

```bash
./out/linux/Brycescape --render out.bmp 640 400
./out/linux/Brycescape --render out.png 640 400 "my scene.c3d"
./out/linux/Brycescape --example 0 example.bmp 640 400
./out/linux/Brycescape --export-example 0 example.c3d
./out/linux/Brycescape --batch "render queue.txt"
./out/linux/Brycescape --render-frames "my scene.c3d" "shared frames" 0 119
```

On Windows, use `.\build\Brycescape.exe` with the same arguments.

`--render` opens the default scene when no scene file is supplied. Example indices start at zero. The command-line renderer also works on Linux without a running desktop session.

A queue file contains one scene and output pair per line, such as `"my scene.c3d" "output.tiff"`. Relative paths resolve from the queue file. In the desktop, use **Render Options → Start Render Queue**. Animation output can use a name such as `walk_{frame}.png`.

For distributed animation rendering, run the same `--render-frames` job on multiple workers with access to the same scene assets and output folder. The scene determines the image size and render settings. The range is inclusive and starts at frame zero. Each worker claims frames using directories on the shared filesystem, so use a share that supports atomic directory creation. Use a separate output folder per job. Workers can rerun a job and skip finished frames. If a worker crashes, its `.rendering` directory identifies the interrupted frame. Remove that directory after confirming the worker has stopped, then rerun the job.

Brycescape uses its own `.c3d` scene format and reads older versions. For geometry interchange with Bryce or Daz Studio, use OBJ. Brycescape reads OBJ geometry, normals and UVs. Apply the exported material images in the Materials Lab using **Mesh UV**. ASCII DXF also works for 3DFACE and polyface meshes. The remaining native file, preset and interchange work is tracked in the backlog.

## Useful keys

| Key | Action |
| --- | --- |
| `1`–`4` | Switch between perspective, top, front, and side views |
| `A` / `M` / `E` | Open attributes, materials, or the selected object's editor |
| `F` | Frame the selection or the whole scene |
| `F1` | Open the command palette |
| `G` / `R` / `S` | Choose Move / Rotate / Scale, then drag in the viewport |
| `X` / `Y` / `Z` | Toggle an axis constraint before or during a drag |
| `Shift` / `Ctrl` during a drag | Precision adjustment / temporary grid snap when snapping is off |
| `Shift+right-click` | Place the 3D cursor on the scene surface under the pointer |
| `Ctrl+Shift+D` | Duplicate the selection across the chosen mirror plane |
| `Ctrl+G` / `Ctrl+U` | Group / ungroup the selection |
| `Ctrl+D` / `Delete` | Duplicate / delete the selection |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo |
| `Ctrl+K` / `Space` | Add a keyframe / play or pause the timeline |
| `[` / `]` | Decrease / increase the terrain brush size |
| `Esc` | Cancel a transform, close the active lab, or stop a render |

Select an object to reveal its viewport handles. Drag **Move**, **Rotate**, or **Scale** to transform it. Use **E** to shape stones and tori, edit imported meshes and picture cards, or tune lights and metaballs. To fuse metaballs, position them so their fields overlap, select both, and press **Ctrl+G**.

Open **Edit → Transform** to set up precise placement. **Auto-lock Move Axis** follows the main direction of the drag. Move uses world axes. Rotate and Scale adjust each object's transform axes and use the chosen pivot to position the selection. **Grid** snaps translation by the grid step, rotation by the angle step, and scaling by 0.1 increments of the original scale. **Surface** places the bottom center of the selection bounds on the surface under the pointer. Release the mouse or press **Enter** to commit a drag.

Use **Bounds Center** for a selection's geometric center, **Median** for the average of selected root origins, or pivot around the **Active Object**, **World Origin**, or **3D Cursor**. The palette can center the selection at the origin or cursor and move the cursor to the bounds center. To build a symmetric arrangement around the world origin, choose **World Origin**, select a mirror axis, and click **Mirror Duplicate**. In **Terrain Editor**, **Mirror Left to Right** and **Mirror Right to Left** copy one half of the heightfield onto the other.

**Display** opens the viewport palette. Double-click a hierarchy row to rename it. Drag a row to reorder it, or Alt-drag it onto a parent. Terrain regions and strokes have separate undo and redo controls in the Terrain Editor.

## Source and checks

`src/main.cpp` contains the shared desktop editor. `src/desktop_win.cpp` and `src/desktop_gtk.cpp` provide windowing, drawing, input, and dialogs. `src/scene.*` handles objects, intersections, animation, and persistence. `src/render.*` is the ray tracer.

On Linux, enable the regression checks and headless smoke test with CMake.

```bash
cmake -S . -B out/linux -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build out/linux --parallel
(cd out/linux && ctest --output-on-failure)
```

On Windows, run `build\t\buildtest.bat` and then `build\t\test.exe` from the repository root after building. The core checks cover scene round trips, legacy scenes, animation, and terrain tools.

See [the backlog](todo.md) and [the implementation log](CHANGELOG.md) for project status.
