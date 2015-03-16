
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2013.Feb.13"

[info]

artists = "
	 Tim F. Smith <yoohootim@hotmail.com>
	 Daniel Speyer <dspeyer@users.sf.net> (mix)
	 Frederic Rodrigo <f.rodrigo@tuxfamily.org> (mix)
	 Peter Arbor <peter.arbor@gmail.com>
	 Hogne Håskjold <haskjold@gmail.com>
	 GriffonSpade
"

[file]
gfx = "hexemplio/terrain2"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 126
dy = 64
pixel_border = 1

tiles = { "row", "column","tag"

;jungles as overlay
;Center

 0,  0, "t.l2.jungle_n0e0se0s0w0nw0"
 0,  0, "t.l2.jungle_n0e1se0s0w0nw0"
 0,  0, "t.l2.jungle_n0e0se0s1w0nw0"
 0,  0, "t.l2.jungle_n0e1se0s1w0nw0"
 0,  0, "t.l2.jungle_n0e0se0s0w0nw1"
 0,  0, "t.l2.jungle_n0e1se0s0w0nw1"
 0,  0, "t.l2.jungle_n0e0se0s1w0nw1"
 0,  0, "t.l2.jungle_n0e1se0s1w0nw1"
 0,  0, "t.l2.jungle_n0e0se1s0w0nw0"
 0,  0, "t.l2.jungle_n0e1se1s0w0nw0"
 0,  0, "t.l2.jungle_n0e0se1s1w0nw0"
 0,  0, "t.l2.jungle_n0e1se1s1w0nw0"
 0,  0, "t.l2.jungle_n0e0se1s0w0nw1"
 0,  0, "t.l2.jungle_n0e1se1s0w0nw1"
 0,  0, "t.l2.jungle_n0e0se1s1w0nw1"
 0,  0, "t.l2.jungle_n0e1se1s1w0nw1"




;North
 0,  1, "t.l2.jungle_n1e0se0s0w0nw0"
 0,  1, "t.l2.jungle_n1e1se0s0w0nw0"
 0,  1, "t.l2.jungle_n1e0se0s1w0nw0"
 0,  1, "t.l2.jungle_n1e1se0s1w0nw0"
 0,  1, "t.l2.jungle_n1e0se0s0w0nw1"
 0,  1, "t.l2.jungle_n1e1se0s0w0nw1"
 0,  1, "t.l2.jungle_n1e0se0s1w0nw1"
 0,  1, "t.l2.jungle_n1e1se0s1w0nw1"
 0,  1, "t.l2.jungle_n1e0se1s0w0nw0"
 0,  1, "t.l2.jungle_n1e1se1s0w0nw0"
 0,  1, "t.l2.jungle_n1e0se1s1w0nw0"
 0,  1, "t.l2.jungle_n1e1se1s1w0nw0"
 0,  1, "t.l2.jungle_n1e0se1s0w0nw1"
 0,  1, "t.l2.jungle_n1e1se1s0w0nw1"
 0,  1, "t.l2.jungle_n1e0se1s1w0nw1"
 0,  1, "t.l2.jungle_n1e1se1s1w0nw1"


;West
 0,  2, "t.l2.jungle_n0e0se0s0w1nw0"
 0,  2, "t.l2.jungle_n0e1se0s0w1nw0"
 0,  2, "t.l2.jungle_n0e0se0s1w1nw0"
 0,  2, "t.l2.jungle_n0e1se0s1w1nw0"
 0,  2, "t.l2.jungle_n0e0se0s0w1nw1"
 0,  2, "t.l2.jungle_n0e1se0s0w1nw1"
 0,  2, "t.l2.jungle_n0e0se0s1w1nw1"
 0,  2, "t.l2.jungle_n0e1se0s1w1nw1"
 0,  2, "t.l2.jungle_n0e0se1s0w1nw0"
 0,  2, "t.l2.jungle_n0e1se1s0w1nw0"
 0,  2, "t.l2.jungle_n0e0se1s1w1nw0"
 0,  2, "t.l2.jungle_n0e1se1s1w1nw0"
 0,  2, "t.l2.jungle_n0e0se1s0w1nw1"
 0,  2, "t.l2.jungle_n0e1se1s0w1nw1"
 0,  2, "t.l2.jungle_n0e0se1s1w1nw1"
 0,  2, "t.l2.jungle_n0e1se1s1w1nw1"


;NorthWest
 0,  3, "t.l2.jungle_n1e0se0s0w1nw0"
 0,  3, "t.l2.jungle_n1e1se0s0w1nw0"
 0,  3, "t.l2.jungle_n1e0se0s1w1nw0"
 0,  3, "t.l2.jungle_n1e1se0s1w1nw0"
 0,  3, "t.l2.jungle_n1e0se0s0w1nw1"
 0,  3, "t.l2.jungle_n1e1se0s0w1nw1"
 0,  3, "t.l2.jungle_n1e0se0s1w1nw1"
 0,  3, "t.l2.jungle_n1e1se0s1w1nw1"
 0,  3, "t.l2.jungle_n1e0se1s0w1nw0"
 0,  3, "t.l2.jungle_n1e1se1s0w1nw0"
 0,  3, "t.l2.jungle_n1e0se1s1w1nw0"
 0,  3, "t.l2.jungle_n1e1se1s1w1nw0"
 0,  3, "t.l2.jungle_n1e0se1s0w1nw1"
 0,  3, "t.l2.jungle_n1e1se1s0w1nw1"
 0,  3, "t.l2.jungle_n1e0se1s1w1nw1"
 0,  3, "t.l2.jungle_n1e1se1s1w1nw1"

;forests as overlay

;Center
 1,  0, "t.l2.forest_n0e0se0s0w0nw0"
 1,  0, "t.l2.forest_n0e1se0s0w0nw0"
 1,  0, "t.l2.forest_n0e0se0s1w0nw0"
 1,  0, "t.l2.forest_n0e1se0s1w0nw0"
 1,  0, "t.l2.forest_n0e0se0s0w0nw1"
 1,  0, "t.l2.forest_n0e1se0s0w0nw1"
 1,  0, "t.l2.forest_n0e0se0s1w0nw1"
 1,  0, "t.l2.forest_n0e1se0s1w0nw1"
 1,  0, "t.l2.forest_n0e0se1s0w0nw0"
 1,  0, "t.l2.forest_n0e1se1s0w0nw0"
 1,  0, "t.l2.forest_n0e0se1s1w0nw0"
 1,  0, "t.l2.forest_n0e1se1s1w0nw0"
 1,  0, "t.l2.forest_n0e0se1s0w0nw1"
 1,  0, "t.l2.forest_n0e1se1s0w0nw1"
 1,  0, "t.l2.forest_n0e0se1s1w0nw1"
 1,  0, "t.l2.forest_n0e1se1s1w0nw1"


;North
 1,  1, "t.l2.forest_n1e0se0s0w0nw0"
 1,  1, "t.l2.forest_n1e1se0s0w0nw0"
 1,  1, "t.l2.forest_n1e0se0s1w0nw0"
 1,  1, "t.l2.forest_n1e1se0s1w0nw0"
 1,  1, "t.l2.forest_n1e0se0s0w0nw1"
 1,  1, "t.l2.forest_n1e1se0s0w0nw1"
 1,  1, "t.l2.forest_n1e0se0s1w0nw1"
 1,  1, "t.l2.forest_n1e1se0s1w0nw1"
 1,  1, "t.l2.forest_n1e0se1s0w0nw0"
 1,  1, "t.l2.forest_n1e1se1s0w0nw0"
 1,  1, "t.l2.forest_n1e0se1s1w0nw0"
 1,  1, "t.l2.forest_n1e1se1s1w0nw0"
 1,  1, "t.l2.forest_n1e0se1s0w0nw1"
 1,  1, "t.l2.forest_n1e1se1s0w0nw1"
 1,  1, "t.l2.forest_n1e0se1s1w0nw1"
 1,  1, "t.l2.forest_n1e1se1s1w0nw1"

;West
 1,  2, "t.l2.forest_n0e0se0s0w1nw0"
 1,  2, "t.l2.forest_n0e1se0s0w1nw0"
 1,  2, "t.l2.forest_n0e0se0s1w1nw0"
 1,  2, "t.l2.forest_n0e1se0s1w1nw0"
 1,  2, "t.l2.forest_n0e0se0s0w1nw1"
 1,  2, "t.l2.forest_n0e1se0s0w1nw1"
 1,  2, "t.l2.forest_n0e0se0s1w1nw1"
 1,  2, "t.l2.forest_n0e1se0s1w1nw1"
 1,  2, "t.l2.forest_n0e0se1s0w1nw0"
 1,  2, "t.l2.forest_n0e1se1s0w1nw0"
 1,  2, "t.l2.forest_n0e0se1s1w1nw0"
 1,  2, "t.l2.forest_n0e1se1s1w1nw0"
 1,  2, "t.l2.forest_n0e0se1s0w1nw1"
 1,  2, "t.l2.forest_n0e1se1s0w1nw1"
 1,  2, "t.l2.forest_n0e0se1s1w1nw1"
 1,  2, "t.l2.forest_n0e1se1s1w1nw1"



;NorthWest
 1,  3, "t.l2.forest_n1e0se0s0w1nw0"
 1,  3, "t.l2.forest_n1e1se0s0w1nw0"
 1,  3, "t.l2.forest_n1e0se0s1w1nw0"
 1,  3, "t.l2.forest_n1e1se0s1w1nw0"
 1,  3, "t.l2.forest_n1e0se0s0w1nw1"
 1,  3, "t.l2.forest_n1e1se0s0w1nw1"
 1,  3, "t.l2.forest_n1e0se0s1w1nw1"
 1,  3, "t.l2.forest_n1e1se0s1w1nw1"
 1,  3, "t.l2.forest_n1e0se1s0w1nw0"
 1,  3, "t.l2.forest_n1e1se1s0w1nw0"
 1,  3, "t.l2.forest_n1e0se1s1w1nw0"
 1,  3, "t.l2.forest_n1e1se1s1w1nw0"
 1,  3, "t.l2.forest_n1e0se1s0w1nw1"
 1,  3, "t.l2.forest_n1e1se1s0w1nw1"
 1,  3, "t.l2.forest_n1e0se1s1w1nw1"
 1,  3, "t.l2.forest_n1e1se1s1w1nw1"

;hills as overlay

 2,  0, "t.l2.hills_n0e0se0s0w0nw0"
 2,  1, "t.l2.hills_n1e0se0s0w0nw0"
 2,  2, "t.l2.hills_n0e1se0s0w0nw0"
 2,  3, "t.l2.hills_n1e1se0s0w0nw0"
 3,  0, "t.l2.hills_n0e0se0s1w0nw0"
 3,  1, "t.l2.hills_n1e0se0s1w0nw0"
 3,  2, "t.l2.hills_n0e1se0s1w0nw0"
 3,  3, "t.l2.hills_n1e1se0s1w0nw0"
 4,  0, "t.l2.hills_n0e0se0s0w1nw0"
 4,  1, "t.l2.hills_n1e0se0s0w1nw0"
 4,  2, "t.l2.hills_n0e1se0s0w1nw0"
 4,  3, "t.l2.hills_n1e1se0s0w1nw0"
 5,  0, "t.l2.hills_n0e0se0s1w1nw0"
 5,  1, "t.l2.hills_n1e0se0s1w1nw0"
 5,  2, "t.l2.hills_n0e1se0s1w1nw0"
 5,  3, "t.l2.hills_n1e1se0s1w1nw0"

 ; The below sprites are duplicates of the previous sprites,
 ; since there aren't graphics for the extra hex directions.
 2,  0, "t.l2.hills_n0e0se0s0w0nw1"
 2,  1, "t.l2.hills_n1e0se0s0w0nw1"
 2,  2, "t.l2.hills_n0e1se0s0w0nw1"
 2,  3, "t.l2.hills_n1e1se0s0w0nw1"
 3,  0, "t.l2.hills_n0e0se0s1w0nw1"
 3,  1, "t.l2.hills_n1e0se0s1w0nw1"
 3,  2, "t.l2.hills_n0e1se0s1w0nw1"
 3,  3, "t.l2.hills_n1e1se0s1w0nw1"
 4,  0, "t.l2.hills_n0e0se0s0w1nw1"
 4,  1, "t.l2.hills_n1e0se0s0w1nw1"
 4,  2, "t.l2.hills_n0e1se0s0w1nw1"
 4,  3, "t.l2.hills_n1e1se0s0w1nw1"
 5,  0, "t.l2.hills_n0e0se0s1w1nw1"
 5,  1, "t.l2.hills_n1e0se0s1w1nw1"
 5,  2, "t.l2.hills_n0e1se0s1w1nw1"
 5,  3, "t.l2.hills_n1e1se0s1w1nw1"
 2,  0, "t.l2.hills_n0e0se1s0w0nw0"
 2,  1, "t.l2.hills_n1e0se1s0w0nw0"
 2,  2, "t.l2.hills_n0e1se1s0w0nw0"
 2,  3, "t.l2.hills_n1e1se1s0w0nw0"
 3,  0, "t.l2.hills_n0e0se1s1w0nw0"
 3,  1, "t.l2.hills_n1e0se1s1w0nw0"
 3,  2, "t.l2.hills_n0e1se1s1w0nw0"
 3,  3, "t.l2.hills_n1e1se1s1w0nw0"
 4,  0, "t.l2.hills_n0e0se1s0w1nw0"
 4,  1, "t.l2.hills_n1e0se1s0w1nw0"
 4,  2, "t.l2.hills_n0e1se1s0w1nw0"
 4,  3, "t.l2.hills_n1e1se1s0w1nw0"
 5,  0, "t.l2.hills_n0e0se1s1w1nw0"
 5,  1, "t.l2.hills_n1e0se1s1w1nw0"
 5,  2, "t.l2.hills_n0e1se1s1w1nw0"
 5,  3, "t.l2.hills_n1e1se1s1w1nw0"
 2,  0, "t.l2.hills_n0e0se1s0w0nw1"
 2,  1, "t.l2.hills_n1e0se1s0w0nw1"
 2,  2, "t.l2.hills_n0e1se1s0w0nw1"
 2,  3, "t.l2.hills_n1e1se1s0w0nw1"
 3,  0, "t.l2.hills_n0e0se1s1w0nw1"
 3,  1, "t.l2.hills_n1e0se1s1w0nw1"
 3,  2, "t.l2.hills_n0e1se1s1w0nw1"
 3,  3, "t.l2.hills_n1e1se1s1w0nw1"
 4,  0, "t.l2.hills_n0e0se1s0w1nw1"
 4,  1, "t.l2.hills_n1e0se1s0w1nw1"
 4,  2, "t.l2.hills_n0e1se1s0w1nw1"
 4,  3, "t.l2.hills_n1e1se1s0w1nw1"
 5,  0, "t.l2.hills_n0e0se1s1w1nw1"
 5,  1, "t.l2.hills_n1e0se1s1w1nw1"
 5,  2, "t.l2.hills_n0e1se1s1w1nw1"
 5,  3, "t.l2.hills_n1e1se1s1w1nw1"

;mountains as overlay

 6,  0, "t.l2.mountains_n0e0se0s0w0nw0"
 6,  1, "t.l2.mountains_n1e0se0s0w0nw0"
 6,  2, "t.l2.mountains_n0e1se0s0w0nw0"
 6,  3, "t.l2.mountains_n1e1se0s0w0nw0"
 7,  0, "t.l2.mountains_n0e0se0s1w0nw0"
 7,  1, "t.l2.mountains_n1e0se0s1w0nw0"
 7,  2, "t.l2.mountains_n0e1se0s1w0nw0"
 7,  3, "t.l2.mountains_n1e1se0s1w0nw0"
 8,  0, "t.l2.mountains_n0e0se0s0w1nw0"
 8,  1, "t.l2.mountains_n1e0se0s0w1nw0"
 8,  2, "t.l2.mountains_n0e1se0s0w1nw0"
 8,  3, "t.l2.mountains_n1e1se0s0w1nw0"
 9,  0, "t.l2.mountains_n0e0se0s1w1nw0"
 9,  1, "t.l2.mountains_n1e0se0s1w1nw0"
 9,  2, "t.l2.mountains_n0e1se0s1w1nw0"
 9,  3, "t.l2.mountains_n1e1se0s1w1nw0"

 ; The below sprites are duplicates of the previous sprites,
 ; since there aren't graphics for the extra hex directions.
 6,  0, "t.l2.mountains_n0e0se0s0w0nw1"
 6,  1, "t.l2.mountains_n1e0se0s0w0nw1"
 6,  2, "t.l2.mountains_n0e1se0s0w0nw1"
 6,  3, "t.l2.mountains_n1e1se0s0w0nw1"
 7,  0, "t.l2.mountains_n0e0se0s1w0nw1"
 7,  1, "t.l2.mountains_n1e0se0s1w0nw1"
 7,  2, "t.l2.mountains_n0e1se0s1w0nw1"
 7,  3, "t.l2.mountains_n1e1se0s1w0nw1"
 8,  0, "t.l2.mountains_n0e0se0s0w1nw1"
 8,  1, "t.l2.mountains_n1e0se0s0w1nw1"
 8,  2, "t.l2.mountains_n0e1se0s0w1nw1"
 8,  3, "t.l2.mountains_n1e1se0s0w1nw1"
 9,  0, "t.l2.mountains_n0e0se0s1w1nw1"
 9,  1, "t.l2.mountains_n1e0se0s1w1nw1"
 9,  2, "t.l2.mountains_n0e1se0s1w1nw1"
 9,  3, "t.l2.mountains_n1e1se0s1w1nw1"
 6,  0, "t.l2.mountains_n0e0se1s0w0nw0"
 6,  1, "t.l2.mountains_n1e0se1s0w0nw0"
 6,  2, "t.l2.mountains_n0e1se1s0w0nw0"
 6,  3, "t.l2.mountains_n1e1se1s0w0nw0"
 7,  0, "t.l2.mountains_n0e0se1s1w0nw0"
 7,  1, "t.l2.mountains_n1e0se1s1w0nw0"
 7,  2, "t.l2.mountains_n0e1se1s1w0nw0"
 7,  3, "t.l2.mountains_n1e1se1s1w0nw0"
 8,  0, "t.l2.mountains_n0e0se1s0w1nw0"
 8,  1, "t.l2.mountains_n1e0se1s0w1nw0"
 8,  2, "t.l2.mountains_n0e1se1s0w1nw0"
 8,  3, "t.l2.mountains_n1e1se1s0w1nw0"
 9,  0, "t.l2.mountains_n0e0se1s1w1nw0"
 9,  1, "t.l2.mountains_n1e0se1s1w1nw0"
 9,  2, "t.l2.mountains_n0e1se1s1w1nw0"
 9,  3, "t.l2.mountains_n1e1se1s1w1nw0"
 6,  0, "t.l2.mountains_n0e0se1s0w0nw1"
 6,  1, "t.l2.mountains_n1e0se1s0w0nw1"
 6,  2, "t.l2.mountains_n0e1se1s0w0nw1"
 6,  3, "t.l2.mountains_n1e1se1s0w0nw1"
 7,  0, "t.l2.mountains_n0e0se1s1w0nw1"
 7,  1, "t.l2.mountains_n1e0se1s1w0nw1"
 7,  2, "t.l2.mountains_n0e1se1s1w0nw1"
 7,  3, "t.l2.mountains_n1e1se1s1w0nw1"
 8,  0, "t.l2.mountains_n0e0se1s0w1nw1"
 8,  1, "t.l2.mountains_n1e0se1s0w1nw1"
 8,  2, "t.l2.mountains_n0e1se1s0w1nw1"
 8,  3, "t.l2.mountains_n1e1se1s0w1nw1"
 9,  0, "t.l2.mountains_n0e0se1s1w1nw1"
 9,  1, "t.l2.mountains_n1e0se1s1w1nw1"
 9,  2, "t.l2.mountains_n0e1se1s1w1nw1"
 9,  3, "t.l2.mountains_n1e1se1s1w1nw1"


}
