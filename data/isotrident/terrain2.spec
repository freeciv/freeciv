
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

;forrests as overlay

 4,  0, "tx.s_forest_n0s0e0w0"
 4,  1, "tx.s_forest_n1s0e0w0"
 4,  2, "tx.s_forest_n0s0e1w0"
 4,  3, "tx.s_forest_n1s0e1w0"
 4,  4, "tx.s_forest_n0s1e0w0"
 4,  5, "tx.s_forest_n1s1e0w0"
 4,  6, "tx.s_forest_n0s1e1w0"
 4,  7, "tx.s_forest_n1s1e1w0"
 5,  0, "tx.s_forest_n0s0e0w1"
 5,  1, "tx.s_forest_n1s0e0w1"
 5,  2, "tx.s_forest_n0s0e1w1"
 5,  3, "tx.s_forest_n1s0e1w1"
 5,  4, "tx.s_forest_n0s1e0w1"
 5,  5, "tx.s_forest_n1s1e0w1"
 5,  6, "tx.s_forest_n0s1e1w1"
 5,  7, "tx.s_forest_n1s1e1w1"

;mountains as overlay

 6,  0, "tx.s_mountain_n0s0e0w0"
 6,  1, "tx.s_mountain_n1s0e0w0"
 6,  2, "tx.s_mountain_n0s0e1w0"
 6,  3, "tx.s_mountain_n1s0e1w0"
 6,  4, "tx.s_mountain_n0s1e0w0"
 6,  5, "tx.s_mountain_n1s1e0w0"
 6,  6, "tx.s_mountain_n0s1e1w0"
 6,  7, "tx.s_mountain_n1s1e1w0"
 7,  0, "tx.s_mountain_n0s0e0w1"
 7,  1, "tx.s_mountain_n1s0e0w1"
 7,  2, "tx.s_mountain_n0s0e1w1"
 7,  3, "tx.s_mountain_n1s0e1w1"
 7,  4, "tx.s_mountain_n0s1e0w1"
 7,  5, "tx.s_mountain_n1s1e0w1"
 7,  6, "tx.s_mountain_n0s1e1w1"
 7,  7, "tx.s_mountain_n1s1e1w1"

;hills as overlay

 8,  0, "tx.s_hill_n0s0e0w0"
 8,  1, "tx.s_hill_n1s0e0w0"
 8,  2, "tx.s_hill_n0s0e1w0"
 8,  3, "tx.s_hill_n1s0e1w0"
 8,  4, "tx.s_hill_n0s1e0w0"
 8,  5, "tx.s_hill_n1s1e0w0"
 8,  6, "tx.s_hill_n0s1e1w0"
 8,  7, "tx.s_hill_n1s1e1w0"
 9,  0, "tx.s_hill_n0s0e0w1"
 9,  1, "tx.s_hill_n1s0e0w1"
 9,  2, "tx.s_hill_n0s0e1w1"
 9,  3, "tx.s_hill_n1s0e1w1"
 9,  4, "tx.s_hill_n0s1e0w1"
 9,  5, "tx.s_hill_n1s1e0w1"
 9,  6, "tx.s_hill_n0s1e1w1"
 9,  7, "tx.s_hill_n1s1e1w1"

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
 0, 0,  "tx.coast_cape_u0"
 0, 2,  "tx.coast_cape_u1"
 0, 4,  "tx.coast_cape_u2"
 0, 6,  "tx.coast_cape_u3"
 0, 8,  "tx.coast_cape_u4"
 0, 10, "tx.coast_cape_u5"
 0, 12, "tx.coast_cape_u6"
 0, 14, "tx.coast_cape_u7"
 
 1, 0,  "tx.coast_cape_d0"
 1, 2,  "tx.coast_cape_d1"
 1, 4,  "tx.coast_cape_d2"
 1, 6,  "tx.coast_cape_d3"
 1, 8,  "tx.coast_cape_d4"
 1, 10, "tx.coast_cape_d5"
 1, 12, "tx.coast_cape_d6"
 1, 14, "tx.coast_cape_d7"

 2, 0,  "tx.coast_cape_l0"
 2, 2,  "tx.coast_cape_l1"
 2, 4,  "tx.coast_cape_l2"
 2, 6,  "tx.coast_cape_l3"
 2, 8,  "tx.coast_cape_l4"
 2, 10, "tx.coast_cape_l5"
 2, 12, "tx.coast_cape_l6"
 2, 14, "tx.coast_cape_l7"

 2, 1,  "tx.coast_cape_r0"
 2, 3,  "tx.coast_cape_r1"
 2, 5,  "tx.coast_cape_r2"
 2, 7,  "tx.coast_cape_r3"
 2, 9,  "tx.coast_cape_r4"
 2, 11, "tx.coast_cape_r5"
 2, 13, "tx.coast_cape_r6"
 2, 15, "tx.coast_cape_r7"
}
