[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2015-Mar-25"

[info]

artists = "
    Airfield, city walls and misc stuff Hogne Håskjold <hogne@freeciv.org>
    Buoy by Eleazar
    Ruins by Vincent Croisier
    Fort and Airstrip by GriffonSpade
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
 0,  0, "base.airbase_mg"
 0,  1, "tx.airbase_full"
 0,  3, "base.fortress_fg"
 0,  4, "base.fortress_bg"
 1,  0, "base.airstrip_mg"
 1,  1, "base.buoy_mg"
 1,  2, "base.ruins_mg"
 1,  3, "base.outpost_fg"
 1,  4, "base.outpost_bg"
}
