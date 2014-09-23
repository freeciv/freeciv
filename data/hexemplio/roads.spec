
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2013.Feb.13"

[info]

artists = "
    Tim F. Smith <yoohootim@hotmail.com>
    Daniel Speyer <dspeyer@users.sf.net> (mix)
    Frederic Rodrigo <f.rodrigo@tuxfamily.org> (mix)
    Andreas Røsdal <andrearo@pvv.ntnu.no> (hex mode)
    Jason Short <jdorje@users.sf.net> (hex mixing)
"

[file]
gfx = "hexemplio/roads"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 128
dy = 64
pixel_border = 1

tiles = { "row", "column","tag"
; Rails (as special type), and whether north, south, east, west 

 0,  0, "road.rail_n0e0se0s0w0nw0"
 0,  1, "road.rail_n1e0se0s0w0nw0"
 0,  2, "road.rail_n0e1se0s0w0nw0"
 0,  3, "road.rail_n1e1se0s0w0nw0"
 0,  4, "road.rail_n0e0se0s1w0nw0"
 0,  5, "road.rail_n1e0se0s1w0nw0"
 0,  6, "road.rail_n0e1se0s1w0nw0"
 0,  7, "road.rail_n1e1se0s1w0nw0"
 1,  0, "road.rail_n0e0se0s0w1nw0"
 1,  1, "road.rail_n1e0se0s0w1nw0"
 1,  2, "road.rail_n0e1se0s0w1nw0"
 1,  3, "road.rail_n1e1se0s0w1nw0"
 1,  4, "road.rail_n0e0se0s1w1nw0"
 1,  5, "road.rail_n1e0se0s1w1nw0"
 1,  6, "road.rail_n0e1se0s1w1nw0"
 1,  7, "road.rail_n1e1se0s1w1nw0"

 2,  0, "road.rail_n0e0se1s0w0nw0"
 2,  1, "road.rail_n1e0se1s0w0nw0"
 2,  2, "road.rail_n0e1se1s0w0nw0"
 2,  3, "road.rail_n1e1se1s0w0nw0"
 2,  4, "road.rail_n0e0se1s1w0nw0"
 2,  5, "road.rail_n1e0se1s1w0nw0"
 2,  6, "road.rail_n0e1se1s1w0nw0"
 2,  7, "road.rail_n1e1se1s1w0nw0"
 3,  0, "road.rail_n0e0se1s0w1nw0"
 3,  1, "road.rail_n1e0se1s0w1nw0"
 3,  2, "road.rail_n0e1se1s0w1nw0"
 3,  3, "road.rail_n1e1se1s0w1nw0"
 3,  4, "road.rail_n0e0se1s1w1nw0"
 3,  5, "road.rail_n1e0se1s1w1nw0"
 3,  6, "road.rail_n0e1se1s1w1nw0"
 3,  7, "road.rail_n1e1se1s1w1nw0"

 4,  0, "road.rail_n0e0se0s0w0nw1"
 4,  1, "road.rail_n1e0se0s0w0nw1"
 4,  2, "road.rail_n0e1se0s0w0nw1"
 4,  3, "road.rail_n1e1se0s0w0nw1"
 4,  4, "road.rail_n0e0se0s1w0nw1"
 4,  5, "road.rail_n1e0se0s1w0nw1"
 4,  6, "road.rail_n0e1se0s1w0nw1"
 4,  7, "road.rail_n1e1se0s1w0nw1"
 5,  0, "road.rail_n0e0se0s0w1nw1"
 5,  1, "road.rail_n1e0se0s0w1nw1"
 5,  2, "road.rail_n0e1se0s0w1nw1"
 5,  3, "road.rail_n1e1se0s0w1nw1"
 5,  4, "road.rail_n0e0se0s1w1nw1"
 5,  5, "road.rail_n1e0se0s1w1nw1"
 5,  6, "road.rail_n0e1se0s1w1nw1"
 5,  7, "road.rail_n1e1se0s1w1nw1"

 6,  0, "road.rail_n0e0se1s0w0nw1"
 6,  1, "road.rail_n1e0se1s0w0nw1"
 6,  2, "road.rail_n0e1se1s0w0nw1"
 6,  3, "road.rail_n1e1se1s0w0nw1"
 6,  4, "road.rail_n0e0se1s1w0nw1"
 6,  5, "road.rail_n1e0se1s1w0nw1"
 6,  6, "road.rail_n0e1se1s1w0nw1"
 6,  7, "road.rail_n1e1se1s1w0nw1"
 7,  0, "road.rail_n0e0se1s0w1nw1"
 7,  1, "road.rail_n1e0se1s0w1nw1"
 7,  2, "road.rail_n0e1se1s0w1nw1"
 7,  3, "road.rail_n1e1se1s0w1nw1"
 7,  4, "road.rail_n0e0se1s1w1nw1"
 7,  5, "road.rail_n1e0se1s1w1nw1"
 7,  6, "road.rail_n0e1se1s1w1nw1"
 7,  7, "road.rail_n1e1se1s1w1nw1"

; Monorails (as special type), and whether north, south, east, west 

 8,  0, "road.maglev_n0e0se0s0w0nw0"
 8,  1, "road.maglev_n1e0se0s0w0nw0"
 8,  2, "road.maglev_n0e1se0s0w0nw0"
 8,  3, "road.maglev_n1e1se0s0w0nw0"
 8,  4, "road.maglev_n0e0se0s1w0nw0"
 8,  5, "road.maglev_n1e0se0s1w0nw0"
 8,  6, "road.maglev_n0e1se0s1w0nw0"
 8,  7, "road.maglev_n1e1se0s1w0nw0"
 9,  0, "road.maglev_n0e0se0s0w1nw0"
 9,  1, "road.maglev_n1e0se0s0w1nw0"
 9,  2, "road.maglev_n0e1se0s0w1nw0"
 9,  3, "road.maglev_n1e1se0s0w1nw0"
 9,  4, "road.maglev_n0e0se0s1w1nw0"
 9,  5, "road.maglev_n1e0se0s1w1nw0"
 9,  6, "road.maglev_n0e1se0s1w1nw0"
 9,  7, "road.maglev_n1e1se0s1w1nw0"

10,  0, "road.maglev_n0e0se1s0w0nw0"
10,  1, "road.maglev_n1e0se1s0w0nw0"
10,  2, "road.maglev_n0e1se1s0w0nw0"
10,  3, "road.maglev_n1e1se1s0w0nw0"
10,  4, "road.maglev_n0e0se1s1w0nw0"
10,  5, "road.maglev_n1e0se1s1w0nw0"
10,  6, "road.maglev_n0e1se1s1w0nw0"
10,  7, "road.maglev_n1e1se1s1w0nw0"
11,  0, "road.maglev_n0e0se1s0w1nw0"
11,  1, "road.maglev_n1e0se1s0w1nw0"
11,  2, "road.maglev_n0e1se1s0w1nw0"
11,  3, "road.maglev_n1e1se1s0w1nw0"
11,  4, "road.maglev_n0e0se1s1w1nw0"
11,  5, "road.maglev_n1e0se1s1w1nw0"
11,  6, "road.maglev_n0e1se1s1w1nw0"
11,  7, "road.maglev_n1e1se1s1w1nw0"

12,  0, "road.maglev_n0e0se0s0w0nw1"
12,  1, "road.maglev_n1e0se0s0w0nw1"
12,  2, "road.maglev_n0e1se0s0w0nw1"
12,  3, "road.maglev_n1e1se0s0w0nw1"
12,  4, "road.maglev_n0e0se0s1w0nw1"
12,  5, "road.maglev_n1e0se0s1w0nw1"
12,  6, "road.maglev_n0e1se0s1w0nw1"
12,  7, "road.maglev_n1e1se0s1w0nw1"
13,  0, "road.maglev_n0e0se0s0w1nw1"
13,  1, "road.maglev_n1e0se0s0w1nw1"
13,  2, "road.maglev_n0e1se0s0w1nw1"
13,  3, "road.maglev_n1e1se0s0w1nw1"
13,  4, "road.maglev_n0e0se0s1w1nw1"
13,  5, "road.maglev_n1e0se0s1w1nw1"
13,  6, "road.maglev_n0e1se0s1w1nw1"
13,  7, "road.maglev_n1e1se0s1w1nw1"

14,  0, "road.maglev_n0e0se1s0w0nw1"
14,  1, "road.maglev_n1e0se1s0w0nw1"
14,  2, "road.maglev_n0e1se1s0w0nw1"
14,  3, "road.maglev_n1e1se1s0w0nw1"
14,  4, "road.maglev_n0e0se1s1w0nw1"
14,  5, "road.maglev_n1e0se1s1w0nw1"
14,  6, "road.maglev_n0e1se1s1w0nw1"
14,  7, "road.maglev_n1e1se1s1w0nw1"
15,  0, "road.maglev_n0e0se1s0w1nw1"
15,  1, "road.maglev_n1e0se1s0w1nw1"
15,  2, "road.maglev_n0e1se1s0w1nw1"
15,  3, "road.maglev_n1e1se1s0w1nw1"
15,  4, "road.maglev_n0e0se1s1w1nw1"
15,  5, "road.maglev_n1e0se1s1w1nw1"
15,  6, "road.maglev_n0e1se1s1w1nw1"
15,  7, "road.maglev_n1e1se1s1w1nw1"

;roads

16,  0, "road.road_isolated"
16,  1, "road.road_n"
16,  2, "road.road_e"
16,  3, "road.road_se"
16,  4, "road.road_s"
16,  5, "road.road_w"
16,  6, "road.road_nw"
19,  7, "road.road_ne"
19,  7, "road.road_sw"

;paved roads

17,  0, "road.pave_isolated"
17,  1, "road.pave_n"
17,  2, "road.pave_e"
17,  3, "road.pave_se"
17,  4, "road.pave_s"
17,  5, "road.pave_w"
17,  6, "road.pave_nw"
19,  7, "road.pave_ne"
19,  7, "road.pave_sw"

;rails
18,  0, "road.rail_isolated"
18,  1, "road.rail_n"
18,  2, "road.rail_e"
18,  3, "road.rail_se"
18,  4, "road.rail_s"
18,  5, "road.rail_w"
18,  6, "road.rail_nw"
19,  7, "road.rail_ne"
19,  7, "road.rail_sw"

;monorails
19,  0, "road.maglev_isolated"
19,  1, "road.maglev_n"
19,  2, "road.maglev_e"
19,  3, "road.maglev_se"
19,  4, "road.maglev_s"
19,  5, "road.maglev_w"
19,  6, "road.maglev_nw"
19,  7, "road.maglev_ne"
19,  7, "road.maglev_sw"


}

