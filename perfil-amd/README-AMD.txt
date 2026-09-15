=====================================================================
  DLSS5 FOR ALL  -  AMD EXPERIMENTAL
=====================================================================
  (Versão em português: LEIA-ME-AMD.txt)

  THIS IS EXPERIMENTAL. It may not work on your card, it may run
  slowly, and it may break when the mod ships a new version. If you
  want something that just works, the guaranteed path is still a
  GeForce RTX with the bin\ folder.

  Tested by PC do Fafa on a Radeon RX 9060 XT on 2026-09-11, with
  mod v0.2.18: the neural network does run. Not tested in a game,
  only with video and on the test bench.


WHAT THIS IS
---------------------------------------------------------------------
DLSS 5 (Neural Rendering) is NVIDIA's and only runs on their cards.
The "DLSS-NR-on-AMD" mod (by danielblnc) makes the DLSS 5 neural
network run on a Radeon: it hooks into a game that uses FSR and
replaces the FSR result with the image after the network.

Our AMD module makes the app behave like that "game with FSR": the
captured image goes through a real FSR 3.1 (AMD's official DLL, MIT
licensed), and the mod picks it up from there. Everything you see
and use in the app (source, overlay, panel, F6, effects) is the same
as the NVIDIA module.


REQUIREMENTS
---------------------------------------------------------------------
- Windows 11, 64-bit
- AMD Adrenalin driver 26.1.1 or newer (it ships the HIP runtime the
  mod uses)
- Radeon RX 7000 (RDNA3) or RX 9000 (RDNA4)
    RX 9060 XT: works from mod v0.2.14 on, which added the kernels
    for its chip (gfx1200). The installer shipped in this folder is
    v0.2.18, which has that and is the version we tested on that
    card. A forum "patch" that edits the mod's binary for the 9060
    XT exists: we do not do that and do not ship it, and with
    v0.2.18 it is not needed.
    Speed: the network's cost follows the SIZE of the image. In our
    2026-09-11 measurement, over a 634x669 area, the network took
    about 13 ms per frame. For full 1080p the mod's author quotes
    about 60 ms per frame, which is the 15 to 20 fps he cites for
    the RX 9060 XT (about 31 fps on an RX 9070 XT). If it feels
    slow, lower the DLSS Resolution in the panel: the cost falls
    with the area.
- The file nvngx_dlssnr.dll version 310.8.0.0 (the same one the
  NVIDIA chain uses). It is a leaked NVIDIA build: NOT included in
  the package and not redistributable. You bring your own copy.
- The mod's installer (dlssnr_on_amd_setup.exe, v0.2.18 or newer). It
  is NOT included in the package — it is third-party. Download it at
    https://github.com/danielblnc/DLSS-NR-on-AMD/releases/latest
  and copy it into this folder. preparar-mod-amd checks its version
  and SHA-256, and warns if it is not from an official release.

  WHEN YOU RUN THE INSTALLER, WATCH FOR ONE THING:
  at the "Install as dxgi.dll?" prompt, answer  version
  It notices the app loads dxgi.dll and offers to take that name.
  Here dxgi.dll IS RESHADE: accepting wipes ReShade and brings the
  whole chain down (no ReShade means no Feeder, no Feeder means no
  FSR, and without FSR the mod has nothing to hook into).


WHAT IS ALREADY IN THIS FOLDER (bin-amd)
---------------------------------------------------------------------
DLSS5ForAll.exe           the app (same as in bin)
dxgi.dll                  ReShade
dlss5-bridge.addon64      the bridge between the app and ReShade
dlss5-feed.addon64        the Feeder, AMD variant (FSR carrier)
amd_fidelityfx_dx12.dll   AMD's official FSR 3.1 (MIT)
dlssnr_on_amd.ini         the mod settings we tested
ReShade.ini, FafaDLSS5.ini, dlss5-feed.cfg, dlss5forall.ini
                          settings already on the AMD profile
reshade-shaders\          the shaders
preparar-mod-amd.cmd      checks this folder and tells you what's missing
LEIA-ME-AMD.txt, README-AMD.txt, LICENSE, CREDITOS.md,
LICENSE-FidelityFX.txt

Not here (on purpose): renodx-dlss5.addon64 and nvngx_dlss.dll - on
AMD they do nothing - nor dlssnr_on_amd_setup.exe and nvngx_dlssnr.dll,
which are third-party and you bring yourself (steps 1 and 2).


STEP BY STEP (one time only)
---------------------------------------------------------------------
1. Put YOUR nvngx_dlssnr.dll inside this folder (bin-amd).
   If you already use the NVIDIA chain, it is in bin\ : just copy it.

2. Download dlssnr_on_amd_setup.exe from the page above and copy it
   into THIS folder (bin-amd). It has to sit next to nvngx_dlssnr.dll.

3. Double-click dlssnr_on_amd_setup.exe.
   It reads nvngx_dlssnr.dll, extracts the network weights and
   writes here:
       version.dll                 (the mod itself)
       dlssnr_on_amd.ini           (its options)
       dlssnr_on_amd_weights.bin   (the weights, about 160 MB)
   It recognises build 310.8.0.0 outright; any other one (say a
   "310.8.SF.0") it checks piece by piece and only accepts if
   everything matches. If it refuses, look for 310.8.0.0.

4. Open dlssnr_on_amd.ini (Notepad) and check:
       Inline=1
   The installer already writes it that way. It has to be 1 because
   our window is D3D11 and the mod's "async" mode (which applies on
   the game's Present) does not apply to it. With Inline=0 the image
   comes out without the network.
   Also keep Interop=1, UseFsrInputs=1, UseDepth=1, HipDevice=-1.

5. Double-click preparar-mod-amd.cmd. It checks all of this (Windows,
   driver, card, files, the ini, the cfg) and tells you what is
   missing (in Portuguese). If it says "TUDO NO LUGAR" (all in place),
   close it and go to step 6.

6. Go back to the folder above and open the DLSS5ForAll.exe there.
   Click the red button USE DLSS 5 ON AN AMD CARD. The rest is the
   normal tutorial (README.txt): source, overlay, START.
   Two cards in the PC? Under "Graphics card that runs the filter"
   pick the Radeon. The mod only sees the Radeon; if the app runs on
   the other card, the mod never finds the frame.

When the mod ships a new version, just repeat steps 2 and 3 in this
folder. Nothing else changes.


ANTIVIRUS
---------------------------------------------------------------------
Some antivirus products flag the mod's installer (a known issue,
issue 40 on the mod's project). It is a closed program with no
source published yet; trusting it is your decision. If you do, add
this folder to the exclusions before running the setup, otherwise
the antivirus deletes version.dll on the spot.


HOW TO KNOW IT IS WORKING
---------------------------------------------------------------------
- Inside the app, F6 toggles DLSS 5: the image has to change.
- The file dlssnr_on_amd.log (in this folder) must contain
      engine init ok
      network job 1 done in ... ms
  If "network job" is there, the network is running on your Radeon.
- preparar-mod-amd.cmd reads those logs and explains them.

Lines in dlssnr_on_amd.log that point at the problem:
  "UNSUPPORTED GPU ARCHITECTURE gfx1200" or "invalid kernel file"
      -> RX 9060 XT with a mod older than v0.2.14. Get v0.2.14 or
         newer once it shows up on the releases page and repeat
         steps 2 and 3. There is no other tweak.
  "setup failed; idle" or "route backbuffer" (with no "route fsr")
      -> the mod did not catch FSR: it saw the screen but not the
         frame the Feeder hands over. Check that dlss5-feed.addon64
         (AMD variant) and amd_fidelityfx_dx12.dll are in this
         folder and that amd_carrier=1 in dlss5-feed.cfg. If the log
         also says "unsupported colour format", see the next item.
  "unsupported colour format 87"
      -> open dlss5-feed.cfg and change amd_color_fmt=87 to 10.
  "FSR is not active"
      -> dlss5-feed.addon64 (AMD variant) or amd_fidelityfx_dx12.dll
         is missing, or amd_carrier is not 1.
  "no HIP device matches the game's adapter"
      -> the app is running on the other card. Pick the Radeon in
         the selector ("Graphics card that runs the filter").
  "d3d11 device yes (this game renders with D3D11: unsupported)"
      with no "network job" afterwards
      -> check that amd_decoy_swapchain=1 in dlss5-feed.cfg (this
         folder already ships it that way). It creates a hidden
         D3D12 swapchain just so the mod adopts the right device.
         With 0 the network does NOT run and the picture comes out
         as plain FSR.

  dlssnr_on_amd.log stops at the "hooked IDXGI..." lines and says
  nothing else (no "engine init", no "network job")
      -> same case as above: amd_decoy_swapchain must be 1.
  "inline: N GPU wait timeouts so far"
      -> not an error: the network is taking longer than the wait
         cap. Lower the DLSS resolution in the panel (End).

Symptoms reported by people using the mod (2026-09-05):
  - Black screen with only the UI visible on Adrenalin 26.9.1: try
    the previous driver or mod v0.2.10.
  - The mod's menu (End key) does not open if MSI Afterburner was
    started before the app: close Afterburner or start it later.
  - Flickering image with the integrated GPU enabled: disable the
    iGPU in Device Manager and try again.
  - If it still flickers, in dlssnr_on_amd.ini change Temporal=1 to
    Temporal=0 (that is what the community uses on the RX 9060 XT).


SETTINGS (dlss5-feed.cfg)
---------------------------------------------------------------------
Four keys belong to the AMD module; the others match bin\ :
  amd_carrier=1          1 forces the FSR carrier (keep 1 here)
  amd_color_fmt=87       colour format handed to the mod
                         87 = the app's own; 10 = RGBA16F, if the
                         mod rejects 87
  amd_decoy_swapchain=1  1 is the right value here. With 0 the mod
                         never arms the FSR hook and the network
                         does not run
  amd_srgb=-1            -1 automatic; only touch it if the image
                         comes out washed out or too dark (0 or 1)
The ReShade overlay (Home) rewrites this file keeping the four keys
but dropping the comments; that is normal.


PERFORMANCE
---------------------------------------------------------------------
The network's cost follows the SIZE of the image it is handed. That
is the one thing you control, and it controls a lot.

Measured by us on 2026-09-11, RX 9060 XT, mod v0.2.18: over a
634x669 area the network took about 13 ms per frame, with no wait
timeouts at all. The mod author's numbers for full 1080p: about 60
ms per frame on an RX 9060 XT (15 to 20 fps) and about 31 fps on an
RX 9070 XT. The network is the bottleneck, not the game. In the app:
  1. Panel (Insert) > DLSS resolution: 77% or 67%. This is what
     helps most: less area, less work for the network.
  2. Window source instead of Monitor.
  3. Filter fps cap at 30.
  4. In dlssnr_on_amd.ini, InlineWaitMs=25 (mod v0.2.18 and up).
     It caps how long a frame waits for the network; past that, the
     mod shows the previous frame's touch-up instead of holding the
     picture.
The game underneath keeps its own speed; only the DLSS 5 layer
gets slow.


LIMITS OF THIS VERSION (why it is experimental)
---------------------------------------------------------------------
- It depends on a closed, third-party mod that changes every week.
- The meeting with the mod on a real Radeon was confirmed once: RX
  9060 XT, mod v0.2.18, 2026-09-11, with amd_color_fmt=87 and
  amd_decoy_swapchain=1. That was with video on screen, NOT a game.
- The cost of having the game on one card and the filter on the
  other was never measured: the capture crosses the bus between them.
- The AMD Feeder is built with a different compiler and lacks the
  crash safety net the NVIDIA Feeder has.
- The app does not yet read dlssnr_on_amd.log by itself in every
  situation; once it does, the selector itself will warn you.


CREDITS FOR THIS MODULE
---------------------------------------------------------------------
- DLSS-NR-on-AMD: danielblnc (https://github.com/danielblnc/DLSS-NR-on-AMD)
  Not redistributed here. You download it from the author.
- FidelityFX SDK 1.1.4 (amd_fidelityfx_dx12.dll): AMD, MIT license
  (LICENSE-FidelityFX.txt). FSR and Radeon are AMD trademarks.
- DLSS5-Feeder: jlrouzies-fr (MIT). The AMD variant is our
  modification; the credit for the Feeder goes to its author.
- nvngx_dlssnr.dll: NVIDIA. Leaked internal build. Do NOT distribute.
This project is not affiliated with AMD, NVIDIA or the mod's author.
