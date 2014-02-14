
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2013.Feb.13"

[info]

artists = "
    GriffonSpade
"

[file]
gfx = "misc/events"

[grid_main]

x_top_left = 0
y_top_left = 0
dx = 15
dy = 20

tiles = { "row", "column", "tag"
  0,  0, "e_city_cantbuild"
  0,  1, "e_city_lost"
  0,  2, "e_city_love"
  0,  3, "e_city_famine"
  0,  4, "e_city_famine_feared"
  0,  5, "e_city_disorder"
  0,  6, "e_city_growth"
  0,  7, "e_city_may_soon_grow"
  0,  8, "e_city_transfer"
  0,  9, "e_city_aqueduct"
  0, 10, "e_city_aq_building"
  0, 11, "e_city_normal"
  0, 12, "e_city_nuked"
  0, 13, "e_city_cma_release"
  0, 14, "e_city_gran_throttle"
  0, 15, "e_city_build"
; city poisoned?
  0, 17, "e_city_production_changed"
}
