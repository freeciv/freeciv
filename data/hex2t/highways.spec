
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-3.3-Devel-2023.Apr.05"

[info]

artists = "
    Peter van der Meer (highways)
"

[file]
gfx = "hex2t/highways"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 40
dy = 72
pixel_border = 1

tiles = { "row", "column","tag"

; Roads, Highways

 0, 0, "road.highway_n:0"
 0, 1, "road.highway_ne:0"
 0, 2, "road.highway_e:0"
 0, 7, "road.highway_se:0"
 0, 3, "road.highway_s:0"
 0, 4, "road.highway_sw:0"
 0, 5, "road.highway_w:0"
 0, 6, "road.highway_nw:0"

 0, 8, "road.highway_isolated:0"
}
