# M6.7 progress: the whole picture on the Babylon renderer

All three M6.1 captures, replayed through the Babylon column's `std3D` and `stdDisplay` on SDL GPU (Vulkan on
Mesa lavapipe, no window) with `Jones3D-replay --render gpu --frames-out`, beside the DX9 screenshots recorded in
M6.1 (wine, wined3d on llvmpipe). The replays now run to the end with no mismatch: canyon 169,417 records and
32 frames, intro 267,535 records and 2,014 frames, indoor 310,790 records and 1,077 frames. The 2D layer (console
text, the intro movie's frames) is in.

Each DX9 screenshot is paired with the replayed frame that looks most like it. The screenshots were taken by
time, so during the intro movie, which zooms and pans continuously, the nearest frame is a little earlier or later
than the screenshot: those pairs have a low PSNR although the movie plays correctly. In-game pairs reach
41 to 59 dB.

Recorded 2026-11-06 at `1045fe16` (M6.7).

## Intro: movie, title, Canyonlands start

| DX9 (M6.1) | Babylon (M6.7) | Frame | PSNR | Pixels differing by more than 3% |
|---|---|---|---|---|
| ![](../M6.1/intro/s06.png) | ![](intro/babylon-00073.png) | 73 | 16.9 dB | 17.68% |
| ![](../M6.1/intro/s12.png) | ![](intro/babylon-00129.png) | 129 | 17.9 dB | 15.60% |
| ![](../M6.1/intro/s18.png) | ![](intro/babylon-00265.png) | 265 | 10.9 dB | 82.59% |
| ![](../M6.1/intro/s24.png) | ![](intro/babylon-00341.png) | 341 | 8.6 dB | 77.62% |
| ![](../M6.1/intro/s30.png) | ![](intro/babylon-00435.png) | 435 | 9.9 dB | 52.11% |
| ![](../M6.1/intro/s36.png) | ![](intro/babylon-00490.png) | 490 | 58.7 dB | 0.00% |
| ![](../M6.1/intro/s42.png) | ![](intro/babylon-00512.png) | 512 | 58.4 dB | 0.00% |
| ![](../M6.1/intro/s48.png) | ![](intro/babylon-01351.png) | 1351 | 46.5 dB | 0.41% |
| ![](../M6.1/intro/s54.png) | ![](intro/babylon-01943.png) | 1943 | 45.1 dB | 0.64% |
| ![](../M6.1/intro/s60.png) | ![](intro/babylon-01968.png) | 1968 | 41.4 dB | 1.69% |
| ![](../M6.1/intro/s66.png) | ![](intro/babylon-01985.png) | 1985 | 41.6 dB | 1.42% |
| ![](../M6.1/intro/s72.png) | ![](intro/babylon-01994.png) | 1994 | 41.9 dB | 1.33% |
| ![](../M6.1/intro/s78.png) | ![](intro/babylon-02000.png) | 2000 | 41.7 dB | 1.35% |
| ![](../M6.1/intro/s84.png) | ![](intro/babylon-02006.png) | 2006 | 41.6 dB | 1.32% |

## Indoor: second movie, Palawan Volcano interior

| DX9 (M6.1) | Babylon (M6.7) | Frame | PSNR | Pixels differing by more than 3% |
|---|---|---|---|---|
| ![](../M6.1/indoor/s010.png) | ![](indoor/babylon-00127.png) | 127 | 18.2 dB | 14.09% |
| ![](../M6.1/indoor/s020.png) | ![](indoor/babylon-00318.png) | 318 | 9.0 dB | 91.42% |
| ![](../M6.1/indoor/s030.png) | ![](indoor/babylon-00435.png) | 435 | 9.9 dB | 52.11% |
| ![](../M6.1/indoor/s040.png) | ![](indoor/babylon-00503.png) | 503 | 55.5 dB | 0.00% |
| ![](../M6.1/indoor/s050.png) | ![](indoor/babylon-00531.png) | 531 | 55.3 dB | 0.00% |
| ![](../M6.1/indoor/s060.png) | ![](indoor/babylon-01034.png) | 1034 | 43.0 dB | 1.48% |
| ![](../M6.1/indoor/s070.png) | ![](indoor/babylon-01050.png) | 1050 | 51.6 dB | 0.11% |
| ![](../M6.1/indoor/s080.png) | ![](indoor/babylon-01061.png) | 1061 | 41.1 dB | 1.58% |
| ![](../M6.1/indoor/s090.png) | ![](indoor/babylon-01067.png) | 1067 | 41.0 dB | 1.61% |
| ![](../M6.1/indoor/s100.png) | ![](indoor/babylon-01073.png) | 1073 | 41.0 dB | 1.62% |

## Canyon: HUD and console text

| DX9 (M6.1) | Babylon (M6.7) | Frame | PSNR | Pixels differing by more than 3% |
|---|---|---|---|---|
| ![](../M6.1/canyon/s32.png) | ![](canyon/babylon-00002.png) | 2 | 44.5 dB | 0.77% |
| ![](../M6.1/canyon/s36.png) | ![](canyon/babylon-00005.png) | 5 | 44.6 dB | 0.75% |
| ![](../M6.1/canyon/s43.png) | ![](canyon/babylon-00012.png) | 12 | 44.2 dB | 0.77% |
| ![](../M6.1/canyon/s46.png) | ![](canyon/babylon-00014.png) | 14 | 44.8 dB | 0.48% |
| ![](../M6.1/canyon/s52.png) | ![](canyon/babylon-00013.png) | 13 | 45.8 dB | 0.42% |
| ![](../M6.1/canyon/s57.png) | ![](canyon/babylon-00025.png) | 25 | 44.5 dB | 0.77% |

## Not yet

- Cutscene timing: frames are compared by image, so the movie's playback speed isn't checked here (M6.8).
- A windowed run with the real swapchain; the parity review with the owner (M6.9).
