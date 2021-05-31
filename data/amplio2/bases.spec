[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2019-Jul-03"

[info]

artists = "
    Hogne Håskjold <hogne@freeciv.org>[HH]
    Eleazar [El](buoy)
    Anton Ecker (Kaldred) (ruins)
    GriffonSpade [GS]
    Sveinung Kvilhaugsvik [SK]
    Lexxie L. [Lexxie]
"

[file]
gfx = "amplio2/bases"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 96
dy = 72
pixel_border = 1

tiles = { "row", "column", "tag"
;[HH][GS]
 0,  0, "base.airbase_mg"
 0,  1, "tx.airbase_full"
;[HH]
 0,  3, "base.fortress_fg"
 0,  4, "base.fortress_bg"
;[HH][GS]
 1,  0, "base.airstrip_mg"
;[El]
 1,  1, "base.buoy_mg"
;[VC]
 1,  2, "extra.ruins_mg"
;[HH][GS]
 1,  3, "base.outpost_fg"
 1,  4, "base.outpost_bg"
; [SK]
 1,  5, "extra.transport_hub_mg"
 ;[Lexxie]
 2,  2, "base.castle2_fg"        ; alternate castle (larger 'closed' version which makes hidden units more plausible ...
 2,  3, "base.castle2_bg"        ; ... but also hides the terrain graphics)
 2,  4, "base.castle_fg"
 2,  5, "base.castle_bg"
;[HH]
 0,  2, "cd.occupied",
      "city.european_occupied_0",
      "city.classical_occupied_0",
      "city.asian_occupied_0",
      "city.tropical_occupied_0",
      "city.celtic_occupied_0",
      "city.babylonian_occupied_0",
      "city.industrial_occupied_0",
      "city.electricage_occupied_0",
      "city.modern_occupied_0",
      "city.postmodern_occupied_0"
;[HH]
 0,  5, "city.disorder"
;blank defaults
 0,  6, "cd.city",
      "cd.city_wall"
}
