# Credits

- **[ps3recomp](https://github.com/sp00nznet/ps3recomp)**, by Ned Heller
  (sp00nznet) and its contributors (MIT), pinned at v0.18.0 (`4e8f3ca8`). This
  port's toolkit changes are in `work/patches/ps3recomp.patch`.
- **The MotorStorm Apocalypse port** (a sibling PC port by the same author):
  the project scripts (setup, patch, leak checks), the settings file and the
  in-game menu started as copies of that port's, and several fixes were ported
  from it (see [docs/APOCA_LESSONS.md](docs/APOCA_LESSONS.md)).
- **The RACiT launcher** (Ratchet & Clank: A Crack in Time's PC port): the
  idea and layout of the start screen and settings pages -- Play backing up
  the saves first, the Saves screen, and the screen that finds and checks the
  game's files. No RACiT code is part of this project.
- **[RPCS3](https://rpcs3.net)** (GPL-2.0): used to decrypt the executable and
  to install the PS3 firmware whose `dev_flash` the port reads. No RPCS3 code
  is part of this project.
- **[AMD FidelityFX Super Resolution 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR)**
  (MIT): `ffx_a.h` and `ffx_fsr1.h`, unchanged, in the toolkit patch
  (`libs/video/vulkan/fsr1/`, with AMD's license).
- **[Dear ImGui](https://github.com/ocornut/imgui)** (MIT), v1.91.5, cloned by
  `scripts/setup.sh`: the start screen, the in-game menu and performance HUD.
- **[libpng](http://www.libpng.org)** (libpng license), optional: the start
  screen's backdrop, the game's own PIC1.PNG.
- **[SDL2](https://www.libsdl.org)** (zlib): window (Linux), pad and audio.
- **[glslang](https://github.com/KhronosGroup/glslang)** and the
  **Vulkan** headers and loader (Khronos): shader translation and rendering.
- **[SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)** (Apache-2.0):
  on macOS, the game's shaders from SPIR-V to the Metal Shading Language.
- **Metal** and **MetalFX** (Apple): rendering and upscaling on macOS. The
  Metal backend is ps3recomp's, extended by this port's patch.
