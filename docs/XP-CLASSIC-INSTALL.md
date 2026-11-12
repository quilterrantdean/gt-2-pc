# GT2 PC — Windows XP Classic

This is the native 32-bit Windows XP build profile. It uses the OpenGL fixed-function renderer; it does not require Vulkan, SDL2, the Vulkan SDK or a modern Windows runtime.

## Requirements

- Windows XP SP3, 32-bit.
- An OpenGL 1.5 driver with a hardware-accelerated implementation.
- At least a 256 × 256 maximum texture size.
- A genuine Gran Turismo 2 BIN image in raw 2352-byte-per-sector format. A 2048-byte ISO is not sufficient because it does not preserve the XA audio and movie data.

The initial graphics defaults are 800 × 600 windowed, VSync on, mipmapping off, anisotropic filtering off and perspective-correct texture mapping. The available window sizes are 640 × 480, 800 × 600 and 1024 × 768. Actual support depends on the display and driver.

## Files

The XP package contains:

- gt2game.exe — the game.
- gt2xpglcheck.exe — checks the active OpenGL driver and its required capabilities.
- gt2install.exe — inspects and installs the user's own disc image.

No game disc data, BIOS, music, movies or other copyrighted game assets are included.

## Installation

1. Extract the three executables to a new folder, for example C:\GT2-PC\.
2. Run gt2xpglcheck.exe. It must report **PASS: OpenGL fixed-function baseline detected**. If it reports a software renderer, OpenGL below 1.5, a texture limit below 256 or missing blend entry points, correct the graphics driver before starting the game.
3. Inspect your own disc image from Command Prompt:

   ```bat
   cd /d C:\GT2-PC
   gt2install.exe inspect "D:\Discs\GT2.BIN"
   ```

4. The inspection reports whether the disc is Arcade, Simulation or a combined disc. Extract it to the matching folder:

   **Arcade**
   ```bat
   gt2install.exe extract "D:\Discs\GT2.BIN" "C:\GT2-PC\arcade"
   ```

   **Simulation**
   ```bat
   gt2install.exe extract "D:\Discs\GT2.BIN" "C:\GT2-PC\simulation"
   ```

   **Combined Arcade / Simulation**
   ```bat
   gt2install.exe extract "D:\Discs\GT2.BIN" "C:\GT2-PC\combined"
   ```

   The destination folder must not already exist. The installer expands the game assets into its assets subfolder and retains the original disc as disc.raw2352 for CD audio and video playback.

5. Start gt2game.exe. The disc selector appears when one or more supported disc folders are installed.

## Controls and settings

- **Alt + Enter:** switch between windowed and fullscreen mode.
- **F10:** open the PC settings overlay during play.
- **Graphics and performance:** change window size, frame limit, draw distance, texture filtering, mipmapping, anisotropic filtering and VSync.
- Saves and settings are stored under the saves folder beside the executables.

Mipmapping and anisotropic filtering are optional. Anisotropic levels are offered only when the OpenGL driver reports the extension and the corresponding maximum level. VSync can only be changed by the game when the driver exposes WGL_EXT_swap_control; otherwise the graphics driver controls synchronisation.

## Compatibility status

The CI build is checked as PE32/i386, with Windows 5.1 OS and subsystem versions, and its imports are scanned for known Vista-or-later APIs. This is not a substitute for testing on the actual Windows XP SP3 machine and graphics card.

The OpenGL renderer is an initial port. Visual parity, all original transparency cases, original car reflections, movie playback and long-session stability still require real-hardware validation. Report any missing or incorrectly rendered element with a screenshot and the contents of the OpenGL diagnostic output.
