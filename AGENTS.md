# Canyon 3D (Bryce-style landscape studio)

Native Win32 + GDI app with a software ray tracer, C++17, no third-party dependencies.

- Build: `build.bat` (uses VS 2019+ Build Tools via vswhere; output `build\Canyon3D.exe`). Close the running app first or linking fails.
- Headless render check: `build\Canyon3D.exe --render out.bmp 640 400 [scene.c3d]` (renders the default scene when no scene is given).

Layout: `src/vmath.*` math/noise, `src/scene.*` model/primitives/intersection/persistence, `src/terrain.*` heightfield ops,
`src/render.*` ray tracer + progressive renderer, `src/main.cpp` immediate-mode GDI UI (palettes, viewport, Labs).
