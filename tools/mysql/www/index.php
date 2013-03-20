<html>
	<head>
		<title>dump1090 live traffic</title>
      		<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
                <meta charset="UTF-8">
                <meta name="author" content="dl6kbg" />
                <meta name="description" content="dump1090 live traffic" />
		<link rel="stylesheet" type="text/css" media="all" href="style.css" />

		<script type="text/javascript" src="http://ajax.googleapis.com/ajax/libs/jquery/1.3.0/jquery.min.js"></script>
		<script type="text/javascript">
			var auto_refresh = setInterval(
				function () {
					$('#flights').load('table.php').fadeIn("slow");
				}, 1000); // refresh every 1000 milliseconds
		</script>

	</head>

<body>
<div id="flights"> </div>
</body>

</html>
