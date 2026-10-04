🇧🇷 [Versão em português](LEIA-ME.md)

# DLSS5 For All

**by PC do Fafa** — channel: <https://youtube.com/@pcdofafa> · site: <https://pcdofafa.com.br/dlss>

A minimal Direct3D 11 window that shows **any video source** — capture card, webcam,
**a game window** or **an entire monitor** — and exists for a single reason: to be the simplest and most
predictable target possible for the chain **ReShade → DLSS5-Feeder → LumeniteFX → renodx-dlss**.
It is the same trick EDMIRE used on the Switch 2 (Feeder inside OBS + projector), only without OBS in the middle:
one exe, one swapchain, one real depth buffer, one resolution.

DLSS 5 (Neural Rendering) receives **one 2D frame + motion vectors** and "photorealizes" what it sees.
The Feeder fabricates the rest of the contract (depth + motion vectors via optical flow).
Since nothing is injected into the game process, this works with **any** game that runs windowed or borderless
— including emulators, YouTube videos, old DirectX 9 games, whatever.

## Video sources

| Source | What it is | How to open |
|---|---|---|
| **Monitor** | Windows Graphics Capture of the entire monitor | selector (it is the default), or `--source monitor --monitor N` |
| **Capture card / camera** | Media Foundation (webcam, ezcap, DroidCam…) | selector, or `--device N --mode M` |
| **Window** | Windows Graphics Capture of the chosen window — **lighter** | selector, or `--source window --window <part of the title or of the exe>` |

**Monitor is the default, but Window makes the filter lighter** — and that is the reason it exists in the selector. The
network processes **area**: a game at 2560x1080 is 2.8 MP per frame against 5.0 MP of a 3440x1440 screen, almost half
the work. In monitor mode there is no way around that, because the capture is the whole screen even if the game occupies
only a piece of it. In exchange, Window asks you to pick the target from a list, and the layer disappears when it loses
focus.

And the screen sources (window and monitor) have **two operating modes**, chosen in the
*"OVERLAY the target"* checkbox of the selector (or `--overlay`):

- **Separate window** (box unchecked): the target appears inside the DLSS5 For All window, with the filter
  applied. Good for recording, comparing side by side and tweaking ReShade at will.
- **Overlaid** (box checked / `--overlay`): the window becomes a borderless popup **glued exactly on top
  of the target**, always on top and **transparent to the mouse** — whoever receives clicks and keyboard is still what
  is underneath. It looks as if DLSS 5 were running inside the game.
  - over a **window**: follows position and size, and disappears by itself when the game loses focus;
  - over a **monitor**: covers the entire screen, and since our window is excluded from the capture
    (`WDA_EXCLUDEFROMCAPTURE`), there is no infinite mirror — **everything on that screen goes through the filter**.
    If nothing changes on screen for several seconds the image may freeze (the layer ends up covering its own source);
    the app detects it, recreates the capture and, if it still does not come back, restores the original image by itself.

> **Two things make the overlaid mode usable, and both were discovered by measuring.**
>
> **1. The layer is `WS_EX_LAYERED`, and without it clicks do not pass through.** `WS_EX_TRANSPARENT` alone + answering
> `HTTRANSPARENT` in `WM_NCHITTEST` **is not enough**, and the documentation says why: `HTTRANSPARENT` sends the
> click to the windows below *"in the same thread"* — that is, only to windows of the app itself. For
> Explorer, the desktop or the game, the click dies at the layer. What crosses processes is the pair
> `LAYERED + TRANSPARENT`. Measured here, same session and same point, only that variable changing: without layered,
> **blocked**; with layered, **passed**. `DLSS5_SEM_LAYERED=1` goes back to the old behavior, in case layered
> fights with the flip model on some machine.
>
> **2. Layered also solves a second problem, and that is why the window does NOT need a thread of its own.**
> `WM_NCHITTEST` is **synchronous**: when Windows needs to ask "is this click yours?", it waits for the answer
> from the window's owner thread before delivering the click to anyone. With the layer covering the screen and the
> render on the same thread, every expensive DLSS `Present` held the whole system's mouse — measured: **1,864 ms**
> of wait, with the log marking `present travou 841 ms` at the same instant. The window was even moved to
> its own thread because of this (0.4 ms worst case), but with `WS_EX_LAYERED` the system **discards the layer in the
> hit-test without asking anything**, and the wait disappears at the root. The extra thread was undone because it charged dearly
> elsewhere: ReShade lives inside this process and `PokeReShade` (the F6) posts the key to the window counting
> on the `Present` of the same thread — with the window on another one, F6 started to lag. Nothing is intercepted or
> re-routed: the layer is simply not consulted, and the click follows the system's normal path.

In overlaid mode the app **does not appear in the taskbar or in Alt+Tab**: it stays in the **system tray**,
and the icon's menu carries the commands and the shortcuts.

### Shortcuts (only two)

| Key | What |
|---|---|
| **End** | opens and closes the panel — everything else (filter, neural, fps, HDR, ReShade, quit) is done there, with the mouse |
| **F6** | toggles **everything the panel configured** (neural + the layer, in overlaid mode) at once — the before/after — and returns exactly as it was; on-screen notice of the real state |
| **Alt+Esc** | quits the app (twice within 2 s, so it does not close by accident) |
| **F8** | switches the processing mode by hand: Native ↔ Universal (see "Processing modes" below) |

All keys are configurable — in the **Shortcuts…** button of the selector (before choosing the source) and in the
SHORTCUTS section of the panel, under "More options": panel, neural, filter, keyboard for ReShade and quit, each with the key
you want (with conflict detection and "restore defaults"). By default End, F6 and Alt+Esc have a key.

The **Shortcuts…** button of the selector also configures the key that **opens the ReShade overlay** (default **Home**).
That is the only one in the list that does not belong to the app: it goes to `[INPUT] KeyOverlay` of `ReShade.ini` — the app makes a
backup (`ReShade.ini.bak-keyoverlay-…`) before the first write and refuses a key already used by a shortcut
from here, in both directions. Chosen in the selector, it takes effect in this very launch: ReShade reads `ReShade.ini` when the
first swapchain comes up, and that happens after you close the selector.
On startup, a card with the keys stays on screen for 30 s (goes away when you press End; "do not show again" available,
and the panel has "How to use" to review it). If another program already holds the key,
the app takes the fallback and shows which one won on the initial card. **Esc never closes the app** — only the panel or the screen
that is open; quitting is via Alt+Esc, the panel button, the X (separate window mode) or the tray.

#### When the shortcut does not reach inside the game

`RegisterHotKey` is global and works with any window in front — **except** in two cases, and both have
a way out:

- **the game holds the Windows hotkeys** (some request `RIDEV_NOHOTKEYS` in Raw Input). There is no API to
  ask that from the outside; the app solves it by **watching the key through another path**: an invisible window
  of ours registered as a **Raw Input** receiver (`RIDEV_INPUTSINK`, on its own thread) sees the key without
  focus, waits 250 ms for the `WM_HOTKEY` and, if it does not arrive, fires the action anyway — with
  deduplication in both directions, so a key never becomes two actions. The log says when that happens
  (`o WM_HOTKEY NAO chegou em 250 ms … a janela na frente era …`). `[ultima] atalho_reserva=0` turns it off.
  **It was a `WH_KEYBOARD_LL` hook until 15 September 2026.** A low-level hook sits in the chain that all of the
  system's keyboard input passes through before reaching the game, which is exactly what anti-cheat heuristics
  read as a macro; Raw Input is the same API the game itself uses to read the keyboard, pointed at our window
  instead, and it puts nothing in the game's path (the game's `NOHOTKEYS` only turns off hotkey processing while
  it is in front, it does not stop another process from reading the keyboard as a device). The
  `LowLevelHooksTimeout` is gone too; the thread stays its own only for latency, so `WM_INPUT` does not wait on
  a stalled `Present`. When the app detects a known anti-cheat system in the captured exe's folder (window
  source), it turns **Anti-cheat mode** on for that source and a card says so on startup; this fallback path
  **stays on** (it only reads, outside the game's process) — End and F6 keep working through it, `RegisterHotKey`
  and the tray icon;
- **the game runs elevated and the app does not.** Then none of our keys gets there (UIPI), and there is no trick: the app detects it
  (`OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` + `TokenElevation`; the `ACCESS_DENIED` itself is already an answer)
  and shows a card telling you to open DLSS5 For All as administrator too.

There is also a path that **does not depend on any key**: the **tray icon**, whose menu opens the panel.

> **The floating button was removed on 11 September 2026.** There was a pill always above the layer, as a third door
> to the panel; the user asked for its removal — it was one more thing on top of the game. **No switch was left to turn it
> back on**: a hidden option in the ini for a drawing nobody sees anymore is not configuration, it is dead code with
> a switch on top. The keys `[ultima] botao_flutuante`, `botao_x` and `botao_y` are no longer read. The
> **keyboard** fallback path remains — it is what solves the "the game holds the hotkey" case, which was the
> original reason for the pill.

## On-screen panel (End)

The **End** key opens a panel above everything (outside the capture, so it does not go into the video or the filter).

The **first view** brings only what gets adjusted in the middle of a match; the rest lives in **More options**, which opens a
second column beside it. The explanations are no longer on screen: hovering the mouse over any control opens a
**tooltip** with the text, after 350 ms. The panel is never drawn below its base size — if it does not fit the
screen height, content is hidden, the font is not shrunk.

- **DLSS 5**: a single card, ENABLED/DISABLED (F6), which turns the layer **and** the neural on and off together. The state
  of the Neural Rendering appears in it as information — that is how you notice the chain did not come up.
  **The program always opens on, at 1 pass** (user's decision): turning it off with F6 holds **for the session**,
  and the next launch starts on. Startup undoes the `[ultima] neural_off_nosso` mark that the previous
  session may have left — without that the card would show a state that is no longer real
- **Image profile (neural network)**: a card with a summary of what is in effect
  (`Modelo B · Intensidade 100% · 2 passes`) and the **Adjust** button, which opens a window of its own
  (class `DLSS5ForAllPerfil`, in the same mold as the Filters window) with the six controls:
  MODEL (A/B/C), INTENSITY, STRUCTURE, SKIN AND FACES, LOCAL TONE and GLOBAL TONE
- **NETWORK PASSES** 1…10 — how many times the network redoes the **same** frame (`DirectNeuralRenderingPassCount`).
  Measured in this tree: ~4.2 ms of GPU per pass. Replaces the old "1x / 2x / 3x"
- **DLSS FRAME SKIPPING** Off / 1 in every 2 / 3 / 4 — the network processes one frame out of every N and the app
  **draws** the others, projecting the scene through the motion field. The synthesized motion is automatic
  (there is no switch) and falls back on its own to "hold" when the base or the field is missing. A single section: in overlaid
  mode it uses the intermittent layer with synthesis, in mirror mode it uses the simple skip — the two keys
  (`alterna_quadros`, `pulo_quadros`) remain in the ini and turning one on turns the other off
- **DLSS 5 SMOOTHNESS ON SCREEN** `[−] 60 [+]` and **Unlock** — how many frames per second the film
  presents; it does not change the game's fps. **Below** what the game delivers the number is a cap and is **exact**.
  **Above**, frames projected by motion come in — and that is **experimental**: delivery is **partial**,
  according to the card's headroom. Measured here with the game at 60: asking for 90 the film went to **70**; asking for 120,
  to **73** (the screen's useful cap was 94). It really goes up, but does not reach the requested number.
  **Unlock** does not turn projection off: with no number chosen, the target becomes the screen's useful maximum
  (95% of the refresh). The `[−]` still works in the unlocked state and returns to the last chosen number.
  Saves in `[ultima] fps_teto_pelicula`; default 60; `0` = unlocked. A status line below shows
  `jogo N/s · película N/s · N projetados por segundo` — and the **film counts everything that goes to the screen**:
  processed, synthesized by frame skipping and projected. (Until 11 September 2026 the synthesized frames were left out and,
  with skipping on, the line showed half of what the person was seeing.)

  **Composes with DLSS FRAME SKIPPING**, and this was measured: with skipping 1 in 2 and Smoothness at 120, the network
  processed ~20/s and the film came out at **74–80/s**. The external proof (count of distinct frames on screen,
  outside the app) gave **≥57/s** — it is a floor, because the sampler saturates at ~88 samples/s; even so it is
  far above the ~20 processed, which is what the composition needed to show.
- **DLSS resolution** Auto/100%/77%/67%/58% — changes the **network's cost**. `Auto` picks by the source area
  (≤2.4 MP: 100 · ≤4.0: 77 · ≤6.0: 67 · above: 58). Takes effect live: the feed holds the rebuild for 60 frames and
  rebuilds (measured: ~2 s), with the neural turned off during the rebuild and turned back on 4 s later
- **More options**: Filters (sharpness, anti-aliasing, details, color, brightness & contrast, exposure & gamma, vibrance, curves, sepia, black & white, night mode, vignette and film grain — each with a switch and a slider, applied instantly, in their own window), V-Sync, HDR, Show FPS on screen,
  **Comparison mode**, game FPS, language, shortcuts, the **Deals group — PC do Fafa**, the channel,
  Open ReShade, **Switch source or card** and Quit
- **Switch source or card** (next to Quit): goes back to the source selection screen without closing the app by hand.
  The path is a **relaunch** — the NGX chain does not survive a source switch with the device alive —, with a
  card in front while it switches. The new copy carries the current source, card and language **already selected**
  (`--pre-fonte`, `--pre-janela`/`--pre-monitor`/`--pre-dispositivo`/`--pre-modo`, `--placa`); without those
  arguments the selector would fall back to what is in the ini, which is the last time someone opened the app through the selector.
  The window is matched by **title**: if the game changes its title on its own, the selector opens on the first item of the list.
  The app also returns to that screen by itself when the captured window is closed

### The profile only takes effect when the add-on loads

MODEL, INTENSITY, STRUCTURE, SKIN, TONE and PASSES are written to three places (the `[ultima]` of our ini, the
live ReShade config through the bridge and the `ReShade.ini`), and even so they **do not change the network while the app runs**: the
RenoDX add-on reads the profile once, when it loads, and does not re-read it. Measured: `DirectNeuralRenderingPassCount=10`
chosen in the panel left the feed at 0.56–0.64 ms/frame and 61 fps; the same 10 read **at launch** gave
3.04–6.54 ms/frame and 24.6 fps.

That is why the app **relaunches itself** after a profile adjustment: 2.5 s of wait (each new touch restarts the
count, so touching six controls relaunches only once), a card on screen and `ReabrirApp` preserving target (by
HWND), source, card, skip and intermittent. `[ultima] perfil_reabre=0` turns the relaunch off — then the choice stays
saved and takes effect on the next launch done by hand.

### Network engine: RenoDX (default) or own (experimental)

> **Hidden in this release — returns in the next one.** The engine switch, the
> **NEURAL NETWORK SIZE** slider and the **WAIT BUDGET** of the AMD module only appear with
> `[ultima] recursos_experimentais=1`. The reason is neither the interface nor safety: the feeder that
> ships with this version **already includes** the guard that prevents the two neural engines from running
> together (the one that avoids the `0xC0000005` in the SuperSampling `evaluate`). What is missing is **measurement**:
> the own engine was only compared in a synthetic scene, and what is not settled does not ship
> on by default. With the key, everything below reappears; nothing was removed from the program. The usual
> **DLSS resolution** chips remain where they were — they are something else (the size at which
> the whole chain works).

Two implementations of the **same network** coexist in the folder, and switching is configuration only — nothing is renamed
or removed. In **More options → NEURAL NETWORK ENGINE**:

| | RenoDX (default) | Own (experimental) |
|---|---|---|
| `ReShade.ini` `[RENODX-DLSS] DirectNeuralRenderingHookPoint` | `2` | `0` |
| `dlss5-feed.cfg` `nr_spike` | `0` | `1` |
| who evaluates the network | `renodx-dlss.addon64` | `dlss5-feed.addon64` |
| profile and passes | take effect **on the next launch** | take effect **immediately** (the cfg is re-read every 60 frames) |
| GPU cost | — | **tie**; it is not faster |
| image | blurs a lot with many passes | blurs much less (laplacian 834 versus 168 with 4 passes) |

**Switching relaunches the app once**, and that is not a convenience choice: the RenoDX add-on reads the `HookPoint`
a single time, when it loads. **The two are never on together** — that crashes the SuperSampling `evaluate`
with `0xC0000005` and the scene goes on without any filter; the panel writes both keys in the same
action, startup reapplies them, and the feeder refuses to bring up the own engine if the other one's key is on.

**NEURAL NETWORK SIZE** (own engine only): `Light 65%` · `100%` · `125%` · `150%` · `200%`. It is not
the "DLSS resolution" of the first view — that one is the size at which the **whole chain** works; this is
the size at which **only the network** runs. It is **a single control** because the keys are a pair: `nr_escala` only acts
with `nr_reduzida=1`, so "light network" and "network supersampling" are the same pair at different steps —
two separate controls would cancel each other out. `Light 65%` yields +21% to +34% fps with 4 to 10 passes;
above 100% it is expensive (200% ≈ 2.4× the cost) **and the network acts less** on the image.

The panel reads `dlss5-feed.status` (key=value, next to the log) and shows `motor · rede · passes`. That is where
the **safe passthrough** card comes from: without that file the feeder's warning died in `ReShade.log` and the
person saw the unfiltered image without knowing why. `estado=ok` with `motivo` filled in means "the chain
is filtering, but the requested engine did not come up" — and the card says that, not "stopped".

### In the AMD module the profile goes to the mod, and half of it does not exist

In the AMD package the Neural Rendering is done by the third-party mod **DLSS-NR-on-AMD** — the RenoDX
add-on does not even enter the folder. Until 1.2 the panel wrote the profile to the `[RenoDX.DLSS5]` block of
`ReShade.ini` **even there**, where nobody read it: the controls seemed to work and never reached the
network. Now they go to `[DlssNrOnAmd]` of `dlssnr_on_amd.ini`, which is what the mod reads.

| panel control | mod key |
|---|---|
| LOCAL TONE | `LocalTone` |
| STRUCTURE | `LocalStructure` |
| SKIN AND FACES | `SkinStructure` |
| automatic mask | `UseAutoMask` |
| MODEL, INTENSITY, PASSES | **do not exist in the mod** — appear greyed out, with the reason |
| GLOBAL TONE | exists (`ToneChannels`) and **stays greyed out anyway**: the mod's own help says this build of it keeps those channels off and that raising the value *"mostly darkens the frame"* |

The keys were checked **in the mod's binary** (the installer and `version.dll` v0.2.18), not
assumed. Our five steps (0/25/50/75/100%) enter the mod's scale by rounding into
0..2; the factory `-1` of `SkinStructure` means **auto** ("follows Structure"), and the upper
limit **is not confirmed** — no string in the binary names it. That is why the log records the pair
"our value → mod value" on every write. The write replaces one key at a time and preserves the
rest of the file (including the CRLF), because the mod also writes to it through its own overlay.

Changing the profile **relaunches the app**, for two reasons: the mod only has documented proof of live
re-reading for `Inline` and `Interop`; and its re-read **redoes the staging**, that is, even working
live it would cause a hiccup in the image. And what actually gains fps in AMD mode is none of this — it is the
**DLSS resolution**, and the panel says so in the label of that section (13.5 ms per frame of network alone
at 634x669 on an RX 9060 XT — **measured with video on screen, not in a game**, in a session on another PC).

**WAIT BUDGET PER FRAME** (`[−] 25 ms [+]`, step 5, from 5 to 200) is the mod's `InlineWaitMs`:
how long a frame waits for the network before the mod shows the previous frame's retouch instead of
holding the image. **Lower = smoother, with residue from the previous frame when the network lags;
higher = more faithful, less fps.** 25 ms is the value proven on an RX 9060 XT; the mod ships at 200.
It only appears if the installed mod knows the key (v0.2.18 onwards) — and that check is not by
version number: the app looks for the key in `dlssnr_on_amd.ini` and, if it does not find it, for the name inside
`version.dll`. The mod does not stamp `VERSIONINFO`, so comparing versions was not really
possible.

### Comparison mode (to record the before and after)

In **More options → Comparison mode (half without DLSS 5)**. The screen is split in the middle: on the **left** the game's
**original** image, on the **right** the same image **with DLSS 5 applied** — the two halves of the **same
frame**, which is what convinces. In the middle there is a 2 px white line.

This is done by the bridge (`dlss5-bridge.addon64`), in two hooks: at `reshade_begin_effects` the back buffer
is still the native one and a full copy is kept; at `reshade_finish_effects` the left half of that copy is
put back over the result. Cost measured by design: two GPU copies per frame (~8 MB + ~4 MB at
1080p), and only with the mode on.

At the top of each half goes a **badge**: the images `marca\comparar-original.png` and `marca\comparar-dlss5.png`,
next to the executable. They are loaded from file (the art can be swapped), resized to **9% of the screen
height** (`[comparar] rotulo_altura`, from 5 to 15) with a 1.5% margin, and drawn **without stretching**. If the
PNGs are missing, a fallback sign written on the spot comes in — a dark card with the word `ORIGINAL` / `DLSS 5`. The
badges are copied, not blended: there is no transparency in effect, and on an HDR back buffer (10 or 16 bits) they
do not appear (`ReShade.log` says so).

**The two halves are from the same instant, and that was measured, not assumed.** A battery of 45 shots (still,
motion at 8 px/frame and motion with frame skipping, against `[comparar] atraso` 0/1/2), measuring the step
at the seam divided by the image's typical step: without compensation the ratio stays at **0.87** with the scene moving
and **0.40** with skipping (1.00 = seam as smooth as the rest); compensating one frame takes it to 1.78 and 2.97, that
is, **worse**. The jump that sometimes shows up in a shot is capture tearing (`PrintWindow` catching the window in the
middle of a frame swap), not chain lag. `[comparar] atraso` remains available, with default `0`.

**On the synthesized frame of DLSS FRAME SKIPPING the two halves are identical**, and there is no fix for that here:
on that frame the whole chain is suppressed — there is no "with" and "without", there is a frame drawn by us.
To record a comparison, leave frame skipping **Off**.

### Processing modes: Auto, Native, Universal — and Anti-cheat mode

DLSS 5 For All is one thing; the **mode** is only how the image gets to it. Every function of the panel works the
same in both.

- **Universal** — captures the image from the screen and applies DLSS 5 on top of it, like a filter, in our own layer.
  Compatible with virtually any application displayed on screen; does not interact with the game. Recommended for
  games with anti-cheat.
- **Native** — integrates with the game: a 64-bit DLL of ours is loaded into the running game, the frame is processed
  on the GPU before it is presented, and the game itself shows the result (our layer disappears). Lower performance
  cost and lower latency. Available for 64-bit games on DirectX 11 and 12, without anti-cheat. Nothing is written to
  the game's folder and the game does not restart; when the mode is turned off the DLL unloads itself.
- **Auto** (default) — uses Native when available; otherwise, Universal. Capture comes up first (the image appears),
  and about 2 s after the first frame the app tries Native underneath. If it works, the layer disappears; if not,
  capture simply continues and the reason goes to the log (`modo: Nativo indisponivel nesta fonte (...)`).

The choice lives in the **Mode** card of the panel (End), with the three positions; the badge next to it says which
transport is in use right now. The Native position is dimmed when it is not available for this source (32-bit game,
anti-cheat detected, the integration failed); the reason shows as a tooltip. **F8** switches Native ↔ Universal by
hand at any time, without changing the saved choice.

**Anti-cheat mode** is a button on the source selection screen (under OPTIONS). Recommended for online games: it
keeps only screen capture, with no interaction with the game, like a gameplay recorder — Native is off, the fps
limiter piece is not installed in the game's folder (one already there is removed), and the mouse pointer mapping
(F9) is not entered. The keyboard fallback path (Raw Input, read-only) stays. It increases the likelihood of not
being flagged by anti-cheat systems, but does not guarantee it. When the app finds a known anti-cheat in the
captured exe's folder, it turns this mode on by itself for that source: the button shows it, without saving it as
your choice, and the log says `anti-cheat: modo ligado sozinho para esta fonte`.

### New keys in `dlss5forall.ini`

| key | section | default | what it does |
|---|---|---|---|
| `fps_teto_pelicula` | `[ultima]` | 60 (key absent) | the number of DLSS 5 SMOOTHNESS ON SCREEN; `0` = unlocked |
| `fluidez` | `[ultima]` | absent = automatic | forces the projected frames (2 or 3); only counts if greater than 1 |
| `perfil_reabre` | `[ultima]` | on | `0` does not relaunch the app when changing the profile/passes |
| `atalho_reserva` | `[ultima]` | on | `0` turns off the keyboard fallback path (Raw Input) |
| `alterna_modo` | `[ultima]` | `sintetizar` | how the skipped frame is filled: `sintetizar`, `segurar`, `alfa`, `regiao` |
| `ligado` | `[comparar]` | `0` | the comparison mode; it is what the panel chip writes |
| `divisor` | `[comparar]` | `50` | where the line sits, in percent of the width (accepts 5 to 95) |
| `rotulos` | `[comparar]` | `1` | `0` hides the ORIGINAL / DLSS 5 FOR ALL badges |
| `rotulo_altura` | `[comparar]` | `9` | height of the badges in % of the screen height (5 to 15) |
| `atraso` | `[comparar]` | `0` | how many frames the left side goes back; measured, 0 is the aligned one |
| `motor` | `[ultima]` | `renodx` | `proprio` uses the feeder's network engine (experimental) |
| `recursos_experimentais` | `[ultima]` | absent | `1` shows the engine switch, the network size slider and the AMD mod's wait budget (experimental, returns in the next version) |
| `LocalTone`, `LocalStructure`, `SkinStructure`, `UseAutoMask` | `[DlssNrOnAmd]` of `dlssnr_on_amd.ini` | from the mod | **AMD module**: it is where LOCAL TONE, STRUCTURE, SKIN AND FACES and the mask go |
| `nr_escala` | `[ultima]` | `100` | the lit step of NETWORK SIZE (65, 100, 125, 150, 200) |
| `modo_processamento` | `[ultima]` | `auto` | the processing mode: `auto`, `nativo`, `universal` (the Mode card writes it; F8 does not) |
| `anticheat` | `[ultima]` | `0` | `1` = Anti-cheat mode (the button on the source selection screen writes it) |
| `ponte_modo` | `[ultima]` | `pipelined` | Native mode only: `sincrono` waits for the processed frame in the same Present (less latency, more cost) |

## Language

The whole interface exists in **Portuguese and English**, and the app decides on its own at the first run by the Windows
language (`GetUserDefaultUILanguage`): Windows in Portuguese opens in Portuguese, any other opens in
English. From the moment the person chooses, their choice rules — it is stored in `[idioma] lingua=pt|en`
of `dlss5forall.ini`.

It is switched in two places, and in both it **takes effect immediately**, without restarting: in the first line of the **source
selector** (the first screen, next to *Source:*), in a `Português | English` pair that is always in view — with no
menu to open and no intermediate click —, and in the **IDIOMA / LANGUAGE** section of the panel. In the selector the window
rebuilds itself entirely in the new language; in the panel the card, the title bar and the tray menu follow at the
same moment.

What **does not** change language, on purpose: the messages of `dlss5forall.log` (they are a diagnostic
tool and there is documentation that depends on the exact text), the `.ini` keys and the ReShade technique, shader
and file names. The only exception in the log is the `FATAL:` line, which is a copy of what appeared in the error
box. The texts of the invitation (`ofertas.ini`) have the usual keys for Portuguese and the same ones with the
`_en` suffix for English; without the key for the language in use, the built-in text applies, which already comes translated.

**Color (LUT) and sharpening left the panel.** They were chips that wrote to the shaders `DLSS5_Grade.fx` and
`DLSS5_Sharpen.fx` through the `dlss5-bridge.addon64` add-on. The shaders, the LUT texture and the
`preset-02..05-*.ini` remain in the package: whoever wants to tweak them turns the technique on in the ReShade overlay
(the F7 key) and adjusts it right there.

### What actually runs in the chain

The `FafaDLSS5.ini` preset loads **three techniques**, and they are not interchangeable:

| Technique | Role | Can it be removed? |
|---|---|---|
| `Lumenite_Kernel` | fabricates the *depth* | **no** — without it the neural does not come up |
| `DLSS5_Feed` | fabricates the *motion vectors* and delivers the pair to DLSS | **no** — it is the Neural Rendering contract |
| `DLSS5_Sharpen` | post-sharpening (`Sharpness=0.35`) | **yes** — it is taste, not a requirement |

#### The neural add-on's controls: which ones count and which do not

The `DLSS 5 Neural Rendering` panel (Home key, in ReShade) shows more controls than this
build actually uses. Measured on 6 September 2026 in this chain:

| control | effect here |
|---|---|
| `NRStyle` | **counts, and it is what changes the image the most.** `0` (Default) is the app's default; `1` (Natural) compresses the highlights by 10.5% and, since the periphery of the scene is usually brighter than the middle, that reads as a **dark vignette** around the image. If you saw that vignette, this was it. |
| `NRIntensity`, `NRLocalStructure`, `NRSkinStructure`, `NRLocalTone` | count |
| `NRGlobalTone` | **no effect in this build** |
| `NREnableUpscaling` | **no effect in this build** — here the network works at full resolution |
| `NRTransferStrength` | **no effect in this build** |

The app writes `NRStyle` and `NRLocalTone` to `ReShade.ini` **every time it opens**, from
`[ultima] nr_estilo` and `[ultima] nr_tom_local` of `dlss5forall.ini`. This exists because ReShade's
*AutoSavePreset* writes to `ReShade.ini` whatever is in memory when it closes, and
that is how an `NRStyle=1` showed up in a package without anyone having chosen it — the image changed
style between one session and the next without explanation. To use another style, change it in `dlss5forall.ini`:
the value there is respected on every launch.

The Neural Rendering itself is not a technique of the preset: what runs it is the `renodx-dlss.addon64` add-on.
It is also **the expensive part** — that is why the panel has a button just for it, separate from the filter:
the fps difference when turning it off is the measure of what it costs on this machine. The cost of the rest is in
the `dlss5-feed.log` of each session, in this form: `600 frames: feed CPU 3.36 ms/frame … feed is 6% of the
frame`.

The **Neural Rendering** is the first item of the panel and the reason the app exists: the real state (active / starting /
off) appears highlighted, and the app warns loudly if it does not come up.

**Got heavy?** The filter costs GPU (the Neural Rendering is the expensive part). `F6` turns it off instantly (and turns everything back on the way it was) and the game
keeps running normally underneath — it never stopped; what you saw was our delivery. Turning **DLSS FRAME SKIPPING** on at "1 in every 2" cuts the network's work in half, and lowering the number of
**DLSS 5 SMOOTHNESS ON SCREEN** gives GPU back to the game. The app warns on its own, once, when it notices it is presenting far
below what it captures.

**HDR**: when the target's monitor is in HDR, the app captures in float16 and converts to SDR in the shader itself,
**automatically** — it works with the game already open, without touching the display. Real HDR content (game with HDR
on) no longer comes out blown out to white. The *Turn off the monitor's HDR while capturing (plan B)* checkbox is
optional: it turns it off and restores it on exit (even if the app is killed), but an already-open game usually reads HDR only at
startup. On the capture card the checkbox confirms the PQ/BT.2020 conversion. Measurements in `docs/hdr.md`.

**Exclusive fullscreen does not work** — Windows does not let the capture reach an exclusive swapchain, and
our layer does not appear over it. Game in **windowed** or **borderless** (most new games only have
borderless). Applies to both sources, window and monitor.

**If the game changes size** (window resized, resolution switch): the filter continues — the backbuffer stays at the
size it was born with (touching it with the NGX chain alive brings the Feeder down), the new image is fitted into it, the capture
is recreated if Windows terminates it, and the panel offers "Relaunch the app at the new resolution".

**Internal resolution** — it was built and measured (`docs/supersampling.md`): the neural does **not** improve running on a
larger backbuffer; only text/HUD gains a little, at the cost of game fps. Removed from the app by the author's decision.

**Presets** (switch in the ReShade overlay): `preset-01` core with `IMAGE_SPACE=1` · `02` neutral grade ·
`03` cinema grade · `04` restrained brightness · `05` sharpening. All leave `FafaDLSS5.ini` intact. From `02` to `05`
they are material for the ReShade overlay — since color and sharpening left the panel, nothing in the app selects them.

**Group screen**: opens the moment the app is opened (before the selector), on every run, with QR and button. It does not
close on its own — the person closes it with `Not now`, the X or Esc — no countdown, like the WinRAR notice. All configurable in
`ofertas.ini` (link, texts, `mostrar=sempre|primeira_vez|0`).

## Requirements

- Windows 10 1903+ / 11 64-bit (window capture uses Windows Graphics Capture; camera needs the Media Feature Pack on Windows N)
- GeForce RTX with a recent driver (the 4080 runs it; at 1080p expect to drop to ~half the FPS with NR on)
- Visual C++ Runtime 2015-2022 x64 (the script installs it if missing)

## Installation (once)

1. Unpack everything into a folder **without accents and outside Program Files** (e.g.: `C:\DLSS5ForAll\`).
2. Right-click `setup-dlss5.ps1` → **Run with PowerShell**
   (or `powershell -ExecutionPolicy Bypass -File .\setup-dlss5.ps1`).
   It assembles the whole chain next to the exe:
   - ReShade (build with add-ons) installed on `DLSS5ForAll.exe` → `dxgi.dll`
   - `ReShade.fxh`, `ReShadeUI.fxh`, `DrawText.fxh`
   - `dlss5-feed.addon64` + `DLSS5_Feed.fx` (legitimate repo: `jlrouzies-fr/DLSS5-Feeder`)
   - LumeniteFX (`lumenite_*.fx`, `include\`, blue noise)
   - `nvngx_dlss.dll` copied from the NVIDIA driver or from a game
   - `ReShade.ini` + preset `FafaDLSS5.ini` already with `DLSS5_MV_PROVIDER=3` and the techniques in the right order
3. **You** place `renodx-dlss.addon64` + `nvngx_dlssnr.dll` next to the exe (RankFTW's RHI installer or the #DLSS5 channel of the RenoDX Discord).
   The script stops and waits for them. They are NVIDIA's leaked code — the script does not download that for you.
   **If you open the app without them**, the selector shows a banner saying what is missing — and the check includes the
   `nvngx_dlss.dll` from step 2, which the script copies but may not find: without it the NGX session does not open, the
   feeder falls into safe passthrough and the screen stays identical to the original, with no error at all. There is a button that opens the
   RHI releases and another that opens the folder where the files go. Installed with the selector open? Click back
   on the window and the banner disappears by itself. The app still opens and shows the image without them — what does not
   happen is the DLSS 5 enhancement, and that was exactly the confusion: the image comes out identical to the original and it looks
   like the program does not work.

## Usage

1. Open `DLSS5ForAll.exe`. In the selector choose the **source** (camera / window / monitor), the target and the options. **START**.
   The last choice is saved in `dlss5forall.ini`.
2. In the window: **Home** opens ReShade (in overlaid mode, first open the panel with **End** and use the mouse/keyboard button, or `Ctrl+Alt+I`).
   - *Home* tab: `LUMENITE: Kernel 2.0` on **above** `DLSS 5 Feed` on (the preset already does that).
   - *Add-ons* tab → `DLSS 5 Feed`: it has to show `feature ready … DLAA` and `frame N delivered`.
   - Neural add-on panel (`DLSS 5 Neural Rendering`): turns NR on. **F6** toggles on/off — it is the before/after.
3. App keys (normal window mode): **F11** borderless fullscreen · Alt+Enter also toggles fullscreen · quit: **Alt+Esc** twice, the window's X, tray or panel (Esc never closes the app).

Command line:

```
DLSS5ForAll.exe --device 0 --mode 3 --fullscreen        # câmera / placa
DLSS5ForAll.exe --source window --window cyberpunk      # espelho de uma janela
DLSS5ForAll.exe --window cyberpunk --overlay            # colado por cima do jogo
DLSS5ForAll.exe --source monitor --monitor 1            # monitor numa janela
DLSS5ForAll.exe --source monitor --monitor 1 --overlay  # filtro por cima da tela inteira
DLSS5ForAll.exe --list-windows                          # lista janelas e monitores no log
DLSS5ForAll.exe --list                                  # lista câmeras e modos no log
```

Extra flags: `--full-window` (does not crop the title bar), `--cursor` (draws the mouse),
`--no-clickthrough` (overlay receives the mouse), `--hide-self` / `--no-hide-self`
(removes our window from the capture — on by default for the monitor source), `--capture-border` (Windows' yellow border).

## Frame generation with Lossless Scaling

DLSS 5 here does Neural Rendering, it does **not** generate frames. What can double the fps is Lossless Scaling (LS),
running **on top of our window** — it captures the already-filtered `DLSS5ForAll.exe` and interpolates on top of that.
It needs no change in the app; it needs the right configuration.

Chain order:

```
jogo (janela / borderless)
  -> nossa captura WGC da janela do jogo
  -> ReShade: Lumenite Kernel -> DLSS5_Feed -> RenoDX Neural Rendering
  -> Present do DLSS5ForAll.exe
  -> Lossless Scaling captura a NOSSA janela e interpola
```

The two captures do not fight: we grab the game's window, LS grabs ours. There is no mirror.

**How to use**

1. Open DLSS5 For All with the **Window** source, pointing at the game, and **OVERLAY unchecked** (separate window mode).
   Overlay and the monitor source **do not work** here — see the pitfalls below.
2. Keep **our** window in focus and press LS's *Scale* shortcut. Its target is `DLSS5ForAll.exe`;
   if you point it at the game, it interpolates the image **without** the filter.
3. Turn off other overlays (GeForce Experience, Discord, etc.) — anything on top of LS's window breaks the FG.
4. Do not use an external fps limiter. The limit, if any, is in the game.

**Pitfalls (both are on our side)**

| Situation | Why it breaks |
|---|---|
| **Monitor** source (any mode) | our window gets `WDA_EXCLUDEFROMCAPTURE` and LS sees a black screen |
| **Overlaid** mode | our layer reasserts the top every 1 s and fights with LS's output window |
| Pointing LS at the **game** | it interpolates the raw frame, without going through the DLSS 5 chain |

**The price:** LSFG at 2x costs about **1.5 to 2.5 frames** more, added to the 2-3 frames our chain
already has. At 60 fps of input, something between +25 and +42 ms. Fine for slow games and for video; not for competitive play.

LS is proprietary software you bought on Steam — nothing of it is redistributed here, only the usage recipe.

## Recording the result

ReShade applies everything **at the Present** of our window, so whoever records has to capture the already-presented window:

- **OBS → Window Capture** (Windows 10 / WGC method) pointing at `DLSS5ForAll.exe`. Do not use *Game Capture*.
- **NVIDIA App / ShadowPlay** also works.
- If the source is **monitor** with "hide this window from the capture" on, OBS **does not see** our window —
  use two monitors (game on one, DLSS5 For All on the other) or capture by window.
- For the **before and after** video, turn on **Comparison mode** (More options): the two halves come out in the same
  frame, already with the badges, and nothing needs to be cut in editing. Leave DLSS FRAME SKIPPING off.

Latency: measured on this machine, from the captured frame to the pixel on screen, **18-21 ms with the GPU free** (it was ~121 ms
before the waitable swapchain with `SetMaximumFrameLatency(1)`). The Neural Rendering adds +3 to 4.5 ms at 1080p and
~10 ms at 3440x1440 — and its cost is irreducible without losing image. No knob (V-Sync, fps cap, sliders,
working resolution, topmost, fullscreen) measures better than the current default; measurements in `docs/latencia-nr.md`. You can play slow stuff and watch video; for
competitive play, no. Measurements and methodology in `docs/latencia.md`.

**If the capture is stuck at 50 fps:** an installed virtual monitor (Meta Virtual Monitor, Parsec Virtual
Display, Virtual Desktop) makes WGC deliver at a fixed 20 ms pace, regardless of the game. It costs ~10 ms of
latency and half the smoothness. Disable the virtual adapters in Device Manager (or at least
set the real monitor as primary) and check the `LAT ritmo do WGC` line in `dlss5forall.log`.

## Honest limitations

- **Exclusive fullscreen does not work.** Put the game in windowed or borderless — that is the mode WGC and the overlay see.
- Nothing is injected into the game process (no hook, no DLL): the whole chain runs inside `DLSS5ForAll.exe`.
  Even so, aggressive anti-cheats may not like a topmost overlay — in online games, use at your own risk.
- Motion vectors are **estimated by optical flow**, they do not come from the engine. Fast motion and HUD may ghost.
- Backbuffer resolution = crop resolution. 1080p is the sweet spot on the 4080; 4K quadruples the load.

## Common problems

| Symptom | Probable cause | What to do |
|---|---|---|
| ReShade overlay does not appear with Home | the key was changed, or `dxgi.dll` did not load | Check the key in **Shortcuts…** (the "Open ReShade" line), or `[INPUT] KeyOverlay` in `ReShade.ini`. If it is right: rename `dxgi.dll` → `d3d11.dll` and check `ReShade.log`. |
| In overlaid mode the keyboard does nothing | it is click-through on purpose | panel (**End**) → mouse/keyboard button, or `Ctrl+Alt+I`; the same again to go back to playing |
| The overlay disappears when I click outside | normal behavior | it only appears with the game in the foreground |
| The overlay stays behind the game | topmost game fighting | the app reasserts the top every 1 s; if it persists, run the game in a normal window |
| Sharp image when still but blurs when moving | motion vectors not arriving | Overlay → Feeder's *Motion vectors* section. Kernel **above** Feed and `DLSS5_MV_PROVIDER=3`. |
| The image comes out identical to the original, "does nothing" | one of `renodx-dlss.addon64`, `nvngx_dlssnr.dll` or `nvngx_dlss.dll` is missing | The selector warns in a banner **saying which** and leads to the RHI releases. Without `nvngx_dlss.dll` the NGX session does not even open and the feeder falls into safe passthrough — the symptom is exactly "identical to the original", with no error. |
| DLSS 5 panel in STANDBY | add-on missed the first create | The Feeder recreates it on its own after ~180 frames. Wait. |
| "0.0 fps (alvo parado)" | the captured window did not redraw anything | normal on a static window; in a game the number goes up |
| Fatal: "dispositivo em uso" / no mode (camera) | another app holding the capture | Close OBS/Discord/browser/Camera app, click *Refresh list*. |
| Nothing opens, no window | look at `dlss5forall.log` | It says where it stopped (MFStartup, D3D11, shaders, WGC…). |

## About the files

- `DLSS5ForAll.exe` — the app (pure C++, Win32 + Media Foundation + D3D11 + WinRT/WGC, no dependencies beyond Windows).
- `main.cpp` — window, renderer, selector, camera capture. `screen_capture.h` — window/monitor capture module.
- `idioma.h` — the two interface languages: `T(L"português", L"english")` at the point of use, detection and persistence.
- `build-mingw.sh` — rebuilds with msys2's MinGW-w64 (`./build-mingw.sh`).
- `setup-dlss5.ps1` — assembles the chain.
- `dlss5forall.log` / `dlss5-feed.log` / `ReShade.log` — the three logs that matter when something goes wrong.

**What needs to go in the zip** (besides what `setup-dlss5.ps1` downloads): `DLSS5ForAll.exe`, `dlss5-bridge.addon64` (the panel's
bridge — it is how F6 toggles the neural without the keyboard), `reshade-shaders\Shaders\DLSS5_Grade.fx` and `DLSS5_Sharpen.fx`
(optional, for whoever wants grading through the ReShade overlay),
`reshade-shaders\Textures\dlss5_grade_luts.png`, `marca\comparar-original.png` and `marca\comparar-dlss5.png`
(the comparison mode badges — without them the mode works, but with the fallback sign),
the `preset-0*.ini`, `ofertas.ini`, `README.md` and `setup-dlss5.ps1`.
The script generates `ReShade.ini` and `FafaDLSS5.ini`; `dlss5forall.ini` is born on the first run. Do not distribute `nvngx_dlssnr.dll`.

From 2.0 on, the license in the `LICENSE` file applies (redistributing the original with credit is allowed; modified or rebranded versions are not); versions up to v2026.09.04 remain MIT. The app contains no NVIDIA code. What it hosts (ReShade, Feeder, LumeniteFX, RenoDX, the leaked DLL)
each has its own license and its own risk — DLSS 5 in this state is an internal build that fell out of NBA 2K27, and Take-Two has already
taken things down for copyright. Make the video, make it clear it is an experiment with a leaked build, and do not redistribute `nvngx_dlssnr.dll`.

---

**DLSS5 For All — by PC do Fafa.**
Channel: <https://youtube.com/@pcdofafa> · Download and news: <https://pcdofafa.com.br/dlss>
