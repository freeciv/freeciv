
[spec]

; Format and options of this spec file:
options = "+spec2"

[info]

artists = "
    Tim F. Smith <yoohootim@hotmail.com>
"

[file]
gfx = "isotrident/terrain2"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 64
dy = 32
is_pixel_border = 1

tiles = { "row", "column","tag"
; Rivers (as special type), and whether north, south, east, west 
; also has river or is ocean:

 2,  0, "tx.s_river_n0s0e0w0"
 2,  1, "tx.s_river_n1s0e0w0"
 2,  2, "tx.s_river_n0s0e1w0"
 2,  3, "tx.s_river_n1s0e1w0"
 2,  4, "tx.s_river_n0s1e0w0"
 2,  5, "tx.s_river_n1s1e0w0"
 2,  6, "tx.s_river_n0s1e1w0"
 2,  7, "tx.s_river_n1s1e1w0"
 3,  0, "tx.s_river_n0s0e0w1"
 3,  1, "tx.s_river_n1s0e0w1"
 3,  2, "tx.s_river_n0s0e1w1"
 3,  3, "tx.s_river_n1s0e1w1"
 3,  4, "tx.s_river_n0s1e0w1"
 3,  5, "tx.s_river_n1s1e0w1"
 3,  6, "tx.s_river_n0s1e1w1"
 3,  7, "tx.s_river_n1s1e1w1"

; Rivers as overlay

 2,  0, "t.t_river_n0s0e0w0"
 2,  1, "t.t_river_n1s0e0w0"
 2,  2, "t.t_river_n0s0e1w0"
 2,  3, "t.t_river_n1s0e1w0"
 2,  4, "t.t_river_n0s1e0w0"
 2,  5, "t.t_river_n1s1e0w0"
 2,  6, "t.t_river_n0s1e1w0"
 2,  7, "t.t_river_n1s1e1w0"
 3,  0, "t.t_river_n0s0e0w1"
 3,  1, "t.t_river_n1s0e0w1"
 3,  2, "t.t_river_n0s0e1w1"
 3,  3, "t.t_river_n1s0e1w1"
 3,  4, "t.t_river_n0s1e0w1"
 3,  5, "t.t_river_n1s1e0w1"
 3,  6, "t.t_river_n0s1e1w1"
 3,  7, "t.t_river_n1s1e1w1"

;forrests as overlay

 4,  0, "t.forest_n0s0e0w0"
 4,  1, "t.forest_n1s0e0w0"
 4,  2, "t.forest_n0s0e1w0"
 4,  3, "t.forest_n1s0e1w0"
 4,  4, "t.forest_n0s1e0w0"
 4,  5, "t.forest_n1s1e0w0"
 4,  6, "t.forest_n0s1e1w0"
 4,  7, "t.forest_n1s1e1w0"
 5,  0, "t.forest_n0s0e0w1"
 5,  1, "t.forest_n1s0e0w1"
 5,  2, "t.forest_n0s0e1w1"
 5,  3, "t.forest_n1s0e1w1"
 5,  4, "t.forest_n0s1e0w1"
 5,  5, "t.forest_n1s1e0w1"
 5,  6, "t.forest_n0s1e1w1"
 5,  7, "t.forest_n1s1e1w1"

;mountains as overlay

 6,  0, "t.mountains_n0s0e0w0"
 6,  1, "t.mountains_n1s0e0w0"
 6,  2, "t.mountains_n0s0e1w0"
 6,  3, "t.mountains_n1s0e1w0"
 6,  4, "t.mountains_n0s1e0w0"
 6,  5, "t.mountains_n1s1e0w0"
 6,  6, "t.mountains_n0s1e1w0"
 6,  7, "t.mountains_n1s1e1w0"
 7,  0, "t.mountains_n0s0e0w1"
 7,  1, "t.mountains_n1s0e0w1"
 7,  2, "t.mountains_n0s0e1w1"
 7,  3, "t.mountains_n1s0e1w1"
 7,  4, "t.mountains_n0s1e0w1"
 7,  5, "t.mountains_n1s1e0w1"
 7,  6, "t.mountains_n0s1e1w1"
 7,  7, "t.mountains_n1s1e1w1"

;hills as overlay

 8,  0, "t.hills_n0s0e0w0"
 8,  1, "t.hills_n1s0e0w0"
 8,  2, "t.hills_n0s0e1w0"
 8,  3, "t.hills_n1s0e1w0"
 8,  4, "t.hills_n0s1e0w0"
 8,  5, "t.hills_n1s1e0w0"
 8,  6, "t.hills_n0s1e1w0"
 8,  7, "t.hills_n1s1e1w0"
 9,  0, "t.hills_n0s0e0w1"
 9,  1, "t.hills_n1s0e0w1"
 9,  2, "t.hills_n0s0e1w1"
 9,  3, "t.hills_n1s0e1w1"
 9,  4, "t.hills_n0s1e0w1"
 9,  5, "t.hills_n1s1e0w1"
 9,  6, "t.hills_n0s1e1w1"
 9,  7, "t.hills_n1s1e1w1"

;river outlets

 10, 0, "tx.river_outlet_n"
 10, 1, "tx.river_outlet_e"
 10, 2, "tx.river_outlet_s"
 10, 3, "tx.river_outlet_w"

}


[grid_coasts]

x_top_left = 1
y_top_left = 429
dx = 32
dy = 16
is_pixel_border = 1

tiles = { "row", "column","tag"
 0, 0,  "t.ocean_cell_u0"
 0, 2,  "t.ocean_cell_u1"
 0, 4,  "t.ocean_cell_u2"
 0, 6,  "t.ocean_cell_u3"
 0, 8,  "t.ocean_cell_u4"
 0, 10, "t.ocean_cell_u5"
 0, 12, "t.ocean_cell_u6"
 0, 14, "t.ocean_cell_u7"
 
 1, 0,  "t.ocean_cell_d0"
 1, 2,  "t.ocean_cell_d1"
 1, 4,  "t.ocean_cell_d2"
 1, 6,  "t.ocean_cell_d3"
 1, 8,  "t.ocean_cell_d4"
 1, 10, "t.ocean_cell_d5"
 1, 12, "t.ocean_cell_d6"
 1, 14, "t.ocean_cell_d7"

 2, 0,  "t.ocean_cell_l0"
 2, 2,  "t.ocean_cell_l1"
 2, 4,  "t.ocean_cell_l2"
 2, 6,  "t.ocean_cell_l3"
 2, 8,  "t.ocean_cell_l4"
 2, 10, "t.ocean_cell_l5"
 2, 12, "t.ocean_cell_l6"
 2, 14, "t.ocean_cell_l7"

 2, 1,  "t.ocean_cell_r0"
 2, 3,  "t.ocean_cell_r1"
 2, 5,  "t.ocean_cell_r2"
 2, 7,  "t.ocean_cell_r3"
 2, 9,  "t.ocean_cell_r4"
 2, 11, "t.ocean_cell_r5"
 2, 13, "t.ocean_cell_r6"
 2, 15, "t.ocean_cell_r7"
}
