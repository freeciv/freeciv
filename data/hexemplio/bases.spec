;
; The names for city tiles are not free and must follow the following rules.
; The names consists of 'style name' + '_' + 'index'. The style name is as
; specified in cities.ruleset file and the index only defines the read order
; of the images. The definitions are read starting with index 0 till the first
; missing value The index is checked against the city bonus of effect
; 'City_Image' and the resulting image is used to draw the city on the tile.
;
; Obviously the first tile must be 'style_name'_city_0 and the sizes must be
; in ascending order. There must also be a 'style_name'_wall_0 tile used
; for the default wall graphics and an occupied tile to indicate
; a military units in a city.
; For providing multiple walls buildings (as requested by the "Visible_Walls"
; effect value) tags are 'style_name'_bldg_'effect_value'_'index'.
; The maximum number of images is only limited by the maximum size of a city
; (currently MAX_CITY_SIZE = 255).
;
; For providing custom citizen icons for the city style, use tags of the form
; 'citizen.<tag>.<citizen_type>_<index>'
; where <tag> is citizens_graphic tag from the styles.ruleset,
; <citizen_type> is type like 'content', same ones as
; misc/small.spec has for the default citizen icons, and
; <index> is a running number for alternative sprites.
;

[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-3.3-Devel-2023.Apr.05"

[info]

artists = "
    Hogne Håskjold <hogne@freeciv.org>[HH]
    Eleazar [El](buoy)
    Anton Ecker (Kaldred) (ruins)
    GriffonSpade [GS]
"

[file]
gfx = "hexemplio/bases"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 126
dy = 96
pixel_border = 1

tiles = { "row", "column", "tag"

;[HH][GS]
 0,  0, "base.airstrip_mg:0"
 1,  0, "tx.airstrip_full"
;[HH][GS]
 0,  1, "base.airbase_mg:0"
 1,  1, "tx.airbase_full"
;[HH][GS]
 1,  2, "base.outpost_fg:0"
 0,  2, "base.outpost_bg:0"
;[HH]
 1,  3, "base.fortress_fg:0"
 0,  3, "base.fortress_bg:0"
;[HH]
 0,  4, "city.disorder"
;[El]
 1,  4, "base.buoy_mg:0"
;[VC]
 0,  5, "extra.ruins_mg:0"
;[HH]
 1,  5, "city.european_occupied_0"
 1,  5, "city.classical_occupied_0"
 1,  5, "city.asian_occupied_0"
 1,  5, "city.tropical_occupied_0"
 1,  5, "city.celtic_occupied_0"
 1,  5, "city.babylonian_occupied_0"
 1,  5, "city.industrial_occupied_0"
 1,  5, "city.electricage_occupied_0"
 1,  5, "city.modern_occupied_0"
 1,  5, "city.postmodern_occupied_0"
}
