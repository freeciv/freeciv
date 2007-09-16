<%@page import="java.util.*"%>
<%@page import="java.io.*"%>

<%
  int currentPort;

  ServletContext srvContext = request.getSession().getServletContext();
  Integer currentWebservicePort = (Integer)srvContext.getAttribute("currentWebservicePort"); 
  if (currentWebservicePort != null) {
    currentPort = currentWebservicePort + 1;  /* FIXME: Should recycle old ports. */
    /* set the current global port in servlet context. */
    srvContext.setAttribute("currentWebservicePort", new Integer(currentPort)); 
  } else {
    /* First user connecting. */
    currentPort = 2000;
    srvContext.setAttribute("currentWebservicePort", new Integer(currentPort)); 
  }
  out.println("currentPort:" + currentPort);

  /* set the currentPort for this session in session context */
  request.getSession().setAttribute("currentWebservicePort", currentPort)  ;

  /* Parse input parameters.. */

  String action = request.getParameter("action");
  String playerName = request.getParameter("name");
  String civServer = request.getParameter("server");
  String civserverPort = request.getParameter("port");


  Runtime runtime = Runtime.getRuntime();
  Process process = null;
  try {
    String execs;
    if ("new".equals(action)) {
      /* Start a new game, by autostarting a new local server. */
      execs = "civclient -a -- -NHTTPport " + currentPort + " -autostartserver";
    } else {
      /*  Connect to the server specific by the user. */
      execs = "civclient -a -name " + playerName + " -server " + civServer + " -port " + civserverPort + " -- -NHTTPport " + currentPort;
    }

    /* Execute the civclient as a webservice, with it's own port to listen to. */
    process = runtime.exec(execs);
    System.out.println("Accepted new client:  " + execs + "\n");

    /* Create two threads for reading input from the new civclient process. */ 
    final BufferedReader clientOut = new BufferedReader(new InputStreamReader(process.getInputStream()));
    final BufferedReader clientErr = new BufferedReader(new InputStreamReader(process.getErrorStream()));

    new Thread(new Runnable() {
    String line;
    public void run() {
      try {
        while ((line = clientOut.readLine()) != null)
        System.out.println(line);
      } catch(Exception err) {
        err.printStackTrace();
      }
    }
    }).start();
    new Thread(new Runnable() {
    String line;
    public void run() {
      try {
        while ((line = clientErr.readLine()) != null)
        System.out.println(line);
      } catch(Exception err) {
        err.printStackTrace();
      }
    }
    }).start();

 
    HashMap clientProcs = (HashMap)srvContext.getAttribute("clientProcesses"); 
    if (clientProcs == null) {
      clientProcs = new HashMap();
      srvContext.setAttribute("clientProcesses", clientProcs);
    }
    /* Store this process in a hashmap with current port as key,
       and the process, and date as values. */
    clientProcs.put(new Integer(currentPort), new Object[] {process, new Date()});

  } catch(Exception e) {
    out.println("error==="+e.getMessage());
    e.printStackTrace();
    return;
  }

  response.sendRedirect("mapview.jsp");


%>

