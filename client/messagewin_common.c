
#include <assert.h>

#include "messagewin_g.h"
#include "options.h"

#include "messagewin_common.h"

static int frozen_level = 0;
static bool change = FALSE;
static bool messages_exists = FALSE;

/******************************************************************
 Turn off updating of message window
*******************************************************************/
void meswin_freeze(void)
{
  frozen_level++;
}

/******************************************************************
 Turn on updating of message window
*******************************************************************/
void meswin_thaw(void)
{
  frozen_level--;
  assert(frozen_level >= 0);
  if (frozen_level == 0) {
    update_meswin_dialog();
  }
}

/******************************************************************
 Turn on updating of message window
*******************************************************************/
void meswin_force_thaw(void)
{
  frozen_level = 1;
  meswin_thaw();
}

/**************************************************************************
...
**************************************************************************/
void update_meswin_dialog(void)
{
  if (frozen_level > 0 || !change) {
    return;
  }

  if (!is_meswin_open() && messages_exists &&
      (!game.player_ptr->ai.control || ai_popup_windows)) {
    popup_meswin_dialog();
    change = FALSE;
    return;
  }

  if (is_meswin_open()) {
    real_update_meswin_dialog();
    change = FALSE;
  }
}

/**************************************************************************
...
**************************************************************************/
void clear_notify_window(void)
{
  messages_exists = FALSE;
  change = TRUE;
  real_clear_notify_window();
  update_meswin_dialog();
}

/**************************************************************************
...
**************************************************************************/
void add_notify_window(struct packet_generic_message *packet)
{
  messages_exists = TRUE;
  change = TRUE;
  real_add_notify_window(packet);
  update_meswin_dialog();
}
