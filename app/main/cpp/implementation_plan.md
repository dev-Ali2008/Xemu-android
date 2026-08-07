# HakuX Vulkan Port implementation

The current Vulkan renderer on the `xemu` Android port experiences texture flickering, black textures, and missing rendering elements (like loading icons, text, etc). 

Based on a comparative analysis against the `hakuX` fork, the root cause traces back to how the Android Vulkan texture formats and surfaces are handled. `hakuX` implements a robust rendering pipeline utilizing generation counters, bindless descriptor indexing, native 16-bit packing formats (e.g. `VK_FORMAT_A1R5G5B5_UNORM_PACK16`), and surface general layout sampling. Conversely, `xemu` relies on experimental CPU-side pixel conversions to fall back to generic `RGBA8` formats on Android. These workarounds in `xemu` are explicitly causing the black textures.

Additionally, `hakuX` features around ~20,000 lines of differences within the `hw/xbox/nv2a/pgraph/vk` rendering core specifically optimized for tiled Android GPUs (Adreno, Mali) and async pipeline processing.

## Proposed Changes

To accurately "copy what they did" and guarantee the same stable results for Android Vulkan, we will sync the heavily-optimized `hakuX` graphics backend directly over `xemu`'s implementation.

### PGRAPH and NV2A Graphics Pipeline Core

We will synchronize the modified Vulkan module and its heavily coupled PGRAPH CPU dependencies.

#### [MODIFY] `hw/xbox/nv2a/pgraph/vk/` (Entire Directory)
- Replace all files with the upstream `hakuX` implementation.
- This includes dropping the problematic Android CPU texture format conversions in `constants.h` and adopting `hakuX`'s surface/texture generational caches, async shader compilers, and deferred frame pacing loops.
- New files added by `hakuX`: `render_thread.c` and `submit_worker.c`.

#### [MODIFY] `hw/xbox/nv2a/pgraph/texture.c`
- Remove the unreliable `A1R5G5B5` and `A4R4G4B4` CPU texture upscaling methods specific to the current `xemu` build.

#### [MODIFY] `hw/xbox/nv2a/pgraph/pgraph.c` & `hw/xbox/nv2a/pgraph/pgraph.h`
- Synchronize state declarations and Vulkan caching hooks introduced by `hakuX`.

#### [MODIFY] `hw/xbox/nv2a/nv2a.c` & `hw/xbox/nv2a/nv2a_int.h`
- Ensure integration matches `hakuX`'s execution flow, particularly concerning asynchronous compilation polling.

## User Review Required

> [!WARNING]
> Replacing the Vulkan backend directory is a major architectural overwrite. It will overwrite the `RGBA8` pixel fallback commits recently merged into the Android port of `xemu` in favor of `hakuX`'s native implementation. Please confirm that you are okay with forcefully overriding `hw/xbox/nv2a/pgraph/vk/` with the `hakuX` variant.

## Verification Plan

### Automated Tests
- The build process will be verified statically to ensure successful C linkage and no regression in dependencies.

### Manual Verification
- Compile the updated app to Android (`vulkan` driver selected).
- Validate that the black flickering textures are resolved and all 2D UI elements, including loading icons and words, render correctly during game boot.
