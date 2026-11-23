# Canyon 3D (Bryce-style landscape studio)

Native Win32 + GDI app with a software ray tracer, C++17, no third-party dependencies.

- Build: `build.bat` (uses VS 2019+ Build Tools via vswhere; output `build\Canyon3D.exe`). Close the running app first or linking fails.
- Headless render check: `build\Canyon3D.exe --render out.bmp 640 400 [scene.c3d]` (renders the default scene when no scene is given).

- Core checks: after `build.bat`, run `build\t\buildtest.bat` then `build\t\test.exe` (keyframes, save/load round trip, v1 compat,
  terrain tools; writes preset/primitive/premium/panorama sheets as BMPs into `build\t\`). Must end with "ALL CHECKS PASSED".
- UI-only check while the app is open: `build\t\buildui.bat` links `build\t\ui.exe` from the existing core .obj files.
  `build\t\click.ps1 ui x y ...` / `build\t\shot.ps1 -proc ui -out file.png` drive and capture that window (coords = window-image pixels).

Layout: `src/vmath.*` math/noise, `src/scene.*` model/primitives/CSG/keyframes/persistence, `src/terrain.*` heightfield ops,
`src/render.*` ray tracer + progressive renderer, `src/mesh.*` BVH meshes/OBJ import/tree generator, `src/image.*` BMP loader,
`src/presets.cpp` materials/skies/example scenes (all original content), `src/main.cpp` immediate-mode GDI UI (palettes, viewport, Labs).
