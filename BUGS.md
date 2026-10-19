# MechAssault 2 PC port: open bugs from play-testing

One line per bug: where it happens, what goes wrong, and a screenshot if you
have one. Fixed bugs move to MODLOG.md.

## Open

- **No dynamic shadow under the mech.** The player's mech casts no shadow
  (2026-10-18).
- **Mouse camera doesn't work in the first mech you get** (red, jump jets;
  screenshot 2026-10-18). The mouse look works in the tutorial walker only, so
  this mech's pilot or turning code must take a path the hooks don't watch
  (return addresses 0x001002FB / 0x001739C5 / 0x00175CEF, the camera-list
  entity). Reproduce from the user's save.
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
- **Speech (and some music) plays too quietly, at varying levels.** Not
  silent; mainly the speech (2026-10-18). Suspect: the voices are routed to
  mixbins or DSP effect sends that the APU mixdown attenuates (the downmix
  weights side/rear/send bins at 0.5-0.7 and 0.25), or the peak limiter pulls
  them down under loud effects.
