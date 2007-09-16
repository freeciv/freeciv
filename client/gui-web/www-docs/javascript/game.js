/*****************************************************************************\
 Freeciv web client, game functions. 
\*****************************************************************************/

  var current_game_state = 0;

  /*  enum client_states from game.h in Freeciv. */
  var CLIENT_BOOT_STATE = 0;
  var CLIENT_PRE_GAME_STATE = 1;
  var CLIENT_WAITING_FOR_GAME_START_STATE = 2;
  var CLIENT_GAME_RUNNING_STATE = 3;
  var CLIENT_GAME_OVER_STATE = 4;

function init_game () 
{
  init_mapview();
  main_loop();


}
