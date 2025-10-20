
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-3.3-Devel-2023.Apr.05"

[info]

artists = "
    XYZ
    GriffonSpade
    VladimirSlavik
    ngunjaca
    omero (operative)
"

[file]
gfx = "amplio2/extra_units"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 64
dy = 48
pixel_border = 1

tiles = { "row", "column", "tag"
  0,  0, "u.gladiators_Idle:0"
  0,  1, "u.arbalests_Idle:0"
  0,  2, "u.halberdiers_Idle:0"
  0,  3, "u.samurai_Idle:0"
  0,  4, "u.militia_Idle:0"
  0,  5, "u.line_infantry_Idle:0"
  0,  6, "u.grenadier_Idle:0"
  0,  7, "u.fusiliers_Idle:0"
  0,  8, "u.musket_militia_Idle:0"
  0,  9, "u.marksman_Idle:0"

  1,  0, "u.stormtroopers_Idle:0"
  1,  1, "u.commando_Idle:0"
  1,  7, "u.motorized_infantry_Idle:0"
  1,  8, "u.tachanka_Idle:0"
  1,  9, "u.mobile_fanatics_Idle:0"

  2,  0, "u.dromedari_Idle:0"
  2,  1, "u.mounted_archers_Idle:0"
  2,  2, "u.mounted_samurai_Idle:0"
  2,  3, "u.uhlan_Idle:0"
  2,  4, "u.mounted_militia_Idle:0"
  2,  5, "u.mounted_marksman_Idle:0"

  3,  0, "u.early_light_tank_Idle:0", "u.mark_iv_Idle:0"
  3,  1, "u.early_tank_Idle:0", "u.whippet_Idle:0"
  3,  2, "u.early_heavy_tank_Idle:0", "u.renault_ft_Idle:0"
  3,  3, "u.tank_Idle:0", "u.sherman_Idle:0"
  3,  4, "u.heavy_tank_Idle:0", "u.leopard_Idle:0"
  3,  5, "u.reactive_armor_Idle:0", "u.abrahams_Idle:0"
  3,  6, "u.armored_car_Idle:0", "u.rolls_royce_armored_car_Idle:0"
  3,  7, "u.armored_recon_vehicle_Idle:0", "u.rooikat_Idle:0"

  4,  0, "u.trebuchet_Idle:0"
  4,  1, "u.cannon_alternative_Idle:0"
  4,  2, "u.longrange_howitzer_Idle:0", "u.panzerhaubitze2000_Idle:0"
  4,  3, "u.ballista_Idle:0"
  4,  4, "u.siege_tower_Idle:0"
  4,  5, "u.battering_ram_Idle:0"

  5,  0, "u.biplane_Idle:0"
  5,  1, "u.gliderplane_Idle:0", "u.gotha_Idle:0"
  5,  2, "u.ekranoplan_Idle:0"
  5,  3, "u.divebomber_Idle:0"
  5,  4, "u.transportplane_Idle:0", "u.c47_Idle:0"
  5,  5, "u.jetfighter_Idle:0", "u.mig_Idle:0"
  5,  6, "u.jetbomber_Idle:0"
  5,  7, "u.supersonic_fighter_Idle:0"
  5,  8, "u.transportjet_Idle:0"
  5,  9, "u.strikeplane_Idle:0", "u.a10_warthog_Idle:0"
  5, 10, "u.strikejet_Idle:0", "u.f4_Idle:0"
  5, 12, "u.balloon_Idle:0"
  5, 13, "u.zeppelin_Idle:0"

  6,  0, "u.light_helicopter_Idle:0", "u.alouette_iii_Idle:0"
  6,  1, "u.transport_helicopter_Idle:0", "u.chinook_Idle:0"
  6,  2, "u.attack_transport_helicopter_Idle:0", "u.mi24_Idle:0"
  6,  4, "u.drone_Idle:0"
  6,  6, "u.v2_rocket_Idle:0"

  7,  0, "u.quinquireme_Idle:0"
  7,  1, "u.longboat_Idle:0"
  7,  2, "u.junk_Idle:0"
  7,  3, "u.cog_Idle:0"
  7,  4, "u.galleon_alternative_Idle:0"
  7,  5, "u.frigate_alternative_Idle:0"
  7,  6, "u.flagship_Idle:0"
  7,  7, "u.paddle_steamer_Idle:0"
  7,  8, "u.patrol_boat_Idle:0"
  7,  9, "u.feluca_Idle:0"
  7, 10, "u.container_ship_Idle:0"

  8,  0, "u.dredger_Idle:0"
  8,  1, "u.icebreaker_Idle:0"
  8,  2, "u.minesweeper_Idle:0"
  8,  4, "u.rowboat_Idle:0"
  8,  6, "u.early_submarine_Idle:0"

  9,  0, "u.slave_Idle:0"
  9,  1, "u.emissary_Idle:0"
  9,  2, "u.barbarian_leader_alternative_Idle:0"
  9,  3, "u.early_explorer_Idle:0"
  9,  4, "u.operative_Idle:0"
  9,  5, "u.armoured_train_Idle:0"
  9,  6, "u.rail_artillery_Idle:0"

 10,  0, "u.wolves_Idle:0"
 10,  1, "u.panther_Idle:0"
 10,  2, "u.tiger_Idle:0"
 10,  3, "u.lion_Idle:0"
 10,  4, "u.bear_Idle:0"
 10,  5, "u.snake_Idle:0"
 10,  6, "u.alligator_Idle:0"
 10,  7, "u.orangutan_Idle:0"
 10,  8, "u.hippopotamus_Idle:0"
 10,  9, "u.rhinoceros_Idle:0"
 10, 10, "u.elephant_Idle:0"
 10, 11, "u.polar_bear_Idle:0"
 10, 12, "u.squid_Idle:0"

 10, 14, "u.mammoth_Idle:0"
}
