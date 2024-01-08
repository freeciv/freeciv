
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-3.3-Devel-2023.Apr.05"

[info]

artists = "
    Peter van der Meer (highways)
"

[file]
gfx = "trident/highways"

[grid_highways]

x_top_left = 0
y_top_left = 0
dx = 30
dy = 30

tiles = { "row", "column", "tag"

  0,  0, "road.highway_isolated"

; Cardinal highways, connections north, south, east, west:

  0,  1, "road.highway_c_n1e0s0w0"
  0,  2, "road.highway_c_n0e1s0w0"
  0,  3, "road.highway_c_n1e1s0w0"
  0,  4, "road.highway_c_n0e0s1w0"
  0,  5, "road.highway_c_n1e0s1w0"
  0,  6, "road.highway_c_n0e1s1w0"
  0,  7, "road.highway_c_n1e1s1w0"
  0,  8, "road.highway_c_n0e0s0w1"
  0,  9, "road.highway_c_n1e0s0w1"
  0, 10, "road.highway_c_n0e1s0w1"
  0, 11, "road.highway_c_n1e1s0w1"
  0, 12, "road.highway_c_n0e0s1w1"
  0, 13, "road.highway_c_n1e0s1w1"
  0, 14, "road.highway_c_n0e1s1w1"
  0, 15, "road.highway_c_n1e1s1w1"

; Diagonal highways, connections same, rotated 45 degrees clockwise:

  1,  1, "road.highway_d_ne1se0sw0nw0"
  1,  2, "road.highway_d_ne0se1sw0nw0"
  1,  3, "road.highway_d_ne1se1sw0nw0"
  1,  4, "road.highway_d_ne0se0sw1nw0"
  1,  5, "road.highway_d_ne1se0sw1nw0"
  1,  6, "road.highway_d_ne0se1sw1nw0"
  1,  7, "road.highway_d_ne1se1sw1nw0"
  1,  8, "road.highway_d_ne0se0sw0nw1"
  1,  9, "road.highway_d_ne1se0sw0nw1"
  1, 10, "road.highway_d_ne0se1sw0nw1"
  1, 11, "road.highway_d_ne1se1sw0nw1"
  1, 12, "road.highway_d_ne0se0sw1nw1"
  1, 13, "road.highway_d_ne1se0sw1nw1"
  1, 14, "road.highway_d_ne0se1sw1nw1"
  1, 15, "road.highway_d_ne1se1sw1nw1"

; highway corners

  0, 16, "road.highway_c_nw"
  0, 17, "road.highway_c_ne"
  1, 16, "road.highway_c_sw"
  1, 17, "road.highway_c_se"
}
