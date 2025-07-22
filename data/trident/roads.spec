
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-3.3-Devel-2023.Apr.05"

[info]

artists = "
    Tatu Rissanen <tatu.rissanen@hut.fi>
    David Pfitzner <dwp@mso.anu.edu.au> (original design)
"

[file]
gfx = "trident/roads"

[grid_roads]

x_top_left = 0
y_top_left = 0
dx = 30
dy = 30

tiles = { "row", "column", "tag"

  0,  0, "road.road_isolated:0"

; Cardinal roads, connections north, south, east, west:

  0,  1, "road.road_c_n1e0s0w0:0"
  0,  2, "road.road_c_n0e1s0w0:0"
  0,  3, "road.road_c_n1e1s0w0:0"
  0,  4, "road.road_c_n0e0s1w0:0"
  0,  5, "road.road_c_n1e0s1w0:0"
  0,  6, "road.road_c_n0e1s1w0:0"
  0,  7, "road.road_c_n1e1s1w0:0"
  0,  8, "road.road_c_n0e0s0w1:0"
  0,  9, "road.road_c_n1e0s0w1:0"
  0, 10, "road.road_c_n0e1s0w1:0"
  0, 11, "road.road_c_n1e1s0w1:0"
  0, 12, "road.road_c_n0e0s1w1:0"
  0, 13, "road.road_c_n1e0s1w1:0"
  0, 14, "road.road_c_n0e1s1w1:0"
  0, 15, "road.road_c_n1e1s1w1:0"

; Diagonal roads, connections same, rotated 45 degrees clockwise:

  1,  1, "road.road_d_ne1se0sw0nw0:0"
  1,  2, "road.road_d_ne0se1sw0nw0:0"
  1,  3, "road.road_d_ne1se1sw0nw0:0"
  1,  4, "road.road_d_ne0se0sw1nw0:0"
  1,  5, "road.road_d_ne1se0sw1nw0:0"
  1,  6, "road.road_d_ne0se1sw1nw0:0"
  1,  7, "road.road_d_ne1se1sw1nw0:0"
  1,  8, "road.road_d_ne0se0sw0nw1:0"
  1,  9, "road.road_d_ne1se0sw0nw1:0"
  1, 10, "road.road_d_ne0se1sw0nw1:0"
  1, 11, "road.road_d_ne1se1sw0nw1:0"
  1, 12, "road.road_d_ne0se0sw1nw1:0"
  1, 13, "road.road_d_ne1se0sw1nw1:0"
  1, 14, "road.road_d_ne0se1sw1nw1:0"
  1, 15, "road.road_d_ne1se1sw1nw1:0"
}

[grid_rails]

x_top_left = 0
y_top_left = 60	  ; Change to 0 for original trident rails, 60 for dwp-style
dx = 30
dy = 30

tiles = { "row", "column", "tag"

  2,  0, "road.rail_isolated:0"

; Cardinal rails, connections north, south, east, west:

  2,  1, "road.rail_c_n1e0s0w0:0"
  2,  2, "road.rail_c_n0e1s0w0:0"
  2,  3, "road.rail_c_n1e1s0w0:0"
  2,  4, "road.rail_c_n0e0s1w0:0"
  2,  5, "road.rail_c_n1e0s1w0:0"
  2,  6, "road.rail_c_n0e1s1w0:0"
  2,  7, "road.rail_c_n1e1s1w0:0"
  2,  8, "road.rail_c_n0e0s0w1:0"
  2,  9, "road.rail_c_n1e0s0w1:0"
  2, 10, "road.rail_c_n0e1s0w1:0"
  2, 11, "road.rail_c_n1e1s0w1:0"
  2, 12, "road.rail_c_n0e0s1w1:0"
  2, 13, "road.rail_c_n1e0s1w1:0"
  2, 14, "road.rail_c_n0e1s1w1:0"
  2, 15, "road.rail_c_n1e1s1w1:0"

; Diagonal rails, connections same, rotated 45 degrees clockwise:

  3,  1, "road.rail_d_ne1se0sw0nw0:0"
  3,  2, "road.rail_d_ne0se1sw0nw0:0"
  3,  3, "road.rail_d_ne1se1sw0nw0:0"
  3,  4, "road.rail_d_ne0se0sw1nw0:0"
  3,  5, "road.rail_d_ne1se0sw1nw0:0"
  3,  6, "road.rail_d_ne0se1sw1nw0:0"
  3,  7, "road.rail_d_ne1se1sw1nw0:0"
  3,  8, "road.rail_d_ne0se0sw0nw1:0"
  3,  9, "road.rail_d_ne1se0sw0nw1:0"
  3, 10, "road.rail_d_ne0se1sw0nw1:0"
  3, 11, "road.rail_d_ne1se1sw0nw1:0"
  3, 12, "road.rail_d_ne0se0sw1nw1:0"
  3, 13, "road.rail_d_ne1se0sw1nw1:0"
  3, 14, "road.rail_d_ne0se1sw1nw1:0"
  3, 15, "road.rail_d_ne1se1sw1nw1:0"

; Rail corners

  2, 16, "road.rail_c_nw:0"
  2, 17, "road.rail_c_ne:0"
  3, 16, "road.rail_c_sw:0"
  3, 17, "road.rail_c_se:0"

}

[grid_maglev]

x_top_left = 0
y_top_left = 180
dx = 30
dy = 30

tiles = { "row", "column", "tag"

  0,  0, "road.maglev_isolated:0"

; Cardinal maglevs, connections north, south, east, west:

  0,  1, "road.maglev_c_n1e0s0w0:0"
  0,  2, "road.maglev_c_n0e1s0w0:0"
  0,  3, "road.maglev_c_n1e1s0w0:0"
  0,  4, "road.maglev_c_n0e0s1w0:0"
  0,  5, "road.maglev_c_n1e0s1w0:0"
  0,  6, "road.maglev_c_n0e1s1w0:0"
  0,  7, "road.maglev_c_n1e1s1w0:0"
  0,  8, "road.maglev_c_n0e0s0w1:0"
  0,  9, "road.maglev_c_n1e0s0w1:0"
  0, 10, "road.maglev_c_n0e1s0w1:0"
  0, 11, "road.maglev_c_n1e1s0w1:0"
  0, 12, "road.maglev_c_n0e0s1w1:0"
  0, 13, "road.maglev_c_n1e0s1w1:0"
  0, 14, "road.maglev_c_n0e1s1w1:0"
  0, 15, "road.maglev_c_n1e1s1w1:0"

; Diagonal maglevs, connections same, rotated 45 degrees clockwise:

  1,  1, "road.maglev_d_ne1se0sw0nw0:0"
  1,  2, "road.maglev_d_ne0se1sw0nw0:0"
  1,  3, "road.maglev_d_ne1se1sw0nw0:0"
  1,  4, "road.maglev_d_ne0se0sw1nw0:0"
  1,  5, "road.maglev_d_ne1se0sw1nw0:0"
  1,  6, "road.maglev_d_ne0se1sw1nw0:0"
  1,  7, "road.maglev_d_ne1se1sw1nw0:0"
  1,  8, "road.maglev_d_ne0se0sw0nw1:0"
  1,  9, "road.maglev_d_ne1se0sw0nw1:0"
  1, 10, "road.maglev_d_ne0se1sw0nw1:0"
  1, 11, "road.maglev_d_ne1se1sw0nw1:0"
  1, 12, "road.maglev_d_ne0se0sw1nw1:0"
  1, 13, "road.maglev_d_ne1se0sw1nw1:0"
  1, 14, "road.maglev_d_ne0se1sw1nw1:0"
  1, 15, "road.maglev_d_ne1se1sw1nw1:0"

; maglev corners

  0, 16, "road.maglev_c_nw:0"
  0, 17, "road.maglev_c_ne:0"
  1, 16, "road.maglev_c_sw:0"
  1, 17, "road.maglev_c_se:0"

}
