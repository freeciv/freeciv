
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2013.Feb.13"

[info]

artists = "
    Tatu Rissanen <tatu.rissanen@hut.fi>
    Jeff Mallatt <jjm@codewell.com> (miscellaneous)
"

[file]
gfx = "hexemplio/unitextras"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 126
dy = 128
pixel_border = 1

tiles = { "row", "column", "tag"
; Unit activity letters:  (note unit icons have just "u.")

; Unit hit-point bars: approx percent of hp remaining

  0,  0, "unit.hp_100"
  0,  1, "unit.hp_90"
  0,  2, "unit.hp_80"
  0,  3, "unit.hp_70"
  0,  4, "unit.hp_60"
  0,  5, "unit.hp_50"
  0,  6, "unit.hp_40"
  0,  7, "unit.hp_30"
  0,  8, "unit.hp_20"
  0,  9, "unit.hp_10"
  0, 10, "unit.hp_0"

; Veteran Levels: up to 10 military honors for experienced units

  1,  0, "unit.vet_1"
  1,  1, "unit.vet_2"
  1,  2, "unit.vet_3"
  1,  3, "unit.vet_4"
  1,  4, "unit.vet_5"
  1,  5, "unit.vet_6"
  1,  6, "unit.vet_7"
  1,  7, "unit.vet_8"
  1,  8, "unit.vet_9"
  1,  9, "unit.vet_10"
  1, 10, "unit.vet_11"

; Unit special info

  2, 0, "unit.stack"
  2, 1, "unit.loaded"
  2, 2, "unit.tired"
;  2, 3, "unit.lowfuel"
  2, 4, "unit.lowfuel"
  2, 5, "unit.connect"
  2, 6, "unit.auto_attack",
        "unit.auto_settler"

; Unit activity letters: unit commands

  3, 0, "unit.fortified"
  3, 1, "unit.fortifying"
  3, 2, "unit.auto_explore"
  3, 3, "unit.sentry"
  3, 4, "unit.goto"
  3, 5, "unit.patrol"
  3, 6, "unit.convert"

; Unit activity letters: tile commands

  4, 0, "unit.rest"
  4, 1, "unit.irrigate"
  4, 2, "unit.mine"
  4, 3, "unit.transform"
  4, 4, "unit.pillage"
  4, 5, "unit.pollution"
  4, 6, "unit.fallout"
  4, 7, "unit.farm"

; Unit Activities: roads

  5, 0, "unit.road"
  5, 1, "unit.rail"
  5, 2, "unit.pave"
  5, 3, "unit.maglev"

; Unit activities: bases

  5,  4, "unit.outpost"
  5,  5, "unit.fortress"
  5,  6, "unit.airstrip"
  5,  7, "unit.airbase"
  5,  8, "unit.buoy"

}
