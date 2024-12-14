[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-3.3-Devel-2023.Apr.05"

[info]

artists = "
; software by
    Jason Dorje Short <jdorje@freeciv.org>
; adapted to hex2t
    Daniel Markstedt <himasaram@spray.se>
"

[file]
gfx = "hex2t/select"

[grid_main]

x_top_left = 0
y_top_left = 0
dx = 40
dy = 37
pixel_border = 0

tiles = { "row", "column", "tag"
  0, 0, "unit.select:0"
  0, 1, "unit.select:1"
  0, 2, "unit.select:2"
  0, 3, "unit.select:3"
}
