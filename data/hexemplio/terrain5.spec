
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2013.Feb.13"

[info]

artists = "
	 Peter Arbor <peter.arbor@gmail.com>
	 Hogne Håskjold <haskjold@gmail.com>
	 GriffonSpade
"

[file]
gfx = "hexemplio/terrain5"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 128
dy = 64
pixel_border = 1

tiles = { "row", "column","tag"

;mountains as overlay

 0,  0, "t.l2.mountains_n0e0se0s0w0nw0"
 0,  1, "t.l2.mountains_n1e0se0s0w0nw0"
 0,  2, "t.l2.mountains_n0e1se0s0w0nw0"
 0,  3, "t.l2.mountains_n1e1se0s0w0nw0"
 1,  0, "t.l2.mountains_n0e0se0s1w0nw0"
 1,  1, "t.l2.mountains_n1e0se0s1w0nw0"
 1,  2, "t.l2.mountains_n0e1se0s1w0nw0"
 1,  3, "t.l2.mountains_n1e1se0s1w0nw0"
 2,  0, "t.l2.mountains_n0e0se0s0w1nw0"
 2,  1, "t.l2.mountains_n1e0se0s0w1nw0"
 2,  2, "t.l2.mountains_n0e1se0s0w1nw0"
 2,  3, "t.l2.mountains_n1e1se0s0w1nw0"
 3,  0, "t.l2.mountains_n0e0se0s1w1nw0"
 3,  1, "t.l2.mountains_n1e0se0s1w1nw0"
 3,  2, "t.l2.mountains_n0e1se0s1w1nw0"
 3,  3, "t.l2.mountains_n1e1se0s1w1nw0"

 ; The below sprites are duplicates of the previous sprites,
 ; since there aren't graphics for the extra hex directions.
 0,  0, "t.l2.mountains_n0e0se0s0w0nw1"
 0,  1, "t.l2.mountains_n1e0se0s0w0nw1"
 0,  2, "t.l2.mountains_n0e1se0s0w0nw1"
 0,  3, "t.l2.mountains_n1e1se0s0w0nw1"
 1,  0, "t.l2.mountains_n0e0se0s1w0nw1"
 1,  1, "t.l2.mountains_n1e0se0s1w0nw1"
 1,  2, "t.l2.mountains_n0e1se0s1w0nw1"
 1,  3, "t.l2.mountains_n1e1se0s1w0nw1"
 2,  0, "t.l2.mountains_n0e0se0s0w1nw1"
 2,  1, "t.l2.mountains_n1e0se0s0w1nw1"
 2,  2, "t.l2.mountains_n0e1se0s0w1nw1"
 2,  3, "t.l2.mountains_n1e1se0s0w1nw1"
 3,  0, "t.l2.mountains_n0e0se0s1w1nw1"
 3,  1, "t.l2.mountains_n1e0se0s1w1nw1"
 3,  2, "t.l2.mountains_n0e1se0s1w1nw1"
 3,  3, "t.l2.mountains_n1e1se0s1w1nw1"
 0,  0, "t.l2.mountains_n0e0se1s0w0nw0"
 0,  1, "t.l2.mountains_n1e0se1s0w0nw0"
 0,  2, "t.l2.mountains_n0e1se1s0w0nw0"
 0,  3, "t.l2.mountains_n1e1se1s0w0nw0"
 1,  0, "t.l2.mountains_n0e0se1s1w0nw0"
 1,  1, "t.l2.mountains_n1e0se1s1w0nw0"
 1,  2, "t.l2.mountains_n0e1se1s1w0nw0"
 1,  3, "t.l2.mountains_n1e1se1s1w0nw0"
 2,  0, "t.l2.mountains_n0e0se1s0w1nw0"
 2,  1, "t.l2.mountains_n1e0se1s0w1nw0"
 2,  2, "t.l2.mountains_n0e1se1s0w1nw0"
 2,  3, "t.l2.mountains_n1e1se1s0w1nw0"
 3,  0, "t.l2.mountains_n0e0se1s1w1nw0"
 3,  1, "t.l2.mountains_n1e0se1s1w1nw0"
 3,  2, "t.l2.mountains_n0e1se1s1w1nw0"
 3,  3, "t.l2.mountains_n1e1se1s1w1nw0"
 0,  0, "t.l2.mountains_n0e0se1s0w0nw1"
 0,  1, "t.l2.mountains_n1e0se1s0w0nw1"
 0,  2, "t.l2.mountains_n0e1se1s0w0nw1"
 0,  3, "t.l2.mountains_n1e1se1s0w0nw1"
 1,  0, "t.l2.mountains_n0e0se1s1w0nw1"
 1,  1, "t.l2.mountains_n1e0se1s1w0nw1"
 1,  2, "t.l2.mountains_n0e1se1s1w0nw1"
 1,  3, "t.l2.mountains_n1e1se1s1w0nw1"
 2,  0, "t.l2.mountains_n0e0se1s0w1nw1"
 2,  1, "t.l2.mountains_n1e0se1s0w1nw1"
 2,  2, "t.l2.mountains_n0e1se1s0w1nw1"
 2,  3, "t.l2.mountains_n1e1se1s0w1nw1"
 3,  0, "t.l2.mountains_n0e0se1s1w1nw1"
 3,  1, "t.l2.mountains_n1e0se1s1w1nw1"
 3,  2, "t.l2.mountains_n0e1se1s1w1nw1"
 3,  3, "t.l2.mountains_n1e1se1s1w1nw1"

}
