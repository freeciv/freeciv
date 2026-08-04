/***********************************************************************
 Freeciv - Copyright (C) 1996 - The Freeciv Project

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* utility */
#include "fc_cmdline.h"
#include "fciconv.h"
#include "log.h"
#include "mem.h"
#include "netintf.h"
#include "support.h"
#include "timing.h"

/* common */
#include "player.h"

#include "gui_agent.h"

/* client */
#include "chatline_common.h"
#include "client_main.h"
#include "clinet.h"
#include "connection.h"
#include "editgui_g.h"
#include "gui_cbsetter.h"
#include "gui_interface.h"
#include "gui_properties.h"
#include "options.h"
#include "tilespec.h"

/* client include */
#include "canvas_g.h"
#include "dialogs_g.h"
#include "diplodlg_g.h"
#include "graphics_g.h"
#include "gui_main_g.h"
#include "repodlgs_g.h"
#include "sprite_g.h"

#include "ipc_codec.h"
#include "fd_guard.h"
#include "protocol_v2.h"

const char *client_string = "gui-agent";

const char * const gui_character_encoding = "UTF-8";
const bool gui_use_transliteration = FALSE;

#define AGENT_TAKE_TIMEOUT_SECONDS 15.0

struct agent_idle_callback {
  void (*callback)(void *);
  void *data;
  struct agent_idle_callback *next;
};

enum agent_option_result {
  AGENT_OPTIONS_OK,
  AGENT_OPTIONS_HELP,
  AGENT_OPTIONS_ERROR
};

static struct fc_agent_ipc agent_ipc;
static struct agent_idle_callback *idle_head;
static struct agent_idle_callback *idle_tail;
static int agent_net_socket = -1;
static int agent_ipc_fd = -1;
static char agent_player_name[MAX_LEN_NAME];
static bool agent_loop_active;
static bool agent_exit_requested;
static bool agent_exit_failure;
static bool agent_server_connected;
static bool agent_net_needs_write;
static bool agent_handshake_complete;
static bool agent_take_requested;
static bool agent_take_sent;
static bool agent_ready_reported;
static bool agent_options_preparsed;
static struct timer *agent_take_timer;
static int agent_target_player_number = -1;
static uint64_t agent_target_player_lifecycle_id;

enum agent_common_option_flag {
  AGENT_COMMON_NAME = 1 << 0,
  AGENT_COMMON_SERVER = 1 << 1,
  AGENT_COMMON_PORT = 1 << 2,
  AGENT_COMMON_AUTOCONNECT = 1 << 3,
  AGENT_COMMON_DEBUG = 1 << 4
};

/**********************************************************************//**
  Complete the callback-mode table for paths that the historical stub client
  never exercised. The agent client loads a real tileset and receives the
  full packet stream, so every callback exposed through gui_interface must be
  non-null.
**************************************************************************/
static void agent_complete_gui_funcs(void)
{
  struct gui_funcs *funcs = get_gui_funcs();

  funcs->tileset_type_set = gui_tileset_type_set;
  funcs->load_gfxnumber = gui_load_gfxnumber;
  funcs->canvas_put_sprite_full_scaled = gui_canvas_put_sprite_full_scaled;

  funcs->gui_init_meeting = gui_gui_init_meeting;
  funcs->gui_recv_cancel_meeting = gui_gui_recv_cancel_meeting;
  funcs->gui_prepare_clause_updt = gui_gui_prepare_clause_updt;
  funcs->gui_recv_create_clause = gui_gui_recv_create_clause;
  funcs->gui_recv_remove_clause = gui_gui_recv_remove_clause;
  funcs->gui_recv_accept_treaty = gui_gui_recv_accept_treaty;

  funcs->request_action_confirmation = gui_request_action_confirmation;

  funcs->real_science_report_dialog_update
    = gui_real_science_report_dialog_update;
  funcs->science_report_dialog_redraw = gui_science_report_dialog_redraw;
  funcs->science_report_dialog_popup = gui_science_report_dialog_popup;
  funcs->real_economy_report_dialog_update
    = gui_real_economy_report_dialog_update;
  funcs->real_units_report_dialog_update
    = gui_real_units_report_dialog_update;
  funcs->endgame_report_dialog_start = gui_endgame_report_dialog_start;
  funcs->endgame_report_dialog_player = gui_endgame_report_dialog_player;
}

static bool agent_queue(const char *message)
{
  if (!fc_agent_ipc_queue(&agent_ipc, message, strlen(message))) {
    log_error("The private agent IPC output queue could not accept a frame.");
    agent_exit_requested = TRUE;
    agent_exit_failure = TRUE;
    return FALSE;
  }

  return TRUE;
}

static const char *agent_state_name(void)
{
  switch (client_state()) {
  case C_S_INITIAL:
    return "initial";
  case C_S_DISCONNECTED:
    return "disconnected";
  case C_S_PREPARING:
    return "preparing";
  case C_S_RUNNING:
    return "running";
  case C_S_OVER:
    return "over";
  }

  return "unknown";
}

static bool agent_has_target_player(void)
{
  struct player *pplayer = client_player();

  if (!client.conn.established || client_is_observer() || pplayer == NULL
      || !is_human(pplayer) || pplayer->client.lifecycle_id == 0) {
    return FALSE;
  }

  if (agent_target_player_number >= 0) {
    return player_number(pplayer) == agent_target_player_number
           && pplayer->client.lifecycle_id
              == agent_target_player_lifecycle_id;
  }

  if (strcmp(player_name(pplayer), agent_player_name) != 0) {
    return FALSE;
  }

  /* Nation selection changes the player display name to the selected leader.
   * Pin the exact client-side player incarnation after takeover so that rename
   * cannot revoke the sidecar and slot reuse cannot inherit its authority. */
  agent_target_player_number = player_number(pplayer);
  agent_target_player_lifecycle_id = pplayer->client.lifecycle_id;
  return TRUE;
}

static void agent_reset_take_state(void)
{
  agent_take_requested = FALSE;
  agent_take_sent = FALSE;
  if (agent_take_timer != NULL) {
    timer_destroy(agent_take_timer);
    agent_take_timer = NULL;
  }
}

static void agent_begin_take(void)
{
  agent_reset_take_state();
  agent_take_requested = TRUE;
  agent_take_timer = timer_new(TIMER_USER, TIMER_ACTIVE,
                               "agent seat takeover");
  timer_start(agent_take_timer);
}

static void agent_take_failed(const char *reason)
{
  char message[96];

  fc_snprintf(message, sizeof(message), "TAKE_FAILED\t%s", reason);
  agent_queue(message);
  agent_reset_take_state();
}

static void agent_report_status(void)
{
  char message[256];
  const char *seat_state;
  int player_number = -1;
  uint64_t player_lifecycle_id = 0;

  if (agent_has_target_player()) {
    seat_state = "ready";
    player_number = agent_target_player_number;
    player_lifecycle_id = agent_target_player_lifecycle_id;
  } else if (agent_take_sent) {
    seat_state = "take_sent";
  } else if (agent_take_requested) {
    seat_state = "awaiting_player";
  } else {
    seat_state = "idle";
  }

  fc_snprintf(message, sizeof(message),
              "STATUS\tstate=%s\tserver=%d\tseat=%s\tplayer=%d"
              "\tlifecycle=%llu",
              agent_state_name(), client.conn.established ? 1 : 0,
              seat_state, player_number,
              (unsigned long long) player_lifecycle_id);
  agent_queue(message);
}

static void agent_progress_take(void)
{
  struct player *pplayer;

  if (!agent_handshake_complete) {
    return;
  }

  if (agent_has_target_player()) {
    if (!agent_ready_reported) {
      char message[MAX_LEN_NAME + 16];

      fc_snprintf(message, sizeof(message), "READY\t%s",
                  player_name(client_player()));
      if (agent_queue(message)) {
        agent_ready_reported = TRUE;
      }
    }
    agent_reset_take_state();
    return;
  }
  agent_ready_reported = FALSE;

  if (agent_take_requested && agent_take_timer != NULL
      && timer_read_seconds(agent_take_timer)
         >= AGENT_TAKE_TIMEOUT_SECONDS) {
    if (!client.conn.established) {
      agent_take_failed("NOT_CONNECTED");
    } else if (player_by_name(agent_player_name) == NULL) {
      agent_take_failed("PLAYER_NOT_FOUND");
    } else {
      agent_take_failed("NOT_ACQUIRED");
    }
    return;
  }

  if (!agent_take_requested || agent_take_sent
      || !client.conn.established) {
    return;
  }

  /* player_by_name() proves that the ordinary player list has arrived.
   * Use the canonical server-provided name in the normal client command. */
  pplayer = player_by_name(agent_player_name);
  if (pplayer == NULL
      || strcmp(player_name(pplayer), agent_player_name) != 0) {
    return;
  }

  if (send_chat_printf("/take \"%s\"", player_name(pplayer)) > 0) {
    agent_take_sent = TRUE;
    agent_queue("TAKE\tCOMMAND_SENT");
  } else {
    agent_take_failed("SEND_FAILED");
  }
}

static bool agent_ping_token_valid(const char *token)
{
  size_t length = strlen(token);
  size_t i;

  if (length == 0 || length > 64) {
    return FALSE;
  }
  for (i = 0; i < length; i++) {
    unsigned char ch = (unsigned char) token[i];

    if (!((ch >= 'a' && ch <= 'z')
          || (ch >= 'A' && ch <= 'Z')
          || (ch >= '0' && ch <= '9')
          || ch == '.' || ch == '_' || ch == ':' || ch == '-')) {
      return FALSE;
    }
  }

  return TRUE;
}

static void agent_handle_command(const char *payload, size_t length)
{
  if (!agent_handshake_complete) {
    if (length == strlen("HELLO\t1")
        && memcmp(payload, "HELLO\t1", length) == 0) {
      agent_handshake_complete = TRUE;
      agent_queue("HELLO\tOK\t1");
      fc_agent_v2_advertise();
      agent_progress_take();
    } else {
      agent_queue("ERROR\tHANDSHAKE_REQUIRED\texpected HELLO protocol 1");
    }
    return;
  }

  if (fc_agent_v2_handle(payload, length)) {
    return;
  }

  if (length == strlen("STATUS")
      && memcmp(payload, "STATUS", length) == 0) {
    agent_report_status();
  } else if (length == strlen("TAKE")
             && memcmp(payload, "TAKE", length) == 0) {
    if (agent_has_target_player()) {
      agent_queue("TAKE\tREADY");
      agent_progress_take();
    } else {
      agent_begin_take();
      agent_queue("TAKE\tQUEUED");
      agent_progress_take();
    }
  } else if (length == strlen("SHUTDOWN")
             && memcmp(payload, "SHUTDOWN", length) == 0) {
    agent_queue("BYE\tSHUTDOWN");
    agent_exit_requested = TRUE;
  } else if (length > strlen("PING\t")
             && memcmp(payload, "PING\t", strlen("PING\t")) == 0
             && agent_ping_token_valid(payload + strlen("PING\t"))) {
    char response[80];

    fc_snprintf(response, sizeof(response), "PONG\t%s",
                payload + strlen("PING\t"));
    agent_queue(response);
  } else {
    agent_queue("ERROR\tBAD_COMMAND\tcommand does not match protocol 1");
  }
}

static void agent_run_idle_callbacks(void)
{
  struct agent_idle_callback *run_head = idle_head;

  /* Detach this tick's snapshot. Callbacks queued by callbacks are left on
   * the new global queue until the next event-loop tick. */
  idle_head = NULL;
  idle_tail = NULL;

  while (run_head != NULL) {
    struct agent_idle_callback *entry = run_head;

    run_head = entry->next;
    entry->callback(entry->data);
    free(entry);
  }
}

static void agent_clear_idle_callbacks(void)
{
  while (idle_head != NULL) {
    struct agent_idle_callback *entry = idle_head;

    idle_head = entry->next;
    free(entry);
  }
  idle_tail = NULL;
}

static void print_usage(const char *argv0)
{
  fc_fprintf(stderr,
             _("Usage: %s [Freeciv options] -- --ipc-fd FD --player NAME\n"),
             argv0);
  fc_fprintf(stderr,
             _("The IPC descriptor must be inherited; do not pass credentials "
               "on the command line.\n\n"));
  fc_fprintf(stderr, _("Report bugs at %s\n"), BUG_URL);
}

static bool agent_player_option_valid(const char *name)
{
  size_t length = strlen(name);
  size_t i;

  if (length == 0 || length >= sizeof(agent_player_name)) {
    return FALSE;
  }
  for (i = 0; i < length; i++) {
    unsigned char ch = (unsigned char) name[i];

    if (!((ch >= 'a' && ch <= 'z')
          || (ch >= 'A' && ch <= 'Z')
          || (ch >= '0' && ch <= '9')
          || ch == '.' || ch == '_' || ch == '-')) {
      return FALSE;
    }
  }

  return TRUE;
}

static bool agent_parse_fd(const char *text, int *fd)
{
  char *end = NULL;
  char error[128];
  long value;

  errno = 0;
  value = strtol(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0'
      || value < 3 || value >= FD_SETSIZE || value > INT_MAX
      || fcntl((int) value, F_GETFD, 0) < 0
      || !fc_agent_ipc_validate_fd((int) value, error, sizeof(error))) {
    return FALSE;
  }

  *fd = (int) value;
  return TRUE;
}

static bool agent_host_option_valid(const char *host)
{
  size_t length = strlen(host);
  size_t i;

  if (length == 0 || length >= 256) {
    return FALSE;
  }
  for (i = 0; i < length; i++) {
    unsigned char ch = (unsigned char) host[i];

    if (!((ch >= 'a' && ch <= 'z')
          || (ch >= 'A' && ch <= 'Z')
          || (ch >= '0' && ch <= '9')
          || ch == '.' || ch == ':' || ch == '-' || ch == '_'
          || ch == '[' || ch == ']')) {
      return FALSE;
    }
  }

  return TRUE;
}

static bool agent_port_option_valid(const char *port)
{
  char *end = NULL;
  long value;

  errno = 0;
  value = strtol(port, &end, 10);
  return errno == 0 && end != port && *end == '\0'
         && value >= 1 && value <= 65535;
}

static bool agent_debug_option_valid(const char *level)
{
  const char *allowed = "fewnv";

#ifdef FREECIV_DEBUG
  allowed = "fewnvd";
#endif /* FREECIV_DEBUG */

  return level[0] != '\0' && level[1] == '\0'
         && strchr(allowed, level[0]) != NULL;
}

static bool agent_common_options_valid(int argc, char **argv)
{
  unsigned int seen = 0;
  int i = 1;

  while (i < argc) {
    unsigned int flag;
    bool takes_value = TRUE;
    bool value_valid = TRUE;

    if (strcmp(argv[i], "--name") == 0) {
      flag = AGENT_COMMON_NAME;
    } else if (strcmp(argv[i], "--server") == 0) {
      flag = AGENT_COMMON_SERVER;
    } else if (strcmp(argv[i], "--port") == 0) {
      flag = AGENT_COMMON_PORT;
    } else if (strcmp(argv[i], "--autoconnect") == 0) {
      flag = AGENT_COMMON_AUTOCONNECT;
      takes_value = FALSE;
    } else if (strcmp(argv[i], "--debug") == 0) {
      flag = AGENT_COMMON_DEBUG;
    } else {
      fc_fprintf(stderr, _("Unsupported Freeciv option for agent client.\n"));
      return FALSE;
    }

    if ((seen & flag) != 0 || (takes_value && i + 1 >= argc)) {
      fc_fprintf(stderr, _("Missing or duplicate agent launch option.\n"));
      return FALSE;
    }

    if (takes_value) {
      if (flag == AGENT_COMMON_NAME) {
        value_valid = agent_player_option_valid(argv[i + 1]);
      } else if (flag == AGENT_COMMON_SERVER) {
        value_valid = agent_host_option_valid(argv[i + 1]);
      } else if (flag == AGENT_COMMON_PORT) {
        value_valid = agent_port_option_valid(argv[i + 1]);
      } else if (flag == AGENT_COMMON_DEBUG) {
        value_valid = agent_debug_option_valid(argv[i + 1]);
      }
      if (!value_valid) {
        fc_fprintf(stderr, _("Invalid agent launch option value.\n"));
        return FALSE;
      }
      i += 2;
    } else {
      i++;
    }
    seen |= flag;
  }

  return TRUE;
}

static enum agent_option_result parse_options(int argc, char **argv)
{
  int i = 1;
  bool saw_ipc_fd = FALSE;
  bool saw_player = FALSE;

  while (i < argc) {
    if (strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return AGENT_OPTIONS_HELP;
    } else if (strcmp(argv[i], "--ipc-fd") == 0) {
      if (saw_ipc_fd || i + 1 >= argc
          || !agent_parse_fd(argv[i + 1], &agent_ipc_fd)) {
        fc_fprintf(stderr, _("Invalid or duplicate --ipc-fd option.\n"));
        return AGENT_OPTIONS_ERROR;
      }
      saw_ipc_fd = TRUE;
      i += 2;
      continue;
    } else if (strcmp(argv[i], "--player") == 0) {
      if (saw_player || i + 1 >= argc
          || !agent_player_option_valid(argv[i + 1])) {
        fc_fprintf(stderr, _("Invalid or duplicate --player option.\n"));
        return AGENT_OPTIONS_ERROR;
      }
      sz_strlcpy(agent_player_name, argv[i + 1]);
      saw_player = TRUE;
      i += 2;
      continue;
    }

    fc_fprintf(stderr, _("Unrecognized agent option.\n"));
    return AGENT_OPTIONS_ERROR;
  }

  if (!saw_ipc_fd || !saw_player) {
    fc_fprintf(stderr,
               _("Both --ipc-fd and --player are required after --.\n"));
    return AGENT_OPTIONS_ERROR;
  }

  return AGENT_OPTIONS_OK;
}

static double agent_timer_interval(double interval)
{
  if (interval < 0.001) {
    return 0.001;
  }
  if (interval > 1.0) {
    return 1.0;
  }
  return interval;
}

static void agent_timeout_from_seconds(fc_timeval *timeout, double seconds)
{
  timeout->tv_sec = (long) seconds;
  timeout->tv_usec = (long) ((seconds - timeout->tv_sec) * 1000000.0);
  if (timeout->tv_usec < 0) {
    timeout->tv_usec = 0;
  }
}

/**********************************************************************//**
  Track whether the ordinary Freeciv connection has buffered output whose
  first send attempt encountered a full socket.
**************************************************************************/
static void agent_set_net_writable(struct connection *pc,
                                   bool socket_writable)
{
  fc_assert_ret(pc == &client.conn);
  agent_net_needs_write = socket_writable;
}

static int agent_event_loop(void)
{
  struct timer *timer = timer_new(TIMER_USER, TIMER_ACTIVE,
                                  "agent client loop");
  double timer_due = 0.0;
  unsigned int exit_flush_attempts = 0;

  timer_start(timer);
  agent_loop_active = TRUE;

  while (TRUE) {
    fd_set readfds;
    fd_set writefds;
    fc_timeval timeout;
    double elapsed = timer_read_seconds(timer);
    double wait_seconds;
    int selected_net_socket = agent_net_socket;
    int max_fd = agent_ipc.fd;
    int selected;
    bool watch_writes;

    if (elapsed >= timer_due) {
      timer_due = agent_timer_interval(real_timer_callback());
      timer_clear(timer);
      timer_start(timer);
      elapsed = 0.0;
    }

    agent_progress_take();
    agent_run_idle_callbacks();
    if (agent_handshake_complete) {
      fc_agent_v2_tick();
    }

    if (agent_exit_requested && !fc_agent_ipc_wants_write(&agent_ipc)) {
      break;
    }
    if (agent_exit_requested && ++exit_flush_attempts > 20) {
      break;
    }

    wait_seconds = timer_due - elapsed;
    if (wait_seconds > 0.1) {
      wait_seconds = 0.1;
    } else if (wait_seconds < 0.001) {
      wait_seconds = 0.001;
    }
    agent_timeout_from_seconds(&timeout, wait_seconds);

    FC_FD_ZERO(&readfds);
    FD_SET(agent_ipc.fd, &readfds);
    if (selected_net_socket >= 0) {
      FD_SET(selected_net_socket, &readfds);
      if (selected_net_socket > max_fd) {
        max_fd = selected_net_socket;
      }
    }

    FC_FD_ZERO(&writefds);
    if (fc_agent_ipc_wants_write(&agent_ipc)) {
      FD_SET(agent_ipc.fd, &writefds);
    }
    if (selected_net_socket >= 0 && agent_net_needs_write) {
      FD_SET(selected_net_socket, &writefds);
    }
    watch_writes = fc_agent_ipc_wants_write(&agent_ipc)
                   || (selected_net_socket >= 0 && agent_net_needs_write);

    selected = fc_select(max_fd + 1, &readfds,
                         watch_writes ? &writefds : NULL, NULL, &timeout);
    if (selected < 0) {
      if (errno == EINTR) {
        continue;
      }
      log_error("The agent event loop select failed: %s",
                fc_strerror(fc_get_errno()));
      agent_exit_failure = TRUE;
      break;
    }

    if (FD_ISSET(agent_ipc.fd, &readfds)) {
      const char *payload;
      size_t payload_length;
      enum fc_agent_ipc_read_result read_result;

      read_result = fc_agent_ipc_read(&agent_ipc, &payload,
                                      &payload_length);
      if (read_result == FC_AGENT_IPC_READ_MESSAGE) {
        agent_handle_command(payload, payload_length);
      } else if (read_result == FC_AGENT_IPC_READ_EOF) {
        log_normal("The private agent IPC peer disconnected; exiting.");
        break;
      } else if (read_result == FC_AGENT_IPC_READ_ERROR) {
        log_error("The private agent IPC descriptor failed while reading.");
        agent_exit_failure = TRUE;
        break;
      } else if (read_result == FC_AGENT_IPC_READ_PROTOCOL_ERROR) {
        agent_queue("ERROR\tBAD_FRAME\tinvalid private IPC frame");
        agent_exit_requested = TRUE;
        agent_exit_failure = TRUE;
      }
    }

    /* Read first when both directions are signalled. This lets a closed peer
     * terminate cleanly instead of attempting to write the queued greeting. */
    if (fc_agent_ipc_wants_write(&agent_ipc)
        && FD_ISSET(agent_ipc.fd, &writefds)
        && !fc_agent_ipc_flush(&agent_ipc)) {
      log_error("The private agent IPC descriptor failed while writing.");
      agent_exit_failure = TRUE;
      break;
    }

    if (selected_net_socket >= 0
        && FD_ISSET(selected_net_socket, &readfds)) {
      input_from_server(selected_net_socket);
    }
    if (selected_net_socket >= 0
        && selected_net_socket == agent_net_socket
        && agent_net_needs_write
        && FD_ISSET(selected_net_socket, &writefds)) {
      flush_connection_send_buffer_all(&client.conn);
    }
  }

  agent_loop_active = FALSE;
  timer_destroy(timer);
  return agent_exit_failure ? EXIT_FAILURE : EXIT_SUCCESS;
}

/**********************************************************************//**
  Do any necessary pre-initialization of the UI.
**************************************************************************/
void gui_ui_init(void)
{
}

/**********************************************************************//**
  Entry point for the headless sidecar client.
**************************************************************************/
int main(int argc, char **argv)
{
  int separator = -1;
  int i;

  /* GUI-specific parsing normally happens after common client startup. The
   * headless sidecar validates its inherited descriptor arguments first so a
   * malformed launch cannot initialize networking, tilesets, or audio. */
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--") == 0) {
      separator = i;
      break;
    }
  }
  if (separator < 0) {
    fc_fprintf(stderr, _("The agent launch requires a -- delimiter.\n"));
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }
  if (!agent_common_options_valid(separator, argv)) {
    return EXIT_FAILURE;
  }
  {
    enum agent_option_result options = parse_options(argc - separator,
                                                     argv + separator);

    if (options == AGENT_OPTIONS_HELP) {
      return EXIT_SUCCESS;
    }
    if (options != AGENT_OPTIONS_OK) {
      return EXIT_FAILURE;
    }
  }
  agent_options_preparsed = TRUE;

  setup_gui_funcs();
  agent_complete_gui_funcs();
  hackless = TRUE;
  return client_main(argc, argv, FALSE);
}

/**********************************************************************//**
  Main headless UI loop.
**************************************************************************/
int gui_ui_main(int argc, char *argv[])
{
  enum agent_option_result options = agent_options_preparsed
                                     ? AGENT_OPTIONS_OK
                                     : parse_options(argc, argv);
  char error[128];
  int result;

  if (options == AGENT_OPTIONS_HELP) {
    return EXIT_SUCCESS;
  }
  if (options != AGENT_OPTIONS_OK) {
    return EXIT_FAILURE;
  }

  /* options_load() has already run in common startup. Never write the user's
   * ordinary GUI client configuration from a private agent sidecar. */
  gui_options.save_options_on_exit = FALSE;
  if (!fc_agent_ipc_init(&agent_ipc, agent_ipc_fd,
                         error, sizeof(error))) {
    fc_fprintf(stderr, _("Invalid private IPC descriptor: %s.\n"), error);
    return EXIT_FAILURE;
  }
  fc_agent_v2_init(agent_queue, agent_has_target_player);

  tileset_init(tileset);
  tileset_load_tiles(tileset);
  agent_queue("HELLO\t1\tfreeciv-agent");

  /* This is the standard Freeciv autoconnect entry point. */
  set_client_state(C_S_DISCONNECTED);
  result = agent_event_loop();
  start_quitting();
  if (client.conn.used) {
    disconnect_from_server(FALSE);
  }
  return result;
}

void gui_options_extra_init(void)
{
  gui_options.save_options_on_exit = FALSE;
}

void gui_ui_exit(void)
{
  agent_reset_take_state();
  agent_clear_idle_callbacks();
  fc_agent_v2_reset();
  fc_agent_ipc_close(&agent_ipc);
}

enum gui_type gui_get_gui_type(void)
{
  return GUI_AGENT;
}

void gui_real_conn_list_dialog_update(void *unused)
{
  agent_progress_take();
}

void gui_sound_bell(void)
{
}

void gui_add_net_input(int sock)
{
  if (!fc_agent_fd_selectable(sock)) {
    /* make_connection() still owns this socket and continues after this
     * callback. Do not close it synchronously here: request a failed exit,
     * and gui_ui_main() will disconnect it through the ordinary client path
     * after the event loop unwinds. Crucially, never store or FD_SET it. */
    log_error("The Freeciv server socket is outside the select() fd range.");
    agent_queue("ERROR\tSERVER_FD_RANGE\tserver socket exceeds select limit");
    agent_exit_requested = TRUE;
    agent_exit_failure = TRUE;
    return;
  }

  agent_net_socket = sock;
  agent_server_connected = TRUE;
  agent_net_needs_write = FALSE;
  client.conn.notify_of_writable_data = agent_set_net_writable;
}

void gui_remove_net_input(void)
{
  client.conn.notify_of_writable_data = NULL;
  agent_net_socket = -1;
  agent_net_needs_write = FALSE;
  agent_ready_reported = FALSE;
  agent_target_player_number = -1;
  agent_target_player_lifecycle_id = 0;
  agent_reset_take_state();
  if (agent_loop_active && agent_server_connected) {
    agent_queue("DISCONNECTED\tserver");
    agent_exit_requested = TRUE;
    agent_exit_failure = TRUE;
  }
  agent_server_connected = FALSE;
}

void gui_set_unit_icon(int idx, struct unit *punit)
{
}

void gui_set_unit_icons_more_arrow(bool onoff)
{
}

void gui_real_focus_units_changed(void)
{
}

/**********************************************************************//**
  Enqueue, rather than synchronously invoke, ordinary client idle work.
**************************************************************************/
void gui_add_idle_callback(void (callback)(void *), void *data)
{
  struct agent_idle_callback *entry = fc_malloc(sizeof(*entry));

  entry->callback = callback;
  entry->data = data;
  entry->next = NULL;
  if (idle_tail != NULL) {
    idle_tail->next = entry;
  } else {
    idle_head = entry;
  }
  idle_tail = entry;
}

void gui_editgui_tileset_changed(void)
{
}

void gui_editgui_refresh(void)
{
}

void gui_editgui_popup_properties(const struct tile_list *tiles, int objtype)
{
}

void gui_editgui_popdown_all(void)
{
}

void gui_editgui_notify_object_changed(int objtype, int object_id,
                                       bool removal)
{
}

void gui_editgui_notify_object_created(int tag, int id)
{
}

void gui_gui_update_font(const char *font_name, const char *font_value)
{
}

void gui_insert_client_build_info(char *outbuf, size_t outlen)
{
  fc_strlcpy(outbuf, "headless agent sidecar", outlen);
}

void gui_setup_gui_properties(void)
{
  gui_properties.animations = FALSE;
  gui_properties.views.isometric = TRUE;
  gui_properties.views.overhead = TRUE;
  gui_properties.views.d3 = FALSE;
}
