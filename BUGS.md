# MechAssault 2 PC port: open bugs from play-testing

One line per bug: where it happens, what goes wrong, and a screenshot if you
have one. Fixed bugs move to MODLOG.md.

## Open

- **Water draws black** (night level by the water with a wrecked ship;
  screenshot analysis/bug_water_black.webp, 2026-10-19). The water surface is
  solid black, and a bright grainy white band (spray or mist?) lies across it
  at the horizon. Suspect the water's bump-environment-mapped stage (the NV2A
  texture-shader modes) or a reflection render target, possibly the same
  cause as the odd reflections below.
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
