# MechAssault 2 PC port: open bugs from play-testing

One line per bug: where it happens, what goes wrong, and a screenshot if you
have one. Fixed bugs move to MODLOG.md.

## Open

- **Tank main cannon draws nothing** (user, 2026-10-19; the tank is in a
  later mission, not reachable by test runs). Found meanwhile: the renderer
  dropped every line and point the game drew; lines draw now (toolkit
  b5a4622 -- the tutorial text box's border was one), points are still
  dropped and logged. Next: fire the cannon in the bug-hunt build and press
  F8 right after; logsughunt_stderr.txt shows whether "[PB] points ... not
  drawn" lines come with it.
- **Water draws black** (night level by the water with a wrecked ship;
  screenshot analysis/bug_water_black.webp, 2026-10-19). The water surface is
  solid black, and a bright grainy white band (spray or mist?) lies across it
  at the horizon. Suspect the water's bump-environment-mapped stage (the NV2A
  texture-shader modes) or a reflection render target, possibly the same
  cause as the odd reflections below.
  **Fix in, needs your test** (toolkit bc8bf7f, 2026-10-19): the bump-map
  and dot-product texture stages, which used to read black, now sample. No
  test save reaches water, so it is unchecked there. Please look at the same
  water; if it is wrong in a new way, `set RECOMP_D3D11_DOT=0` before
  starting brings the old black back (compare), and logs\last_stderr.txt
  has a "(BUMPENVMAP...): ... matrix" line with the numbers the water used.
- **Speech may still be too quiet under combat** (user, 2026-10-19: "speech
  is better but still quieter than intended, or the combat sounds overpower
  the voices"). The radio speech now plays at unity through its FX send
  (toolkit 5825f42). The game has its own volume menu (MAMenuInGameVolume,
  pause menu); try the voice slider there before changing the mix.
- **Jump-jet exhaust draws as a black square** under the mech, the plume's
  tail (screenshot 2026-10-18). Suspect a texture the renderer can't decode,
  or a heat-haze effect sampling the frame it is drawing into. The log names
  only the first 4 volume textures, so this one isn't in it.
- **Lighting goes very dark in later parts of the first jump-jet level**, the
  roads nearly black (2 screenshots, 2026-10-18). Suspect lightmaps, vertex
  lights past some count, fog, or a combiner stage the shaders get wrong.
- **Odd reflections on objects:** shiny pink/purple highlights on the
  hangars, and mottled sky-like patches on the rooftops (same screenshots).
  Suspect environment/cube-map reflections: face order or orientation, or a
  reflection texture that is a render target.

## Later (features asked for, not started)

- **Mouse in menus** (user, 2026-10-19): a visible cursor that highlights a
  menu item on hover and selects it on click, in every menu. The menus are
  pad-driven (MAMenu* classes, .ggf layouts), so this needs each item's
  screen rectangle and the call that moves the focus; parked for more
  important features.
- **Y and Black button icons** still show the pad's picture: they did not
  appear in any test run, so their texture hashes are unknown. Run with
  `set RECOMP_TEX_HASHES=64` where one shows, and add its hash to k_icons in
  src/ma2_prompts.c.
