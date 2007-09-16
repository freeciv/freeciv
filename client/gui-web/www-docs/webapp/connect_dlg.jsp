<html>

<head><title>Freeciv web client</title>
<link rel="shortcut icon" href="../freeciv/data/misc/civicon.png" />
</head>

<body text="white" background="../freeciv/data/themes/gui-gtk-2.0/Freeciv/gtk-2.0/bg.png">

<table width="640" height="480" background="../freeciv/data/themes/gui-sdl/human/intro.png" align="center">
<tr><td valign="bottom" align="center">

<FORM action="new_game.jsp" method="get">
<input type="hidden" name="action" value="join">
<b>Connect to a Freeciv server:</b>
<table border="0">
<tr><td>
Server:
</td><td>
<input type="text" name="server" size="18" value="localhost">
</td></tr>
<tr><td>
Port:
</td><td>
<input type="text" name="port" size="18" value="5556">
</td></tr>

<tr><td>
Name:
</td><td>
<input type="text" name="name" size="18">
</td></tr>
<tr><td align="center" colspan="2">
<br><input type="submit" value="Connect">
</td></tr>
</table>
</form>

</td></tr>
</table>
</body>
</html>
