=====================================================================
  DLSS5 FOR ALL  -  PORTABLE VERSION
  by PC do Fafa
=====================================================================
  (Versão em português: LEIA-ME.txt)
  Channel: youtube.com/@pcdofafa   ·   Site: pcdofafa.com.br/dlss


WHAT THIS IS
---------------------------------------------------------------------
Applies DLSS 5 (NVIDIA Neural Rendering) on top of anything that
shows up on your screen: a windowed game, an emulator, a video,
a whole monitor, or a capture card / webcam.

Nothing is installed into Windows and nothing is injected into the
game. Everything runs from inside this folder.


WHAT IS IN THE FOLDER
---------------------------------------------------------------------
DLSS5ForAll.exe   ->  THIS IS WHAT YOU OPEN.
                      Shows the community screen with the mode buttons
                      (NVIDIA card / AMD card) and starts the right program.
README.txt        ->  this file
LEIA-ME.txt       ->  the same tutorial in Portuguese
bin\              ->  the program for NVIDIA cards and everything it needs.
bin-amd\          ->  the program for AMD cards (when your build ships it;
                      without this folder the AMD button stays grey).
                      You don't need to go into either day to day.


REQUIREMENTS
---------------------------------------------------------------------
- Windows 10 (1903 or newer) or Windows 11, 64-bit
- GeForce RTX card (20 series or newer) with an up-to-date driver,
  or a Radeon RX 7000 / RX 9000 with the AMD module (in development)
- The game running in WINDOWED or BORDERLESS mode
  (exclusive fullscreen does NOT work: Windows won't let it be captured)


HOW TO USE IN 5 STEPS
---------------------------------------------------------------------
1. Open the game. Set it to windowed or borderless.

2. Double-click DLSS5ForAll.exe.
   The community screen shows up. Below the community buttons there are two:
     USE DLSS 5 ON AN NVIDIA CARD   (green)
     USE DLSS 5 ON AN AMD CARD      (red)
   The one your PC can't use is grey, with the reason written on it.
   If both are grey, OPEN WITHOUT DLSS 5 appears: the program still opens,
   with the effects only (sharpness, anti-aliasing...).

3. In the selector, pick the SOURCE:
     Window   -> pick the game from the list.  LIGHTEST. (recommended)
     Monitor  -> the whole screen goes through the filter.
     Camera / Capture card -> webcam, ezcap, DroidCam etc.
   More than one graphics card? Under "Graphics card that runs the filter"
   you pick which one runs DLSS 5 — the other stays free for the game.
   With a single card the line just shows its name.

4. Decide how you want to see the result:
     [x] OVERLAY the target  -> the filter sits ON TOP of the game.
                                Looks like DLSS 5 is running inside it.
                                Mouse and keyboard still go to the game.
     [ ] OVERLAY the target  -> the result opens in a SEPARATE WINDOW.
                                Good for recording and side-by-side.

5. Click START. Done.
   The game keeps running normally underneath; what you see is its
   image after DLSS 5.

Your last choice is remembered: next time just click START.


KEYS
---------------------------------------------------------------------
End       open / close the PANEL (everything is adjusted there, with the mouse)
F6        DLSS 5 on / off  ->  the "before and after"
Home      open the ReShade overlay (fine tuning)
F7        open ReShade AND hand it the mouse/keyboard;
          press again to close and give them back to the game
Alt+Esc   quit the app (press TWICE within 2 seconds)
Esc       NEVER quits the app, only closes whatever screen is open

Every key can be changed in the "Shortcuts..." button of the selector
or in the SHORTCUTS section of the panel, under "More options".

IF THE SHORTCUT DOES NOT WORK INSIDE THE GAME
Some games hold on to the Windows hotkeys while they are in front.
The program has two ways around that, and both are on by default:
  - a fallback path: if you press the key and Windows does not
    deliver the hotkey, the program recognises the key anyway, in
    about a quarter of a second;
  - the TRAY ICON, next to the clock: its menu opens the panel
    without depending on any key.
If the game runs as administrator and the program does not, no key
reaches it — the program says so on screen, and the way out is to
start DLSS5 For All as administrator too (or use the tray).


THE PANEL (End key)
---------------------------------------------------------------------
The first screen of the panel has only what you change mid-game. The
rest lives under "More options", which opens a second column beside
it. Hovering over any control shows a bubble explaining what it does.

- DLSS 5: ENABLED / DISABLED (same as F6).
      The program ALWAYS OPENS ON, at 1 pass. Turning it off with F6
      lasts for the session: close and open again and it comes back
      on.

- Image profile (neural network): a card with a summary of what is
      in effect ("Model B · Intensity 100% · 2 passes") and the
      ADJUST button, which opens a window just for this, with the six
      network controls: MODEL (A / B / C), INTENSITY, STRUCTURE,
      SKIN AND FACES, LOCAL TONE and GLOBAL TONE.
      The Neural Rendering add-on reads this profile once, when it
      loads, and does not read it again while running. That is why
      the program reopens itself after you choose: it waits two and a
      half seconds (change several controls and it reopens once, at
      the end), tells you on screen, and comes back with the same
      window, the same GPU and the same settings. To turn that off,
      use perfil_reabre=0 in dlss5forall.ini — the choice is then
      saved and takes effect when you reopen the program yourself.

- NETWORK PASSES: 1 to 10. This is how many times the neural network
      redoes the SAME frame. More passes, more network and more cost:
      measured here at about 4.2 ms of GPU per pass. It replaces the
      old "1x / 2x / 3x". It takes effect the same way as the profile
      (the program reopens itself).

- DLSS FRAME SKIPPING: Off / 1 in 2 / 1 in 3 / 1 in 4. The network
      processes one frame in every N and the program DRAWS the
      others, projecting the scene from its motion — it is not frame
      repetition, and the raw game does not show through. This is how
      you ease the GPU load without losing rhythm on screen.
      The synthesized motion is automatic: there is nothing to turn
      on. When the base frame or the motion field is missing, the
      program holds the last good frame by itself, frame by frame,
      with no flicker.

- DLSS 5 SMOOTHNESS ON SCREEN: [−] 60 [+] and the UNLOCK button.
      This is how many frames per second the filtered image presents.
      It does NOT change the game's fps.
      BELOW what the game delivers the number is EXACT: the image
      stops there.
      ABOVE it, the program projects extra frames from motion — and
      that is EXPERIMENTAL: delivery is PARTIAL and depends on the
      GPU's headroom. Measured here with the game at 60: asking 90
      gave 70; asking 120 gave 73. It does go up, but it does not
      reach the number you asked for. With no headroom at all the
      program backs off by itself.
      UNLOCK (it shows ∞) does not turn the projection off: with no
      number chosen, the target becomes the most your screen can
      show. The − still works in the unlocked state and goes back to
      the last number you used. Hold − or + to repeat.
      Below it, one line says what is actually happening:
      "game 60/s · layer 74/s · 26 projected per second" — and that
      layer number counts EVERYTHING that reaches the screen:
      processed, synthesized by frame skipping, and projected.

- DLSS RESOLUTION: Auto / 100% / 77% / 67% / 58%. This is the size
      the network works at: the lower, the cheaper it gets. It
      applies right away, with about two seconds of rebuilding.

- More options: Effects (sharpness and anti-aliasing), V-Sync, HDR,
      FPS on screen, COMPARISON MODE, NEURAL NETWORK ENGINE, game FPS,
      language, shortcuts, the group and channel links, Open ReShade,
      CHANGE SOURCE OR GPU and Quit.

- IN AMD MODE the engine is the DLSS-NR-on-AMD mod, and there is no
      choice: the panel says so and says where the settings land.
      LOCAL TONE, STRUCTURE and SKIN AND FACES go to
      dlssnr_on_amd.ini and apply when the program reopens. MODEL,
      INTENSITY and PASSES do not exist in the mod; GLOBAL TONE does,
      but the mod's own author warns that this build keeps it off and
      that raising it only darkens the image. All four show greyed
      out, with the reason next to them — before, they looked like
      they worked and changed nothing.
      To GAIN FPS in AMD mode the control is DLSS RESOLUTION, on the
      panel's first screen: size is what costs. Measured on VIDEO on
      screen (not in a game), on an RX 9060 XT: 13.5 ms per frame of
      network alone.

- PER-FRAME WAIT BUDGET (AMD mode only): [−] 25 ms [+].
      This is how long a frame waits for the network before the mod
      gives up and shows the previous frame's touch-up. LOWER is
      smoother, with residue from the previous frame when the network
      is late; HIGHER is more faithful and gives fewer fps. 25 ms is
      the value proven on an RX 9060 XT — the mod ships at 200. It
      only shows up if the installed mod is version 0.2.18 or newer,
      which is when that key started to exist.

- NEURAL NETWORK ENGINE: RenoDX (default) or Built-in (experimental).
      Two implementations of the SAME network; the program reopens
      once to switch. The built-in one IS NOT FASTER — the cost is the
      same, measured. What changes: it blurs far less when you use
      many passes, and "Image profile" changes apply RIGHT AWAY,
      without the program reopening every time.
      With the built-in engine you also get NEURAL NETWORK SIZE:
      Light 65% / 100% / 125% / 150% / 200%. Do not confuse it with
      the "DLSS resolution" on the first screen: that one is the size
      everything works at, this is the network's only. "Light 65%"
      buys 21% to 34% more fps at 4 to 10 passes; above 100% it costs
      a lot (about 2.4x at 200%) AND the network changes the image
      less — if you want more effect, raise the passes at 100%
      instead of raising this.

- CHANGE SOURCE OR GPU (at the bottom, next to Quit): goes back to
      the selection screen, the one from the start, without you
      having to close the program. It reopens itself (the filter has
      to be built again) and the screen comes back with the current
      window, GPU and language already picked. If the window you
      were filtering gets closed, the program goes back to that
      screen on its own.

TOO HEAVY?
  1. Turn DLSS FRAME SKIPPING on, at "1 in 2".
  2. Lower the "DLSS resolution" (77% or 67%).
  3. Leave NETWORK PASSES at 1.
  4. Use the Window source instead of Monitor.
  F6 turns it off instantly so you can compare the cost.

HDR: if the monitor is in HDR, the app converts automatically.
Nothing to set up.

COMMON PROBLEMS
---------------------------------------------------------------------
"The image looks the same, it does nothing"
   One of these three is missing from bin\: renodx-dlss.addon64,
   nvngx_dlssnr.dll or nvngx_dlss.dll. The selector shows a banner
   naming WHICH one, with a button that opens the page to get them
   (RHI installer) and one that opens the bin folder.
   nvngx_dlss.dll is the easiest to forget: without it DLSS never
   even starts, and the screen looks exactly like the original, with
   no error at all.

"The filter disappears when I click outside the game"
   Normal in overlay mode: it only shows while the game has focus.

"In overlay mode the keyboard does nothing in the app"
   By design: mouse and keyboard go to the game.
   End opens the panel; F7 hands the keyboard to ReShade.

"ReShade does not open with Home"
   The key was changed. Check "Shortcuts..." (the "Open ReShade"
   line) in the selector.

"Nothing opens, no window at all"
   Open bin\dlss5forall.log: it says where it stopped.

"Stuck at 50 fps"
   A virtual monitor is installed (Parsec, Meta, Virtual Desktop...).
   Disable it in Device Manager or make the real monitor the primary one.

Exclusive fullscreen: does not work. Windowed or borderless.


AMD MODULE (beta)
---------------------------------------------------------------------
The red button USE DLSS 5 ON AN AMD CARD uses the bin-amd\ folder.
It runs the DLSS 5 network on a Radeon through the "DLSS-NR-on-AMD"
mod (by danielblnc), which is third-party, closed source and NOT
included in the package. It is a BETA: it may not work on your card
and it runs slower (per the mod's author, about 31 fps at 1080p on
an RX 9070 XT and about 15 to 20 fps on an RX 9060 XT).

Requires: Windows 11, Adrenalin driver 26.1.1 or newer, a Radeon
RX 7000 or RX 9000, your own nvngx_dlssnr.dll and the mod's
installer. RX 9060 XT: needs mod v0.2.14 or newer (the author said
on 2026-09-05 that support lands in that version); today the public
release is still v0.2.11 and newer builds are only on the author's
Discord. Check the releases page before downloading.

Short version (the full one is in bin-amd\README-AMD.txt):
  1. Copy your nvngx_dlssnr.dll (the same one from bin\) to bin-amd\.
  2. Download dlssnr_on_amd_setup.exe from
       https://github.com/danielblnc/DLSS-NR-on-AMD/releases/latest
     copy it into bin-amd\ and double-click it. It writes
     version.dll, dlssnr_on_amd.ini and dlssnr_on_amd_weights.bin.
  3. Check in dlssnr_on_amd.ini that Inline=1 (it already writes so).
  4. Double-click bin-amd\preparar-mod-amd.cmd: it checks everything
     and tells you what's missing. Then just open with the AMD button.
Antivirus may flag the mod's installer (known issue). Trusting it is
your call; if you do, add bin-amd\ to the exclusions first.


MOVING OR COPYING THE FOLDER
---------------------------------------------------------------------
You can copy the whole folder anywhere: another place, drive or PC.
The only rule: DLSS5ForAll.exe and the bin\ folder stay TOGETHER.

Avoid paths with accented characters and the Program Files folder.

Your settings live in bin\ :
   dlss5forall.ini   chosen source, shortcuts, language
   ReShade.ini       ReShade and neural settings
   FafaDLSS5.ini     the preset with the techniques


RECORDING THE RESULT
---------------------------------------------------------------------
The recipe that works:
  1. In the app: WINDOW source and OVERLAY unchecked (separate window).
  2. In OBS: "Window Capture" pointed at DLSS5ForAll.exe, with the
     CAPTURE METHOD set to "Windows 10 (1903 and up)".
     With the "Automatic" method (BitBlt) the window comes out BLACK:
     that is a limit of the method, not of the app.
Don't use "Game Capture". NVIDIA App / ShadowPlay also works.
With the MONITOR source our window hides itself from recorders on
purpose (otherwise it would film itself): use the Window source or
two monitors. In OVERLAY mode the layer goes away when the game loses
focus, so the OBS preview looks empty — record in separate-window mode.


COMPARISON MODE (the before-and-after video)
---------------------------------------------------------------------
In More options, in the panel. The screen is split down the middle:
the LEFT half is the game's ORIGINAL image, the RIGHT half is the
same image WITH DLSS 5. They are two halves of the SAME frame — no
editing cut involved, and that is what convinces people watching.

A badge shows at the top of each half (ORIGINAL and DLSS 5 FOR ALL).
They are the images marca\comparar-original.png and
marca\comparar-dlss5.png inside the bin folder: swap those two files
if you want different art. With the files missing the mode still
works and a plain caption takes their place.

LEAVE "DLSS FRAME SKIPPING" OFF WHEN RECORDING. On the frame the
program draws (the skipped one) there is no "with DLSS" and "without
DLSS" — both halves are identical, and that is not a bug.

In bin\dlss5forall.ini, section [comparar]: ligado=1 turns it on
without the panel, divisor=50 moves the middle line (5 to 95, in
percent), rotulos=0 hides both badges and rotulo_altura=9 changes
their size (5 to 15, in percent of the screen height — raise it if
you are going to crop the video for phones).


FINE TUNING IN dlss5forall.ini (optional)
---------------------------------------------------------------------
Everything the panel offers is already saved for you. The keys below
are for changing the default behaviour; they all live in the [ultima]
section of bin\dlss5forall.ini and none of them has to exist.

fluidez=2           forces the projected frames (2 or 3 per incoming
                    frame). Without the key the program decides on its
                    own, from the number chosen in DLSS 5 SMOOTHNESS
                    ON SCREEN. The value 1 is the default and does not
                    turn the automatic mode off.
fps_teto_pelicula=  the number in DLSS 5 SMOOTHNESS ON SCREEN. Without
                    the key the default is 60; 0 means unlocked.
perfil_reabre=0     do not reopen the program by itself when you
                    change the network profile or the passes. The
                    choice is saved and takes effect on the next start.
atalho_reserva=0    turns off the fallback path for the keys (the one
                    that recognises the shortcut when the game holds
                    on to the Windows hotkeys).
recursos_experimentais=1
                    shows the experimental pieces that are not on by
                    default and come back in the next version: the
                    NEURAL NETWORK ENGINE switch, the NEURAL NETWORK
                    SIZE ruler and the AMD mode PER-FRAME WAIT BUDGET.
                    They stay hidden because the built-in engine was
                    only measured on a test scene, not in a game -
                    what is not settled does not ship turned on.
                    Nothing was removed from the program.
alterna_modo=       how the skipped frame is filled, in case the
                    default does not show up on this machine:
                    sintetizar (default), segurar, alfa or regiao.

THREE NEURAL CONTROLS THAT DO NOTHING IN THIS BUILD
The Neural Rendering add-on exposes NRGlobalTone (GLOBAL TONE),
NREnableUpscaling and NRTransferStrength. In this build they do not
change the image — the add-on itself says so about the global tone.
GLOBAL TONE is still in the profile window because it works again in
other versions of the add-on; the other two are not shown at all.

NOTES
---------------------------------------------------------------------
- Online games: anti-cheat may dislike the overlay. At your own risk.
- nvngx_dlssnr.dll and renodx-dlss.addon64 are a leaked NVIDIA /
  RenoDX build. Do not redistribute.
- Technical details, command line, latency and credits:
  bin\README.md  and  bin\CREDITOS.md


PRIVACY (what this program sends over the internet, and how to turn it off)
---------------------------------------------------------------------
The DLSS5ForAll.exe in this folder (the launcher, the group screen) makes up
to three short requests when it opens, all over https and only if ofertas.ini
has the url_base= key filled in (without it, nothing leaves your computer):
- whether a new version of the program exists;
- what the group screen's advertisement is (title, message, coupon);
- on the author's copy, the usage report (your copy does not have that key).

FOUR pieces of data go with those requests, and only these:
  v  = the program version (e.g. 1.1)
  m  = the button you chose last time (nvidia, amd or sem)
  l  = the program language (pt or en)
  id = a random number created on the first run, stored in dlss5forall.ini
       next to this file ([atualizacao] id_instalacao=). It is not derived
       from your hardware, name, e-mail or IP address.
Clicks on the group screen's buttons (NVIDIA, AMD, open without DLSS 5, show
me the deals, support the channel, coupon, update, closed without choosing)
are also recorded with those same four pieces of data and nothing else.

THE DEALS COLUMN (the left column of the screen): with internet, the screen
gets wider and shows, on the left, the group's current-deals page
(grupo.pcdofafa.com.br/app-ofertas), opened by WebView2 (the Edge engine that
ships with Windows; the WebView2Loader.dll next to this file is only the
bridge to it). That page
receives the language (l) and the same id above (only with contar=1), and
nothing else; clicks inside it ("see deal", "join the group") are recorded by
the page itself. Links open in your default browser. Without internet,
without WebView2 on the machine, or on a narrow monitor, the screen is the
usual single-column one. The column cannot be turned off from ofertas.ini
(the vitrine= and vitrine_url= keys are no longer read). The engine's data lives in cache\webview2\ (safe to
delete).

Why: to count how many people use the program, to know which buttons are
used, and to tell you when a new version is out. No game name, hardware,
usage time, files or personal data are sent. There are no cookies.

To turn it off: open ofertas.ini (next to this file) and change
    contar=1
to
    contar=0
With contar=0 none of the four pieces of data is sent and no click is
recorded. The new-version notice and the advertisement keep working. For no
request at all, leave url_base= empty.


=====================================================================
  DLSS5 For All - by PC do Fafa
=====================================================================
  YouTube channel ........ youtube.com/@pcdofafa
  Site / download ........ pcdofafa.com.br/dlss

  The app is free. The channel and the deals group are what keep it
  going - if it helped you, drop by.
