
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-3.3-Devel-2023.Apr.05"

[info]

artists = "
    Hogne Håskjold <hogne@freeciv.org>[HH]
    GriffonSpade[GS]
"

[file]
gfx = "alio/fortresses"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 126
dy = 96
pixel_border = 1

tiles = { "row", "column", "tag"

 0,  0, "base.force_fortress_bg:0"
 1,  0, "base.force_fortress_fg:0"

 0,  1, "base.tower_bg:0"
 1,  1, "base.tower_fg:0"
}
