
[spec]

; Format and options of this spec file:
options = "+spec2"

[info]

artists = "
    Alexandre Beraud <a_beraud@lemel.fr>
    Jeff Mallatt <jjm@codewell.com> (cooling flakes)
    Davide Pagnin <nightmare@freeciv.it> (angry citizens)
    Enrico Bini <e.bini@sssup.it> (fundamentalism icon)
"

[file]
gfx = "misc/small"

[grid_main]

x_top_left = 0
y_top_left = 0
dx = 15
dy = 20

tiles = { "row", "column", "tag"

; Science progress indicators:

  0,  0, "s.science_bulb_0"
  0,  1, "s.science_bulb_1"
  0,  2, "s.science_bulb_2"
  0,  3, "s.science_bulb_3"
  0,  4, "s.science_bulb_4"
  0,  5, "s.science_bulb_5"
  0,  6, "s.science_bulb_6"
  0,  7, "s.science_bulb_7"

; Government icons: (see further below for fundamentalism)

  0,  8, "gov.anarchy"
  0,  9, "gov.despotism"
  0, 10, "gov.monarchy"
  0, 11, "gov.communism"
  0, 12, "gov.fundamentalism"
  0, 13, "gov.republic"
  0, 14, "gov.democracy"

; Global warming progress indicators:

  0, 15, "s.warming_sun_0"
  0, 16, "s.warming_sun_1"
  0, 17, "s.warming_sun_2"
  0, 18, "s.warming_sun_3"
  0, 19, "s.warming_sun_4"
  0, 20, "s.warming_sun_5"
  0, 21, "s.warming_sun_6"
  0, 22, "s.warming_sun_7"

; Nuclear winter progress indicators:

  0, 34, "s.cooling_flake_0"
  0, 35, "s.cooling_flake_1"
  0, 36, "s.cooling_flake_2"
  0, 37, "s.cooling_flake_3"
  0, 38, "s.cooling_flake_4"
  0, 39, "s.cooling_flake_5"
  0, 40, "s.cooling_flake_6"
  0, 41, "s.cooling_flake_7"

; Citizen icons:

  0, 23, "citizen.entertainer"
  0, 24, "citizen.scientist"
  0, 25, "citizen.tax_collector"
  0, 26, "citizen.content_0"
  0, 27, "citizen.content_1"
  0, 28, "citizen.happy_0"
  0, 29, "citizen.happy_1"
  0, 30, "citizen.unhappy_0" 
  0, 31, "citizen.unhappy_1" 
  0, 32, "citizen.angry_0" 
  0, 33, "citizen.angry_1" 

; Right arrow icon:

  0, 42, "s.right_arrow"
}
