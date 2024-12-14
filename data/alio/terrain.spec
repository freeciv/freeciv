[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-3.3-Devel-2023.Apr.05"

[info]

; amplio2 oil rig became thermal vent
; amplio2 coal became glowing rocks
; amplio2 tundra became radiating rocks and alien forest bg
; Wesnoth graphic became huge plant, alien mine and alien forest fg
; TODO: add individual artists if they are identified
artists = "
    GriffonSpade
    The amplio2 artists
    The Wesnoth artists
"

[file]
gfx = "alio/terrain"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 126
dy = 64
pixel_border = 1

tiles = { "row", "column","tag"
  0,    0,  "ts.thermal_vent:0"
  1,    0,  "ts.glowing_rocks:0"
  2,    0,  "ts.huge_plant:0"
  3,    0,  "ts.alien_mine:0"

  0,    1,  "t.l0.radiating_rocks1"
  5,    1,  "t.l1.radiating_rocks1"

  0,    1,  "t.l0.alien_forest1"
  5,    1,  "t.l1.alien_forest1"
  1,    1,  "t.l2.alien_forest1"
}
