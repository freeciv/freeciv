/*****************************************************************************\
 Freeciv web client, client functions.
\*****************************************************************************/

  var url = "/axis/FreecivWebService.jws";
  var time = 0;
 

/* This is the main loop of the Freeciv Javascript client.
 * The main loop iterates once every 100 ms, calling all
 * other functions when appropriate. */
function main_loop () 
{

  if (time % 2000 == 0) draw_mapview();

  if (time % 1000 == 0) updateChatbox();

  if (time % 5000 == 0) get_freeciv_state();

  setTimeout(main_loop, 100);
  time += 100;
}


/* Poll the civclient webservice to update the chat box with new messages. */
function updateChatbox() 
{
  var pl = new SOAPClientParameters();
  try {
    SOAPClient.invoke(url, "getFreecivMessages", pl, true, show_message);
  } catch(e) {
    alert("An error has occured!");
  }

}

/* callback to updateChatbox() from webservice. */
function show_message(text)
{


  if (text != null ) {
    document.getElementById('chatbox').contentDocument.writeln(text); 

    /* Scroll window to bottom. */
    var y =  10000000;
    if (top.opera && (typeof window.pageYOffset != 'undefined')) {
      window.chatbox.pageYOffset = y;
    } else if (window.document.compatMode && (window.document.compatMode !='BackCompat')) {
      window.chatbox.document.documentElement.scrollTop = y;
    } else {
      window.chatbox.scrollBy (0,y);
    }
  }
}

/*  */
function get_freeciv_state() 
{
  var pl = new SOAPClientParameters();
  try {
    SOAPClient.invoke(url, "getFreecivState", pl, true, freeciv_state_callBack);
  } catch(e) {
    alert("An error has occured!");
  }

}

/* callback to get_freeciv_state() from webservice. */
function freeciv_state_callBack(new_state)
{
  if (new_state != null ) {
    current_game_state = new_state;
  }
}

/* Send a chat message to the civclient webservice. */
function send_freeciv_message () 
{
  var message = document.getElementById('messagebox');
  var pl = new SOAPClientParameters();
  pl.add("message", message.value);

  try {
    SOAPClient.invoke(url, "addFreecivMessage", pl, true);
  } catch(e) {
    alert("An error has occured!");
  }
  message.value = '';
  return false;
}



