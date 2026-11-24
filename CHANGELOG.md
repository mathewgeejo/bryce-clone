# Implementation log

## Native Linux desktop

- Added a GTK 3 desktop backend for the shared editor on Linux. It handles the canvas, keyboard and mouse input, and native file and color dialogs.
- Added CMake build and test targets, plus build instructions for Linux Mint, Ubuntu, and Arch Linux.
- Shared the headless renderer entry point across both platforms. Quoted paths with spaces work in command-line renders and scene exports.
- Store the Linux material and sky library in the user's XDG config directory.

## Project name and README

- Named the studio **Two Hoodoos and a Floppy** in the README and desktop window. The README covers setup, editing, rendering, and project checks.

## P0 · 1. Metaballs

- Added a creatable metaball with editable placement, influence radius, strength, sign, and material.
- Grouped balls form a shared surface. Subtractive balls carve the field, and existing Boolean operands can add to, cut, or clip the result.
- Scene files save the new settings. Legacy v1 and v2 scene files retain their original object IDs.
- Added regression cases for blending, subtraction, clipping, and scene round trips.

## P0 · 2. Picture objects

- Added **Create → Picture** to import a BMP as a two-sided card at its original aspect ratio.
- Added a fixed-plane or camera-facing mode, adjustable chroma cutouts, and separate shadow and reflection visibility.
- Scene files retain the image path and card settings. The Picture Editor can replace or relink the source BMP.
- Added regression cases for cutouts, aspect ratio, billboard orientation, ray visibility, and scene round trips.

## P0 · 3. Layered materials

- Added ordered texture layers with per-layer mapping, scale, offset, rotation, opacity, and Mix, Multiply, or Add blending.
- Layers can use their luminance, inverse luminance, or the previous layer as a mask. Each layer has independent Color, Bump, Specular, Reflection, Transparency, and Ambient influence.
- The Materials Lab previews the composed material and lists the selected layer's affected channels. Individual layers can be saved and loaded as `.c3tex` presets.
- Scene files and the material library store layers. Added regression cases for blending, masking, and persistence.

## P0 · 4. Direct object manipulation

- Added a selection cage with viewport drag handles for Move, Rotate, and Scale. Handles use the same transforms and undo history as the Edit tools.
- Added contextual **A** Attributes, **M** Material, and **E** Edit controls and keyboard shortcuts.
- Added **Frame Sel** and **Frame All** to the view palette. Alt-click can select one grouped object, including a metaball by its center.

## P0 · 5. Object-specific editors

- Added a Stone Editor for seed, irregularity, blockiness, and smoothing, plus a Torus Editor for both radii, tube height, and capped arcs.
- Added a Light Editor for color, power, shadow size, and spotlight cone settings. Spot and directional lights have a viewport aim handle.
- Added an imported-mesh editor for flat or smooth shading, base color, diffuse strength, and mesh counts.
- Connected the picture and metaball editors to the same **E** control. Added regression cases for the new geometry, shading, and scene settings.
