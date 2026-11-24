# Two Hoodoos and a Floppy

A native Windows and Linux landscape studio for sculpting terrain, staging little worlds, and software ray tracing. For anyone who remembers when a chrome sphere over a checkerboard was a perfectly good evening.

![Six ray-traced scenes, from mirrored spheres and floating islands to a desert arch and a volcanic island](build/t/ex.png)

## Get started

### Linux

Install the C++ build tools, CMake, and GTK 3 development packages.

**Linux Mint and Ubuntu**

```bash
sudo apt install build-essential cmake pkg-config libgtk-3-dev
```

**Arch Linux**

```bash
sudo pacman -S --needed base-devel cmake pkgconf gtk3
```

From the repository root, build and launch the studio.

```bash
cmake -S . -B out/linux -DCMAKE_BUILD_TYPE=Release
cmake --build out/linux --parallel
./out/linux/Canyon3D
```

The desktop uses GTK 3 on X11 and Wayland. On other Linux distributions, install a C++17 compiler, CMake, and the package that supplies `gtk+-3.0` to pkg-config. The materials and skies library lives at `$XDG_CONFIG_HOME/two-hoodoos-and-a-floppy/canyon3d_library.txt` and uses `~/.config` by default.

### Windows

Install Visual Studio 2019 or later with the **Desktop development with C++** workload, or the equivalent C++ Build Tools. From the repository root in PowerShell, run:

```powershell
.\build.bat
.\build\Canyon3D.exe
```

Close the studio before rebuilding so the linker can replace `Canyon3D.exe`. The Windows desktop uses Win32 and GDI.

### First scene

1. Open **Examples** to load a complete scene, or use **Create** to start placing objects.
2. Use **Edit** to move, rotate, resize, or sculpt. Open **Materials Lab** and **Sky & Fog** to set the look of the scene.
3. Press **Ctrl+R** to render. Use **Save Image** to write a BMP, or **Ctrl+S** to save an editable `.c3d` scene.

## What you can make

- Sculpt heightfields and symmetric lattices with brushes, procedural generators, erosion, terraces, and BMP heightmaps.
- Fuse positive metaballs and carve the shared surface with subtractive balls or Boolean primitives.
- Add BMP picture cards with chroma cutouts. Use fixed planes or cards that face the camera.
- Combine primitives with Boolean operations, generate trees, and import Wavefront OBJ meshes.
- Layer procedural and BMP textures across material channels, and save individual texture layers as `.c3tex` presets.
- Place point, spot, and directional lights, and tune fog, clouds, sunlight, and moonlight.
- Keyframe object transforms, the camera, and the sky. Render a still, a panorama, or an animation as numbered BMP frames.

## Render from the command line

```bash
./out/linux/Canyon3D --render out.bmp 640 400
./out/linux/Canyon3D --render out.bmp 640 400 "my scene.c3d"
./out/linux/Canyon3D --example 0 example.bmp 640 400
./out/linux/Canyon3D --export-example 0 example.c3d
```

On Windows, use `.\build\Canyon3D.exe` with the same arguments.

`--render` opens the default scene when no scene file is supplied. Example indices start at zero. The command-line renderer also works on Linux without a running desktop session.

## Useful keys

| Key | Action |
| --- | --- |
| `1`–`4` | Switch between perspective, top, front, and side views |
| `A` / `M` / `E` | Open attributes, materials, or the selected object's editor |
| `F` | Frame the selection or the whole scene |
| `Ctrl+G` / `Ctrl+U` | Group / ungroup the selection |
| `Ctrl+D` / `Delete` | Duplicate / delete the selection |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo |
| `Ctrl+K` / `Space` | Add a keyframe / play or pause the timeline |
| `[` / `]` | Decrease / increase the terrain brush size |
| `Esc` | Close the active lab or stop a render |

Select an object to reveal its viewport handles. Drag **Move**, **Rotate**, or **Scale** to transform it. Use **E** to shape stones and tori, edit imported meshes and picture cards, or tune lights and metaballs. To fuse metaballs, position them so their fields overlap, select both, and press **Ctrl+G**.

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
