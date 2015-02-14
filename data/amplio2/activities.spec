
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2013.Feb.13"

[info]

artists = "
    GriffonSpade
"

[file]
gfx = "amplio2/activities"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 96
dy = 48
pixel_border = 1

tiles = { "row", "column", "tag"
 0,  0, "unit.road"
 0,  1, "unit.rail"
 0,  2, "unit.maglev"

 1,  0, "unit.fort"
 1,  1, "unit.airstrip"

 2,  0, "unit.convert"
 2,  1, "unit.fortifying"
}
