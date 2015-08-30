
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2015-Mar-25"

[info]

artists = "
    GriffonSpade
    Hogne Håskjold <haskjold@gmail.com> (gold coin)
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
  0,  3, "e_city_disorder"
  0,  4, "e_city_famine"
  0,  5, "e_city_famine_feared"
  0,  6, "e_city_growth"
  0,  7, "e_city_may_soon_grow"
  0,  8, "e_city_aqueduct"
  0,  9, "e_city_aq_building"
  0, 10, "e_city_normal"
  0, 11, "e_city_nuked"
  0, 12, "e_city_cma_release"
  0, 13, "e_city_gran_throttle"
  0, 14, "e_city_transfer"
  0, 15, "e_city_build"
  0, 16, "e_city_production_changed"
  0, 17, "e_disaster"
  0, 18, "e_city_plague"
  0, 12, "e_worklist" ; (0, 19) unused
  0, 12, "e_city_radius_sq" ; (0, 20) unused

  1,  0, "e_nuke"
;  1,  1, "e_chem"
;  1,  2, "e_warn"
  1,  3, "e_global_eco"
;  1,  4, "e_no"

  1,  2, "e_ai_debug"
  1,  2, "e_script"
  1,  2, "e_broadcast_report"
  1,  2, "e_report"
  1,  2, "e_chat_msg"
  1,  2, "e_log_error"
  1,  3, "e_log_fatal"
  1,  3, "e_chat_error"

  2,  0, "e_hut_barb"
  2,  1, "e_hut_city"
  2,  2, "e_hut_gold"
  2,  3, "e_hut_barb_killed"
  2,  4, "e_hut_merc"
  2,  5, "e_hut_settler"
  2,  6, "e_hut_tech"
  2,  7, "e_hut_barb_city_near"

  3,  0, "e_my_diplomat_poison"
  3,  1, "e_my_diplomat_bribe"
  3,  2, "e_my_diplomat_incite"
  3,  3, "e_my_diplomat_embassy"
  3,  4, "e_my_diplomat_failed"
  3,  5, "e_my_diplomat_sabotage"
  3,  6, "e_my_diplomat_theft"
  3,  1, "e_my_spy_steal_gold"
  3,  8, "e_my_spy_steal_map" ; use base sprite for my diplomats
  3,  8, "e_my_spy_nuke" ; use base sprite for my diplomats
  3,  7, "e_diplomatic_incident"
  3,  8, "e_my_diplomat_escape" ; base sprite for my diplomats
  3,  9, "e_enemy_diplomat_poison"
  3, 10, "e_enemy_diplomat_bribe"
  3, 11, "e_enemy_diplomat_incite"
  3, 12, "e_enemy_diplomat_embassy"
  3, 13, "e_enemy_diplomat_failed"
  3, 14, "e_enemy_diplomat_sabotage"
  3, 15, "e_enemy_diplomat_theft"
  3, 10, "e_enemy_spy_steal_gold"
  3, 17, "e_enemy_spy_steal_map" ; use base sprite for enemy diplomats
  3, 17, "e_enemy_spy_nuke" ; use base sprite for enemy diplomats
;  3, 16, "e_enemy_diplomatic_incident"
;  3, 17, "e_enemy_diplomat_escape" ; base sprite for enemy diplomats
  1,  4, "e_unit_illegal_action"
  1,  5, "e_unit_was_expelled"
  1,  6, "e_unit_did_expel"
  1,  6, "e_unit_action_failed"

  4,  0, "e_achievement"
  4,  1, "e_uprising"
  4,  2, "e_civil_war"
  4,  3, "e_anarchy"
  4,  4, "e_first_contact"
  4,  4, "e_new_government" ; (4, 5) unused
  4,  6, "e_low_on_funds"
  4,  7, "e_pollution" ; copy from small.png
  4,  8, "e_revolt_done"
  4,  9, "e_revolt_start"
  4, 10, "e_spaceship"

  1, 2,  "e_spontaneous_extra"
}
