#include <stdio.h>
#include <stdlib.h>
#include <resources.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/AsciiText.h>  
#include <xstuff.h>
#include <clinet.h>
#include <chatline.h>


extern AppResources appResources;
extern Widget toplevel;
extern char name[];
extern char server_host[];
extern int  server_port;

Widget iname, ihost, iport;
Widget connw, quitw;

void server_address_ok_callback(Widget w, XtPointer client_data, 
				XtPointer call_data);
void quit_callback(Widget w, XtPointer client_data, XtPointer call_data);
void connect_callback(Widget w, XtPointer client_data, XtPointer call_data);



int gui_server_connect(void)
{
  Widget shell, form, label, label2;
  char buf[512];
  
  XtTranslations textfieldtranslations;
  
  XtSetSensitive(toplevel, FALSE);
  
  shell=XtCreatePopupShell("connectdialog", transientShellWidgetClass,
			   toplevel, NULL, 0);
  
  form=XtVaCreateManagedWidget("cform", formWidgetClass, shell, NULL);

  label=XtVaCreateManagedWidget("cheadline", labelWidgetClass, form, NULL);   

  XtVaCreateManagedWidget("cnamel", labelWidgetClass, form, NULL);   
  iname=XtVaCreateManagedWidget("cnamei", asciiTextWidgetClass, form, 
			  XtNstring, name, NULL);

  XtVaCreateManagedWidget("chostl", labelWidgetClass, form, NULL);   
  ihost=XtVaCreateManagedWidget("chosti", asciiTextWidgetClass, form, 
			  XtNstring, server_host, NULL);

  sprintf(buf, "%d", server_port);
  
  XtVaCreateManagedWidget("cportl", labelWidgetClass, form, NULL);   
  iport=XtVaCreateManagedWidget("cporti", asciiTextWidgetClass, form, 
			  XtNstring, buf, NULL);

  connw=XtVaCreateManagedWidget("cconnectc", commandWidgetClass, form, NULL);   
  quitw=XtVaCreateManagedWidget("cquitc", commandWidgetClass, form, NULL); 

#if MINOR_VERSION < 7
  label2=XtVaCreateManagedWidget("cbetaline", labelWidgetClass, form, NULL);   
#endif

  XtAddCallback(connw, XtNcallback, connect_callback, NULL);
  XtAddCallback(quitw, XtNcallback, quit_callback, NULL);

  XtPopup(shell, XtGrabNone);
  xaw_set_relative_position(toplevel, shell, 50, 50);

  textfieldtranslations = 
    XtParseTranslationTable("<Key>Return: connect-dialog-returnkey()");
  XtOverrideTranslations(form, textfieldtranslations);
  XtOverrideTranslations(iname, textfieldtranslations);
  XtOverrideTranslations(ihost, textfieldtranslations);
  XtOverrideTranslations(iport, textfieldtranslations);

  XtSetKeyboardFocus(toplevel, shell);


  return 1;
}

/****************************************************************
...
*****************************************************************/
void connect_dialog_returnkey(Widget w, XEvent *event, String *params,
			    Cardinal *num_params)
{
  x_simulate_button_click(connw);
}
  
  
  
void quit_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data) 
{
  exit(0);
}


  
    
void connect_callback(Widget w, XtPointer client_data, 
		      XtPointer call_data) 
{
  XtPointer dp;
  char errbuf[512];
  
  XtVaGetValues(iname, XtNstring, &dp, NULL);
  strcpy(name, (char*)dp);
  XtVaGetValues(ihost, XtNstring, &dp, NULL);
  strcpy(server_host, (char*)dp);
  XtVaGetValues(iport, XtNstring, &dp, NULL);
  sscanf((char*)dp, "%d", &server_port);
  
  if(connect_to_server(name, server_host, server_port, errbuf)!=-1) {
    XtDestroyWidget(XtParent(XtParent(w)));
    XtSetSensitive(toplevel, True);
    return;
  }
  
  append_output_window(errbuf);
}
