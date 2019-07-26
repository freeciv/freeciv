
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2019-Jul-03"

[info]

artists = "
    Alexandre Beraud <a_beraud@lemel.fr>
"

[file]
gfx = "misc/space"

[grid_main]

; Could use separate "grids" for the smaller components, but not
; bothering for now.

x_top_left = 0
y_top_left = 0
dx = 60
dy = 60

tiles = { "row", "column", "tag"

  0,  0, "spaceship.solar_panels"
  0,  1, "spaceship.life_support"
  0,  2, "spaceship.habitation"
  0,  3, "spaceship.structural"
  0,  4, "spaceship.fuel"
  0,  5, "spaceship.propulsion"
  0,  6, "spaceship.exhaust"
}
