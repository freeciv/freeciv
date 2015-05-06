;
; The names for city tiles are not free and must follow the following rules.
; The names consists of 'style name' + '_' + 'index'. The style name is as
; specified in cities.ruleset file and the index only defines the read order
; of the images. The definitions are read starting with index 0 till the first
; missing value The index is checked against the city bonus of effect
; EFT_CITY_IMG and the resulting image is used to draw the city on the tile.
;
; Obviously the first tile must be 'style_name'_city_0 and the sizes must be
; in ascending order. There must also be a 'style_name'_wall_0 tile used to
; draw the wall and an occupied tile to indicate a military units in a city.
; The maximum number of images is only limited by the maximum size of a city
; (currently MAX_CITY_SIZE = 250). The constant is defined in common/city.h.
;

[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2015-Mar-25"

[info]

artists = "
    Jerzy Klek <jekl@altavista.net>
    european style based on trident tileset by
    Tatu Rissanen <tatu.rissanen@hut.fi>
    Eleazar (buoy)
    Vincent Croisier <vincent.croisier@advalvas.be> (ruins)
    GriffonSpade
"

[file]
gfx = "hexemplio/bases"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 126
dy = 128
pixel_border = 1

tiles = { "row", "column", "tag"

; 2,  3, "cd.occupied"
; 2,  3, "cd.city"
; 2,  3, "cd.city_wall"

; used by all city styles

 0,  0, "base.airstrip_mg"
 1,  0, "tx.airstrip_full"

 0,  1, "base.airbase_mg"
 1,  1, "tx.airbase_full"

 1,  2, "base.outpost_fg"
 0,  2, "base.outpost_bg"

 1,  3, "base.fortress_fg"
 0,  3, "base.fortress_bg"

; 2,  0, "city.disorder"
 2,  1, "base.buoy_mg"
 2,  2, "base.ruins_mg"
; 2,  3, "city.european_occupied_0"
; 2,  3, "city.classical_occupied_0"
; 2,  3, "city.asian_occupied_0"
; 2,  3, "city.tropical_occupied_0"
; 2,  3, "city.celtic_occupied_0"
; 2,  3, "city.babylonian_occupied_0"
; 2,  3, "city.industrial_occupied_0"
; 2,  3, "city.electricage_occupied_0"
; 2,  3, "city.modern_occupied_0"
; 2,  3, "city.postmodern_occupied_0"
}
