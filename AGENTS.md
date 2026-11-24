# Two Hoodoos and a Floppy

Native desktop landscape studio with a C++17 software ray tracer. The editor uses Win32 and GDI on Windows and GTK 3 on Linux.

- Build: `build.bat` (uses VS 2019+ Build Tools via vswhere; output `build\Canyon3D.exe`). Close the running app first or linking fails.
- Headless render check: `build\Canyon3D.exe --render out.bmp 640 400 [scene.c3d]` (renders the default scene when no scene is given).
- Linux build: `cmake -S . -B out/linux -DCMAKE_BUILD_TYPE=Release`, then `cmake --build out/linux --parallel`. Run `./out/linux/Canyon3D`.
- Linux checks: configure with `-DBUILD_TESTING=ON`, build, then run `ctest --output-on-failure` from `out/linux`.

- Core checks: after `build.bat`, run `build\t\buildtest.bat` then `build\t\test.exe` (keyframes, save/load round trip, v1 compat,
  terrain tools; writes preset/primitive/premium/panorama sheets as BMPs into `build\t\`). Must end with "ALL CHECKS PASSED".
- UI-only check while the app is open: `build\t\buildui.bat` links `build\t\ui.exe` from the existing core .obj files.
  `build\t\click.ps1 ui x y ...` / `build\t\shot.ps1 -proc ui -out file.png` drive and capture that window (coords = window-image pixels).

Layout: `src/vmath.*` math/noise, `src/scene.*` model/primitives/CSG/keyframes/persistence, `src/terrain.*` heightfield ops,
`src/render.*` ray tracer + progressive renderer, `src/mesh.*` BVH meshes/OBJ import/tree generator, `src/image.*` BMP loader,
`src/presets.cpp` materials/skies/example scenes (all original content), `src/main.cpp` shared immediate-mode desktop UI (palettes, viewport, Labs),
`src/desktop_win.cpp` Win32 backend, `src/desktop_gtk.cpp` GTK backend.
