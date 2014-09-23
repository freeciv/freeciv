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
options = "+Freeciv-spec-Devel-2013.Feb.13"

[info]

artists = "
    Jerzy Klek <jekl@altavista.net>

    european style based on trident tileset by
    Tatu Rissanen <tatu.rissanen@hut.fi>
    Marco Saupe <msaupe@saale-net.de> (reworked classic, industrial and modern)
    Eleazar (buoy)
    Vincent Croisier <vincent.croisier@advalvas.be> (ruins)
"

[file]
gfx = "hexemplio/cities"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 128
dy = 128
pixel_border = 1

tiles = { "row", "column", "tag"

 0,  7, "cd.occupied"
 0,  6, "cd.city"
 0,  3, "cd.city_wall"

; used by all city styles

 0,  0, "city.disorder"
 0,  1, "base.airbase_mg"
 0,  2, "tx.airbase_full"
 0,  3, "base.fortress_fg"
 0,  4, "base.fortress_bg"
 0,  5, "base.buoy_mg"
 0,  6, "base.ruins_mg"

 0,  7, "city.european_occupied_0"
 0,  7, "city.classical_occupied_0"
 0,  7, "city.asian_occupied_0"
 0,  7, "city.tropical_occupied_0"
 0,  7, "city.celtic_occupied_0"
 0,  7, "city.babylonian_occupied_0"

 0,  7, "city.industrial_occupied_0"
 0,  7, "city.electricage_occupied_0"
 0,  7, "city.modern_occupied_0"
 0,  7, "city.postmodern_occupied_0"


 1,  0, "city.celtic_city_0"
 1,  1, "city.celtic_city_1"
 1,  2, "city.celtic_city_2"
 1,  3, "city.celtic_city_3"
 1,  4, "city.celtic_city_4"

 1,  5, "city.celtic_wall_0"
 1,  6, "city.celtic_wall_1"
 1,  7, "city.celtic_wall_2"
 1,  8, "city.celtic_wall_3"
 1,  9, "city.celtic_wall_4"

 2,  0, "city.european_city_0"
 2,  1, "city.european_city_1"
 2,  2, "city.european_city_2"
 2,  3, "city.european_city_3"
 2,  4, "city.european_city_4"

 2,  5, "city.european_wall_0"
 2,  6, "city.european_wall_1"
 2,  7, "city.european_wall_2"
 2,  8, "city.european_wall_3"
 2,  9, "city.european_wall_4"

 3,  0, "city.classical_city_0"
 3,  1, "city.classical_city_1"
 3,  2, "city.classical_city_2"
 3,  3, "city.classical_city_3"
 3,  4, "city.classical_city_4"

 3,  5, "city.classical_wall_0"
 3,  6, "city.classical_wall_1"
 3,  7, "city.classical_wall_2"
 3,  8, "city.classical_wall_3"
 3,  9, "city.classical_wall_4"

 4,  0, "city.babylonian_city_0"
 4,  1, "city.babylonian_city_1"
 4,  2, "city.babylonian_city_2"
 4,  3, "city.babylonian_city_3"
 4,  4, "city.babylonian_city_4"

 4,  5, "city.babylonian_wall_0"
 4,  6, "city.babylonian_wall_1"
 4,  7, "city.babylonian_wall_2"
 4,  8, "city.babylonian_wall_3"
 4,  9, "city.babylonian_wall_4"

 5,  0, "city.asian_city_0"
 5,  1, "city.asian_city_1"
 5,  2, "city.asian_city_2"
 5,  3, "city.asian_city_3"
 5,  4, "city.asian_city_4"

 5,  5, "city.asian_wall_0"
 5,  6, "city.asian_wall_1"
 5,  7, "city.asian_wall_2"
 5,  8, "city.asian_wall_3"
 5,  9, "city.asian_wall_4"

 6,  0, "city.tropical_city_0"
 6,  1, "city.tropical_city_1"
 6,  2, "city.tropical_city_2"
 6,  3, "city.tropical_city_3"
 6,  4, "city.tropical_city_4"

 6,  5, "city.tropical_wall_0"
 6,  6, "city.tropical_wall_1"
 6,  7, "city.tropical_wall_2"
 6,  8, "city.tropical_wall_3"
 6,  9, "city.tropical_wall_4"

 7,  0, "city.industrial_city_0"
 7,  1, "city.industrial_city_1"
 7,  2, "city.industrial_city_2"
 7,  1, "city.industrial_city_3"
 7,  2, "city.industrial_city_4"

 7,  5, "city.industrial_wall_0"
 7,  6, "city.industrial_wall_1"
 7,  7, "city.industrial_wall_2"
 7,  8, "city.industrial_wall_3"
 7,  9, "city.industrial_wall_4"

 8,  0, "city.electricage_city_0"
 8,  1, "city.electricage_city_1"
 8,  2, "city.electricage_city_2"
 8,  3, "city.electricage_city_3"
 8,  4, "city.electricage_city_4"

 8,  5, "city.electricage_wall_0"
 8,  6, "city.electricage_wall_1"
 8,  7, "city.electricage_wall_2"
 8,  8, "city.electricage_wall_3"
 8,  9, "city.electricage_wall_4"

 9,  0, "city.modern_city_0"
 9,  1, "city.modern_city_1"
 9,  2, "city.modern_city_2"
 9,  3, "city.modern_city_3"
 9,  4, "city.modern_city_4"

 9,  5, "city.modern_wall_0"
 9,  6, "city.modern_wall_1"
 9,  7, "city.modern_wall_2"
 9,  8, "city.modern_wall_3"
 9,  9, "city.modern_wall_4"

 10,  0, "city.postmodern_city_0"
 10,  1, "city.postmodern_city_1"
 10,  2, "city.postmodern_city_2"
 10,  3, "city.postmodern_city_3"
 10,  4, "city.postmodern_city_4"

 10,  5, "city.postmodern_wall_0"
 10,  6, "city.postmodern_wall_1"
 10,  7, "city.postmodern_wall_2"
 10,  8, "city.postmodern_wall_3"
 10,  9, "city.postmodern_wall_4"

}
