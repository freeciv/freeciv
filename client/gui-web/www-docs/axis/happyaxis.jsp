<html>
<%@ page contentType="text/html; charset=utf-8"
         import="java.io.InputStream,
                 java.io.IOException,
                 javax.xml.parsers.SAXParser,
                 java.lang.reflect.*,
                 javax.xml.parsers.SAXParserFactory"
   session="false" %>
<%
/*
 * Copyright 2002,2004,2005 The Apache Software Foundation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
%>

<%!
    /*
     * Happiness tests for axis. These look at the classpath and warn if things
     * are missing. Normally addng this much code in a JSP page is mad
     * but here we want to validate JSP compilation too, and have a drop-in
     * page for easy re-use
     * @author Steve 'configuration problems' Loughran
     * @author dims
     * @author Brian Ewins
     */

    /**
     * test for a class existing
     * @param classname
     * @return class iff present
     */
    Class classExists(String classname) {
        try {
            return Class.forName(classname);
        } catch (ClassNotFoundException e) {
            return null;
        }
    }

    /**
     * test for resource on the classpath
     * @param resource
     * @return true iff present
     */
    boolean resourceExists(String resource) {
        boolean found;
        InputStream instream=this.getClass().getResourceAsStream(resource);
        found=instream!=null;
        if(instream!=null) {
            try {
                instream.close();
            } catch (IOException e) {
            }
        }
        return found;
    }

    /**
     * probe for a class, print an error message is missing
     * @param out stream to print stuff
     * @param category text like "warning" or "error"
     * @param classname class to look for
     * @param jarFile where this class comes from
     * @param errorText extra error text
     * @param homePage where to d/l the library
     * @return the number of missing classes
     * @throws IOException
     */
    int probeClass(JspWriter out,
                   String category,
                   String classname,
                   String jarFile,
                   String description,
                   String errorText,
                   String homePage) throws IOException {
        try {
            Class clazz = classExists(classname);
            if(clazz == null)  {
               String url="";
               if(homePage!=null) {
                  url=getMessage("seeHomepage",homePage,homePage);
               }
               out.write(getMessage("couldNotFound",category,classname,jarFile,errorText,url));
               return 1;
            } else {
               String location = getLocation(out, clazz);

               if(location == null) {
                  out.write("<li>"+getMessage("foundClass00",description,classname)+"</li><br>");
               }
               else {
                  out.write("<li>"+getMessage("foundClass01",description,classname,location)+"</li><br>");
               }
               return 0;
            }
        } catch(NoClassDefFoundError ncdfe) {
            String url="";
            if(homePage!=null) {
                url=getMessage("seeHomepage",homePage,homePage);
            }
            out.write(getMessage("couldNotFoundDep",category, classname, errorText, url));
            out.write(getMessage("theRootCause",ncdfe.getMessage(), classname));
            return 1;
        }
    }

    /**
     * get the location of a class
     * @param out
     * @param clazz
     * @return the jar file or path where a class was found
     */

    String getLocation(JspWriter out,
                       Class clazz) {
        try {
            java.net.URL url = clazz.getProtectionDomain().getCodeSource().getLocation();
            String location = url.toString();
            if(location.startsWith("jar")) {
                url = ((java.net.JarURLConnection)url.openConnection()).getJarFileURL();
                location = url.toString();
            }

            if(location.startsWith("file")) {
                java.io.File file = new java.io.File(url.getFile());
                return file.getAbsolutePath();
            } else {
                return url.toString();
            }
        } catch (Throwable t){
        }
        return getMessage("classFoundError");
    }

    /**
     * a class we need if a class is missing
     * @param out stream to print stuff
     * @param classname class to look for
     * @param jarFile where this class comes from
     * @param errorText extra error text
     * @param homePage where to d/l the library
     * @throws IOException when needed
     * @return the number of missing libraries (0 or 1)
     */
    int needClass(JspWriter out,
                   String classname,
                   String jarFile,
                   String description,
                   String errorText,
                   String homePage) throws IOException {
        return probeClass(out,
                "<b>"+getMessage("error")+"</b>",
                classname,
                jarFile,
                description,
                errorText,
                homePage);
    }

    /**
     * print warning message if a class is missing
     * @param out stream to print stuff
     * @param classname class to look for
     * @param jarFile where this class comes from
     * @param errorText extra error text
     * @param homePage where to d/l the library
     * @throws IOException when needed
     * @return the number of missing libraries (0 or 1)
     */
    int wantClass(JspWriter out,
                   String classname,
                   String jarFile,
                   String description,
                   String errorText,
                   String homePage) throws IOException {
        return probeClass(out,
                "<b>"+getMessage("warning")+"</b>",
                classname,
                jarFile,
                description,
                errorText,
                homePage);
    }

    /**
     *  get servlet version string
     *
     */

    public String getServletVersion() {
        ServletContext context=getServletConfig().getServletContext();
        int major = context.getMajorVersion();
        int minor = context.getMinorVersion();
        return Integer.toString(major) + '.' + Integer.toString(minor);
    }

    /**
     * what parser are we using.
     * @return the classname of the parser
     */
    private String getParserName() {
        SAXParser saxParser = getSAXParser();
        if (saxParser == null) {
            return getMessage("couldNotCreateParser");
        }

        // check to what is in the classname
        String saxParserName = saxParser.getClass().getName();
        return saxParserName;
    }

    /**
     * Create a JAXP SAXParser
     * @return parser or null for trouble
     */
    private SAXParser getSAXParser() {
        SAXParserFactory saxParserFactory = SAXParserFactory.newInstance();
        if (saxParserFactory == null) {
            return null;
        }
        SAXParser saxParser = null;
        try {
            saxParser = saxParserFactory.newSAXParser();
        } catch (Exception e) {
        }
        return saxParser;
    }

    /**
     * get the location of the parser
     * @return path or null for trouble in tracking it down
     */

    private String getParserLocation(JspWriter out) {
        SAXParser saxParser = getSAXParser();
        if (saxParser == null) {
            return null;
        }
        String location = getLocation(out,saxParser.getClass());
        return location;
    }

    /**
     * Check if class implements specified interface.
     * @param Class clazz
     * @param String interface name
     * @return boolean
     */
    private boolean implementsInterface(Class clazz, String interfaceName) {
        if (clazz == null) {
            return false;
        }
        Class[] interfaces = clazz.getInterfaces();
        if (interfaces.length != 0) {
            for (int i = 0; i < interfaces.length; i++) {
                if (interfaces[i].getName().equals(interfaceName)) {
                    return true;
                }
            }
        }
        return false;
    }
    %>

<%@ include file="i18nLib.jsp" %>

<%
    // initialize a private HttpServletRequest
    setRequest(request);

    // set a resouce base
    setResouceBase("i18n");
%>

<head>
<title><%= getMessage("pageTitle") %></title>
</head>
<body bgcolor='#ffffff'>

<%
    out.print("<h1>"+ getMessage("pageTitle") +"</h1>");
    out.print("<h2>"+ getMessage("pageRole") +"</h2><p/>");
%>

<%= getLocaleChoice() %>

<%
    out.print("<h3>"+ getMessage("neededComponents") +"</h3>");
%>

<UL>
<%
    int needed=0,wanted=0;

    /**
     * the essentials, without these Axis is not going to work
     */

    // need to check if the available version of SAAJ API meets requirements
    String className = "javax.xml.soap.SOAPPart";
    String interfaceName = "org.w3c.dom.Document";
    Class clazz = classExists(className);
    if (clazz == null || implementsInterface(clazz, interfaceName)) {
        needed = needClass(out, "javax.xml.soap.SOAPMessage",
        	"saaj.jar",
                "SAAJ API",
                getMessage("criticalErrorMessage"),
                "http://ws.apache.org/axis/");
    } else {
        String location = getLocation(out, clazz);

        out.print(getMessage("invalidSAAJ",location));
        out.print(getMessage("criticalErrorMessage"));
        out.print(getMessage("seeHomepage","http://ws.apache.org/axis/java/install.html",getMessage("axisInstallation")));
        out.print("<br>");
    }

    needed+=needClass(out, "javax.xml.rpc.Service",
            "jaxrpc.jar",
            "JAX-RPC API",
            getMessage("criticalErrorMessage"),
            "http://ws.apache.org/axis/");

    needed+=needClass(out, "org.apache.axis.transport.http.AxisServlet",
            "axis.jar",
            "Apache-Axis",
            getMessage("criticalErrorMessage"),
            "http://ws.apache.org/axis/");

    needed+=needClass(out, "org.apache.commons.discovery.Resource",
            "commons-discovery.jar",
            "Jakarta-Commons Discovery",
            getMessage("criticalErrorMessage"),
            "http://jakarta.apache.org/commons/discovery/");

    needed+=needClass(out, "org.apache.commons.logging.Log",
            "commons-logging.jar",
            "Jakarta-Commons Logging",
            getMessage("criticalErrorMessage"),
            "http://jakarta.apache.org/commons/logging/");

    needed+=needClass(out, "org.apache.log4j.Layout",
            "log4j-1.2.8.jar",
            "Log4j",
            getMessage("uncertainErrorMessage"),
            "http://jakarta.apache.org/log4j");

    //should we search for a javax.wsdl file here, to hint that it needs
    //to go into an approved directory? because we dont seem to need to do that.
    needed+=needClass(out, "com.ibm.wsdl.factory.WSDLFactoryImpl",
            "wsdl4j.jar",
            "IBM's WSDL4Java",
            getMessage("criticalErrorMessage"),
            null);

    needed+=needClass(out, "javax.xml.parsers.SAXParserFactory",
            "xerces.jar",
            "JAXP implementation",
            getMessage("criticalErrorMessage"),
            "http://xml.apache.org/xerces-j/");

    needed+=needClass(out,"javax.activation.DataHandler",
            "activation.jar",
            "Activation API",
            getMessage("criticalErrorMessage"),
            "http://java.sun.com/products/javabeans/glasgow/jaf.html");
%>
</UL>
<%
    out.print("<h3>"+ getMessage("optionalComponents") +"</h3>");
%>
<UL>
<%
    /*
     * now the stuff we can live without
     */
    wanted+=wantClass(out,"javax.mail.internet.MimeMessage",
            "mail.jar",
            "Mail API",
            getMessage("attachmentsError"),
            "http://java.sun.com/products/javamail/");

    wanted+=wantClass(out,"org.apache.xml.security.Init",
            "xmlsec.jar",
            "XML Security API",
            getMessage("xmlSecurityError"),
            "http://xml.apache.org/security/");

    wanted += wantClass(out, "javax.net.ssl.SSLSocketFactory",
            "jsse.jar or java1.4+ runtime",
            "Java Secure Socket Extension",
            getMessage("httpsError"),
            "http://java.sun.com/products/jsse/");
    /*
     * resources on the classpath path
     */
    /* add more libraries here */

%>
</UL>
<%
    out.write("<h3>");
    //is everythng we need here
    if(needed==0) {
       //yes, be happy
        out.write(getMessage("happyResult00"));
    } else {
        //no, be very unhappy
        response.setStatus(HttpServletResponse.SC_INTERNAL_SERVER_ERROR);
        out.write(getMessage("unhappyResult00",Integer.toString(needed)));
    }
    //now look at wanted stuff
    if(wanted>0) {
        out.write(getMessage("unhappyResult01",Integer.toString(wanted)));
    } else {
        out.write(getMessage("happyResult01"));
    }
    out.write("</h3>");
%>
<UL>
<%

    //hint if anything is missing
    if(needed>0 || wanted>0 ) {
        out.write(getMessage("hintString"));
    }

    out.write(getMessage("noteString"));
%>
</UL>

    <h2><%= getMessage("apsExamining") %></h2>

<UL>
    <%
        String servletVersion=getServletVersion();
        String xmlParser=getParserName();
        String xmlParserLocation = getParserLocation(out);
    %>
    <table border="1" cellpadding="10">
        <tr><td>Servlet version</td><td><%= servletVersion %></td></tr>
        <tr><td>XML Parser</td><td><%= xmlParser %></td></tr>
        <tr><td>XML ParserLocation</td><td><%= xmlParserLocation %></td></tr>
    </table>
</UL>

<% if(xmlParser.indexOf("crimson")>=0) { %>
    <p>
    <%= getMessage("recommendedParser") %>
    </p>
<%    } %>

    <h2><%= getMessage("sysExamining") %></h2>
<UL>
<%
    /**
     * Dump the system properties
     */
    java.util.Enumeration e=null;
    try {
        e= System.getProperties().propertyNames();
    } catch (SecurityException se) {
    }
    if(e!=null) {
        out.write("<pre>");
        for (;e.hasMoreElements();) {
            String key = (String) e.nextElement();
            out.write(key + "=" + System.getProperty(key)+"\n");
        }
        out.write("</pre><p>");
    } else {
        out.write(getMessage("sysPropError"));
    }
%>
</UL>
    <hr>
    <%= getMessage("apsPlatform") %>:
    <%= getServletConfig().getServletContext().getServerInfo() %>
</body>
</html>
