
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">

<html xmlns="http://www.w3.org/1999/xhtml" >
<head>
    <title>JavaScript SOAP Client - DEMOS | GURU4.net</title>
	<style type="text/css">
		div.h { display: none; }
		div.s { display: block; margin: 10px; }
	</style>
	<script type="text/javascript" src="/javascript/soapclient.js"></script>
	<script type="text/javascript">
	
	var url = "/axis/FreecivWebService.jws";
	
	
	// DEMO 1
	function HelloWorld()
	{
		var pl = new SOAPClientParameters();
		pl.add("input", "Andreas2!!!");
		SOAPClient.invoke(url, "getFreecivMessages", pl, true, HelloWorld_callBack);
	}
	function HelloWorld_callBack(r)
	{
		alert(r);
	}
	

	</script>
</head>
<body onload="HelloWorld()">
TEST....
</body>
</html>
