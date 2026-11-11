# Windows XP Classic Port

## Goal

Build a native 32-bit Windows XP SP3 version of GT2 PC for modest DirectX 9-era
hardware, using the existing decompilation/recompilation and game code.

The target is the original Gran Turismo 2 experience rendered as a conventional
PC game. It is **not** intended to emulate PS1 rasterizer defects or reproduce
the hardware's technical limitations.

Keep original GT2 gameplay, simulation, cars, tracks, menus, HUD, audio, effects,
transparency and original vehicle-reflection behaviour where practical. Do not
make modern presentation features prerequisites for the XP build.

## Target hardware

Initial acceptance target:

- Windows XP SP3, 32-bit.
- Pentium 4 HT Prescott, 3 GHz.
- 2 GiB RAM.
- NVIDIA GeForce FX 5200 (NV34 A3), 128 MiB DDR, 64-bit, AGP 8x.
- NVIDIA ForceWare 81.98.
- OpenGL 1.5 baseline; OpenGL 2.0 / GLSL 1.10 capabilities are optional and
  must be detected at run time.

Do not infer capabilities from the reported OpenGL version alone. Query the
driver's version, extensions, limits and relevant function entry points.

## Rendering approach

- Add a Windows-only OpenGL renderer behind the existing SceneRenderer /
  SceneRendererState boundary.
- Reuse scene construction, DrawItem submission, game assets and shared game
  logic; do not rewrite the game or its simulation.
- Prefer OpenGL 1.5 fixed-function functionality for the baseline path.
  Use small GLSL 1.10 programs only where they provide a clear benefit and are
  supported by the active driver.
- Use ordinary 2D textures and conventional vertex arrays/VBOs where available.
  Avoid texture arrays, integer texture samplers, image load/store, compute,
  multiple render targets and other later-generation features.
- Keep texture decoding and palette/CLUT expansion on the CPU initially. Upload
  only the textures needed by the current scene through a bounded GPU cache.
- Use normal perspective-correct texture interpolation and stable geometry.
  Do not add PS1 affine texture warping, polygon wobble or PS1 colour dithering.
- Preserve original game transparency, depth ordering, texture addressing,
  vehicle lighting and other visible behaviours that are part of GT2 itself.

## Scope exclusions

The XP profile does not build or enable these optional modern additions:

- OpenXR, PCVR, Quest and VR-only controls/rendering.
- GT4 expansion/imported cars and their reconstructed interiors.
- Reconstructed 3D cockpits and cockpit inspection.
- HD media packs, upscaled menus, fonts or movies.
- Modern material/reflection effects beyond the original GT2 vehicle pass.
- Supersampling, MSAA and modern post-processing.
- Optional profiler, developer capture and visual-audit tools in the player.

Do not remove original GT2 features merely because they are visually complex.
Keep original headlights/brake lights, smoke, transparency, scenery, menu
animations, replay behaviour and the original rear-view mirror where supported.

## User-selectable graphics options

Expose these as ordinary settings, with capability detection and safe fallbacks:

- **VSync:** On / Off. Use WGL_EXT_swap_control when available; otherwise
  report that the driver controls presentation synchronisation.
- **Mipmapping:** On / Off. Generate mip levels after palette expansion.
  Keep it optional because it trades extra texture memory for better minified
  texture quality and potentially lower sampling cost.
- **Anisotropic filtering:** Off / 2x / 4x. Only expose supported levels after
  detecting GL_EXT_texture_filter_anisotropic and querying its maximum.
- **Resolution / display mode:** offer stable 4:3 resolutions supported by the
  display, plus windowed mode. Do not assume a particular CRT mode is available.

Default profile: VSync On, mipmapping Off, anisotropic filtering Off,
perspective-correct textures, no MSAA. Defaults can be revised after testing
on the FX 5200.

## Quality-of-life features

Keep low-cost PC conveniences when they do not change game balance or impose
modern graphics requirements:

- Windowed mode and selectable resolution.
- Keyboard and DirectInput controller configuration.
- Sensible pause/focus handling.
- User-selected data/save location and safe save backups.
- Clear error messages and a portable installation option.

Optional features must degrade cleanly if an extension or device capability is
missing. No silent software-rendering fallback for a feature that would make
the game unusably slow.

## Build and compatibility constraints

- Produce a genuine i686 Win32 executable and XP-compatible dependencies.
- Set XP target macros deliberately and audit every Windows API used by the
  player; do not assume that a successful link makes the executable XP-safe.
- Keep build tools and packaging dependencies off the target PC where possible.
- Do not include macOS, Linux, Steam Deck, browser or Android targets in the
  XP player build.
- Keep developer tests separate from the distributable player.

## Implementation stages

1. Add the XP build profile and isolate its source/dependency set.
2. Implement a minimal OpenGL context and renderer that clears the screen,
   draws untextured/textured triangles and handles depth.
3. Add GT2 texture/CLUT decoding, transparent polygon passes and the complete
   original scene path.
4. Add the original menus/HUD, audio, input and save flow.
5. Add optional mipmapping, anisotropic filtering and VSync settings with
   run-time capability detection.
6. Build and test on a modern host, then validate on real XP / FX 5200 hardware.

A build is not considered XP-compatible or playable until it has been tested
on Windows XP SP3 and the target GPU. CI compilation alone is insufficient.
