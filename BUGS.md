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
- **Music sometimes doesn't play** (2026-10-19). In the scripted runs the
  mission's music action fires once, at the start: it plays music cue
  0x2000D (sets it as current and fallback track, level 4000), whose wave
  is Music.DVDxwb entry 17, a 9.6 s non-looping intro (the long tracks are
  the looping entries). The intro streams and plays to the end, its voice
  is freed normally, and then nothing: no further music action fires and the
  bank is never read again, walking 117 units over 100 s. Not yet known
  whether the mission should be quiet until combat raises the intensity, or
  whether the track after the intro is lost. Needs: where in play music is
  missing. MA2_MUSIC_TRACE=1 logs every music action.
