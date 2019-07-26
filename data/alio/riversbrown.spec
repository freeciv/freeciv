
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2019-Jul-03"

[info]

artists = "
    Yautja (Pond/River Sources)
    GriffonSpade
"

[file]
gfx = "alio/riversbrown"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 126
dy = 64
pixel_border = 1

tiles = { "row", "column","tag"
; Rivers (as special type), and whether north, south, east, west 
; also has river or is ocean:

 0,  0, "road.brown_river_s_n0e0se0s0w0nw0"
 0,  1, "road.brown_river_s_n1e0se0s0w0nw0"
 0,  2, "road.brown_river_s_n0e1se0s0w0nw0"
 0,  3, "road.brown_river_s_n1e1se0s0w0nw0"
 0,  4, "road.brown_river_s_n0e0se0s1w0nw0"
 0,  5, "road.brown_river_s_n1e0se0s1w0nw0"
 0,  6, "road.brown_river_s_n0e1se0s1w0nw0"
 0,  7, "road.brown_river_s_n1e1se0s1w0nw0"
 1,  0, "road.brown_river_s_n0e0se0s0w1nw0"
 1,  1, "road.brown_river_s_n1e0se0s0w1nw0"
 1,  2, "road.brown_river_s_n0e1se0s0w1nw0"
 1,  3, "road.brown_river_s_n1e1se0s0w1nw0"
 1,  4, "road.brown_river_s_n0e0se0s1w1nw0"
 1,  5, "road.brown_river_s_n1e0se0s1w1nw0"
 1,  6, "road.brown_river_s_n0e1se0s1w1nw0"
 1,  7, "road.brown_river_s_n1e1se0s1w1nw0"

 2,  0, "road.brown_river_s_n0e0se1s0w0nw0"
 2,  1, "road.brown_river_s_n1e0se1s0w0nw0"
 2,  2, "road.brown_river_s_n0e1se1s0w0nw0"
 2,  3, "road.brown_river_s_n1e1se1s0w0nw0"
 2,  4, "road.brown_river_s_n0e0se1s1w0nw0"
 2,  5, "road.brown_river_s_n1e0se1s1w0nw0"
 2,  6, "road.brown_river_s_n0e1se1s1w0nw0"
 2,  7, "road.brown_river_s_n1e1se1s1w0nw0"
 3,  0, "road.brown_river_s_n0e0se1s0w1nw0"
 3,  1, "road.brown_river_s_n1e0se1s0w1nw0"
 3,  2, "road.brown_river_s_n0e1se1s0w1nw0"
 3,  3, "road.brown_river_s_n1e1se1s0w1nw0"
 3,  4, "road.brown_river_s_n0e0se1s1w1nw0"
 3,  5, "road.brown_river_s_n1e0se1s1w1nw0"
 3,  6, "road.brown_river_s_n0e1se1s1w1nw0"
 3,  7, "road.brown_river_s_n1e1se1s1w1nw0"

 4,  0, "road.brown_river_s_n0e0se0s0w0nw1"
 4,  1, "road.brown_river_s_n1e0se0s0w0nw1"
 4,  2, "road.brown_river_s_n0e1se0s0w0nw1"
 4,  3, "road.brown_river_s_n1e1se0s0w0nw1"
 4,  4, "road.brown_river_s_n0e0se0s1w0nw1"
 4,  5, "road.brown_river_s_n1e0se0s1w0nw1"
 4,  6, "road.brown_river_s_n0e1se0s1w0nw1"
 4,  7, "road.brown_river_s_n1e1se0s1w0nw1"
 5,  0, "road.brown_river_s_n0e0se0s0w1nw1"
 5,  1, "road.brown_river_s_n1e0se0s0w1nw1"
 5,  2, "road.brown_river_s_n0e1se0s0w1nw1"
 5,  3, "road.brown_river_s_n1e1se0s0w1nw1"
 5,  4, "road.brown_river_s_n0e0se0s1w1nw1"
 5,  5, "road.brown_river_s_n1e0se0s1w1nw1"
 5,  6, "road.brown_river_s_n0e1se0s1w1nw1"
 5,  7, "road.brown_river_s_n1e1se0s1w1nw1"

 6,  0, "road.brown_river_s_n0e0se1s0w0nw1"
 6,  1, "road.brown_river_s_n1e0se1s0w0nw1"
 6,  2, "road.brown_river_s_n0e1se1s0w0nw1"
 6,  3, "road.brown_river_s_n1e1se1s0w0nw1"
 6,  4, "road.brown_river_s_n0e0se1s1w0nw1"
 6,  5, "road.brown_river_s_n1e0se1s1w0nw1"
 6,  6, "road.brown_river_s_n0e1se1s1w0nw1"
 6,  7, "road.brown_river_s_n1e1se1s1w0nw1"
 7,  0, "road.brown_river_s_n0e0se1s0w1nw1"
 7,  1, "road.brown_river_s_n1e0se1s0w1nw1"
 7,  2, "road.brown_river_s_n0e1se1s0w1nw1"
 7,  3, "road.brown_river_s_n1e1se1s0w1nw1"
 7,  4, "road.brown_river_s_n0e0se1s1w1nw1"
 7,  5, "road.brown_river_s_n1e0se1s1w1nw1"
 7,  6, "road.brown_river_s_n0e1se1s1w1nw1"
 7,  7, "road.brown_river_s_n1e1se1s1w1nw1"

;river outlets

 8, 0, "road.brown_river_outlet_n"
 8, 1, "road.brown_river_outlet_e"
 8, 2, "road.brown_river_outlet_s"
 8, 3, "road.brown_river_outlet_w"
 8, 4, "road.brown_river_outlet_nw"
 8, 5, "road.brown_river_outlet_se"

}
