=====================================================================
  DLSS5 FOR ALL  -  AMD RADEON CARDS (EXPERIMENTAL)
  folder: bin\mod-amd
=====================================================================
  (Versão em português: LEIA-ME.txt)

  THIS IS EXPERIMENTAL. It may not work on your card, it may run
  slowly and it may break when the mod ships a new version. The
  safe path is still a GeForce RTX.

  Neural Rendering (DLSS 5) on a Radeon is done by the
  DLSS-NR-on-AMD mod, by Daniel Blanco (danielblnc):
      https://github.com/danielblnc/DLSS-NR-on-AMD
  The mod is NOT shipped with this program, as its license requires:
  you download it from the author and install it in this folder.
  DLSS5 For All only loads the mod when you pick the Radeon.

  Without the mod (or on a card it does not cover) the program opens
  anyway: Frame Gen and filters work, only DLSS 5 is unavailable,
  with the reason shown in the panel.


1. WHICH CARDS THE MOD COVERS
---------------------------------------------------------------------
- Radeon RX 9000 (RDNA4) and RX 7000 (RDNA3), with AMD Adrenalin
  26.1.1 or newer (it ships the HIP runtime the mod uses).
- Radeon RX 6000 (RDNA2): from mod v0.6.0 on, which needs the AMD
  HIP SDK 7.2 installed separately:
      https://www.amd.com/en/developer/resources/rocm-hub/hip-sdk.html
  NOT TESTED by this program.
- Windows 11, 64-bit.
- The official, current list is on the mod's page (link above): it
  rules, not this file.
- iGPUs (Radeon Graphics, 780M...), RX 5000 and older: no.


2. GET THE MOD INSTALLER (OFFICIAL PAGE ONLY)
---------------------------------------------------------------------
Download dlssnr_on_amd_setup.exe ONLY from the author's releases:
    https://github.com/danielblnc/DLSS-NR-on-AMD/releases
Files from forums, Discord or "packs" were audited by nobody. Use
the newest version that covers your card.

Antivirus: some antivirus tools flag the mod's installer (a known
issue of that project). Trusting it is your call. If you use it, add
this folder (bin\mod-amd) to the exclusions before running the
installer, or the antivirus deletes version.dll.


3. INSTALL HERE, IN bin\mod-amd (once)
---------------------------------------------------------------------
a) Copy dlssnr_on_amd_setup.exe into THIS folder (bin\mod-amd).

b) The installer needs nvngx_dlssnr.dll NEXT TO IT (it takes the
   network weights from it). If it is already in bin\, copy it here
   - or double-click preparar-mod-amd.cmd (in this folder), which
   checks everything and offers that copy.

c) Double-click dlssnr_on_amd_setup.exe, IN HERE.
   It writes into this folder:
       version.dll                 (the mod itself)
       dlssnr_on_amd.ini           (its options)
       dlssnr_on_amd_weights.bin   (the network weights)
   If it asks which name to install as (for example
   "Install as dxgi.dll?"), answer  version  - never dxgi.
   The dxgi.dll in bin\ is ReShade; nothing works without it.

d) Double-click preparar-mod-amd.cmd. It checks Windows, driver,
   card, the files in this folder and the mod's ini, and tells you
   what is missing (its messages are in Portuguese). It downloads
   nothing and does not run the installer for you.

When the mod ships a new version: download it again (step 2) and
repeat step 3 in this folder.

Updated from 1.2/2.0 and the mod is in the old bin-amd\ folder? The
program still finds it there, but this is the new place.
preparar-mod-amd.cmd offers to COPY the mod files from bin-amd\ to
here (nothing is deleted; you may delete bin-amd\ yourself later).


4. USE
---------------------------------------------------------------------
Open the program with DLSS5ForAll.exe in the main folder (the
launcher) and click START. There is no AMD button: with the mod
installed and a covered Radeon, the launcher already opens in AMD
mode. On the selection screen, under "Graphics card that runs the
filter", pick the Radeon. If another card was selected, the program
reopens by itself in AMD mode (and loads the mod from this folder).
Everything else follows the normal tutorial. In AMD mode the panel
opens with Insert; the End key is the mod's own menu.


5. HOW TO TELL IT WORKS (and where the logs are)
---------------------------------------------------------------------
- Inside the program, F6 toggles DLSS 5: the image must change. The
  panel shows the mod's state.
- bin\dlss5forall.log (the program's log) must contain:
      mod AMD: carregado de ...\bin\mod-amd\version.dll
- bin\mod-amd\dlssnr_on_amd.log (the mod's log) must contain:
      engine init ok
      network job 1 done in ... ms
  "network job" means the network runs on your Radeon.
  (If the mod's log is not here, look for it in bin\.)

dlssnr_on_amd.log lines that point at the problem:
  "UNSUPPORTED GPU ARCHITECTURE" or "invalid kernel file"
      -> the installed mod version has no code for your card. Check
         the mod's page for the version that covers it.
  "amdhip64_7.dll ... not found"
      -> the HIP runtime is missing: Adrenalin (RX 7000/9000) or
         HIP SDK 7.2 (RX 6000).
  "no HIP device matches the game's adapter"
      -> the program runs on the other card. Pick the Radeon under
         "Graphics card that runs the filter".
  "setup failed; idle", "route backbuffer" or "FSR is not active"
      -> the mod did not catch the FSR the program turns on in AMD
         mode. Run preparar-mod-amd.cmd and send its output.
  "unsupported colour format 87"
      -> in bin\dlss5-feed.cfg change amd_color_fmt=87 to 10.
  "inline: N GPU wait timeouts so far"
      -> not an error: the network is slow for the wait budget.
         Lower DLSS Resolution in the panel.


6. BACK TO NVIDIA / UNINSTALL
---------------------------------------------------------------------
- Back to NVIDIA: under "Graphics card that runs the filter", pick
  the GeForce. The program reopens in NVIDIA mode, does NOT load the
  mod and undoes the AMD-mode settings by itself (the amd_* keys of
  dlss5-feed.cfg and RenoDX turned off in ReShade). Nothing to do.
- Uninstall the mod: delete from this folder the files the installer
  created (version.dll, dlssnr_on_amd.ini, dlssnr_on_amd_weights.bin,
  dlssnr_on_amd.log) and, if you want, dlssnr_on_amd_setup.exe and
  the nvngx_dlssnr.dll copy. Keep this README. Without the mod here
  the program no longer enters AMD mode.


CREDITS
---------------------------------------------------------------------
- DLSS-NR-on-AMD: Daniel Blanco (danielblnc),
  https://github.com/danielblnc/DLSS-NR-on-AMD
  Not shipped with this program, as the author's license requires.
  All credit for the network running on a Radeon is his.
- FidelityFX SDK (amd_fidelityfx_dx12.dll, in bin\): AMD, MIT license
  (LICENSE-FidelityFX.txt). FSR and Radeon are AMD trademarks.
- DLSS5-Feeder: jlrouzies-fr (MIT).
- nvngx_dlssnr.dll: NVIDIA. Do NOT redistribute.
This project is not affiliated with AMD, NVIDIA or the mod's author.
