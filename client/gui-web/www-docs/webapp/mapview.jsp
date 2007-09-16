<%@page import="java.util.*"%>

<html>
<body background="/images/bg.jpg" onload="init_game();">
<!-- Freeciv debug: <%= request.getSession().getAttribute("currentWebservicePort") + " / " + new Date() %> -->
<script type="text/javascript" src="/javascript/game.js"></script>
<script type="text/javascript" src="/javascript/graphics.js"></script>
<script type="text/javascript" src="/javascript/soapclient.js"></script>
<script type="text/javascript" src="/javascript/civclient.js"></script>
<script type="text/javascript" src="/javascript/profiler.js"></script>
<!--[if IE]><script type="text/javascript" src="/javascript/excanvas.js"></script><![endif]-->

<div id="logo" style="position:absolute; top:5; left:5; height:30; width:180;">
<a href="http://www.freeciv.org" target="_new"><img src="/images/logo.png" border="0"></a>
</div>

<div id="menu" style="position:absolute; top:5; left:190; right:0; height:25;">
<input type="button" name="game" value="Game" style="background-image:url(/images/button.png);">
<input type="button" name="government" value="Government" style="background-image:url(/images/button.png);">
<input type="button" name="view" value="View" style="background-image:url(/images/button.png);">
<input type="button" name="orders" value="Orders" style="background-image:url(/images/button.png);">
<input type="button" name="reports" value="Reports" style="background-image:url(/images/button.png);">
<input type="button" name="editor" value="Editor" style="background-image:url(/images/button.png);">
<input type="button" name="help" value="Help" style="background-image:url(/images/button.png);">
</div>

<canvas id="minimap" width='180' height='100'  style="position:absolute; left:5px; top:35px;"> </canvas>
<canvas id="mapview" width='1024 ' height='650' style="position:absolute; left:190; top:35px; bottom:145; right:5;"> </canvas>

<div id="game_info" style="position:absolute; left:5; top:150; width:180; height:400;" align="center">
<br><br><br><br>
<input type="button" name="end_turn" value="Turn Done" style="background-image:url(/images/button.png);">
</div>

<div id="chatline" style="position: absolute; left: 0px; right:0px; bottom: 0px ; height: 130px; padding: 5px; ">
<form name="uplink" onSubmit="return send_freeciv_message();">
<iframe id="chatbox" name="chatbox" style="width:100%; height:110; border:0; background-image:url(/images/bg_dark.jpg);"></iframe>
<br>
<input id="messagebox" type="text" style="height:20; width:100%" > 
</form>
</div>


</body>
</html>
