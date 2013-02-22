
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
gfx = "isophex/rivers"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 64
dy = 32
pixel_border = 1

tiles = { "row", "column","tag"
; Rivers (as special type), and whether north, south, east, west 
; also has river or is ocean:

 2,  0, "river_s_n0e0se0s0w0nw0"
 2,  1, "river_s_n1e0se0s0w0nw0"
 2,  2, "river_s_n0e1se0s0w0nw0"
 2,  3, "river_s_n1e1se0s0w0nw0"
 2,  4, "river_s_n0e0se0s1w0nw0"
 2,  5, "river_s_n1e0se0s1w0nw0"
 2,  6, "river_s_n0e1se0s1w0nw0"
 2,  7, "river_s_n1e1se0s1w0nw0"
 3,  0, "river_s_n0e0se0s0w1nw0"
 3,  1, "river_s_n1e0se0s0w1nw0"
 3,  2, "river_s_n0e1se0s0w1nw0"
 3,  3, "river_s_n1e1se0s0w1nw0"
 3,  4, "river_s_n0e0se0s1w1nw0"
 3,  5, "river_s_n1e0se0s1w1nw0"
 3,  6, "river_s_n0e1se0s1w1nw0"
 3,  7, "river_s_n1e1se0s1w1nw0"

 4,  0, "river_s_n0e0se1s0w0nw0"
 4,  1, "river_s_n1e0se1s0w0nw0"
 4,  2, "river_s_n0e1se1s0w0nw0"
 4,  3, "river_s_n1e1se1s0w0nw0"
 4,  4, "river_s_n0e0se1s1w0nw0"
 4,  5, "river_s_n1e0se1s1w0nw0"
 4,  6, "river_s_n0e1se1s1w0nw0"
 4,  7, "river_s_n1e1se1s1w0nw0"
 5,  0, "river_s_n0e0se1s0w1nw0"
 5,  1, "river_s_n1e0se1s0w1nw0"
 5,  2, "river_s_n0e1se1s0w1nw0"
 5,  3, "river_s_n1e1se1s0w1nw0"
 5,  4, "river_s_n0e0se1s1w1nw0"
 5,  5, "river_s_n1e0se1s1w1nw0"
 5,  6, "river_s_n0e1se1s1w1nw0"
 5,  7, "river_s_n1e1se1s1w1nw0"

 6,  0, "river_s_n0e0se0s0w0nw1"
 6,  1, "river_s_n1e0se0s0w0nw1"
 6,  2, "river_s_n0e1se0s0w0nw1"
 6,  3, "river_s_n1e1se0s0w0nw1"
 6,  4, "river_s_n0e0se0s1w0nw1"
 6,  5, "river_s_n1e0se0s1w0nw1"
 6,  6, "river_s_n0e1se0s1w0nw1"
 6,  7, "river_s_n1e1se0s1w0nw1"
 7,  0, "river_s_n0e0se0s0w1nw1"
 7,  1, "river_s_n1e0se0s0w1nw1"
 7,  2, "river_s_n0e1se0s0w1nw1"
 7,  3, "river_s_n1e1se0s0w1nw1"
 7,  4, "river_s_n0e0se0s1w1nw1"
 7,  5, "river_s_n1e0se0s1w1nw1"
 7,  6, "river_s_n0e1se0s1w1nw1"
 7,  7, "river_s_n1e1se0s1w1nw1"

 8,  0, "river_s_n0e0se1s0w0nw1"
 8,  1, "river_s_n1e0se1s0w0nw1"
 8,  2, "river_s_n0e1se1s0w0nw1"
 8,  3, "river_s_n1e1se1s0w0nw1"
 8,  4, "river_s_n0e0se1s1w0nw1"
 8,  5, "river_s_n1e0se1s1w0nw1"
 8,  6, "river_s_n0e1se1s1w0nw1"
 8,  7, "river_s_n1e1se1s1w0nw1"
 9,  0, "river_s_n0e0se1s0w1nw1"
 9,  1, "river_s_n1e0se1s0w1nw1"
 9,  2, "river_s_n0e1se1s0w1nw1"
 9,  3, "river_s_n1e1se1s0w1nw1"
 9,  4, "river_s_n0e0se1s1w1nw1"
 9,  5, "river_s_n1e0se1s1w1nw1"
 9,  6, "river_s_n0e1se1s1w1nw1"
 9,  7, "river_s_n1e1se1s1w1nw1"

}
