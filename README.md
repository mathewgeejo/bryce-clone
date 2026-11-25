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
- Paint linked vegetation or geometry onto terrain with **F1 → Scatter Brush**. Select the source objects first and Shift-click the receiving surface last. Set the brush density, size variation, rotation, alignment and seed, then paint in the viewport. Press **Esc** to leave the brush. Terrain instances follow height edits and share their source geometry.
- Layer procedural and image textures across material channels, and save individual texture layers as `.c3tex` presets.
- Use **Texture Layers** to warp procedural coordinates, shape levels, and mask layers by elevation or slope. The **Mesh UV** mapping option uses the texture coordinates supplied by an imported OBJ.
- Place point, spot, and directional lights, and tune fog, clouds, sunlight, and moonlight.
- Load a Radiance HDR environment in **Sky Lab** for background, reflections and lighting. Use the indirect light controls for a diffuse bounce, or add a grouped dome, cluster or fill rig.
- Blend a heightmap into a terrain or rectangle with an optional grayscale mask. **Material to Height** blends the material's bump or color into the heightfield, sampled on a grid of up to 1025 points per side. The terrain editor supports 16-bit grayscale TIFF and PGM and a 4097-point grid. Press **E** on water to edit directional waves.
- Animate transforms and appearance tracks, switch between named cameras, and render stills, panoramas or numbered animation frames.
- Save scene fragments in the thumbnail library. Package, embed or relink image assets through **Commands**.

## Render from the command line

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
| `Ctrl+G` / `Ctrl+U` | Group / ungroup the selection |
| `Ctrl+D` / `Delete` | Duplicate / delete the selection |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo |
| `Ctrl+K` / `Space` | Add a keyframe / play or pause the timeline |
| `[` / `]` | Decrease / increase the terrain brush size |
| `Esc` | Close the active lab or stop a render |

Select an object to reveal its viewport handles. Drag **Move**, **Rotate**, or **Scale** to transform it. Use **E** to shape stones and tori, edit imported meshes and picture cards, or tune lights and metaballs. To fuse metaballs, position them so their fields overlap, select both, and press **Ctrl+G**.

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
