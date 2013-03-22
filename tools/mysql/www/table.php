<?php

$con = mysql_connect("192.168.2.100","pi","raspberry");

if (!$con)
  {
  die('Could not connect: ' . mysql_error());
  }

mysql_select_db("dump1090", $con);

// this query gives us the live flight table. latest entries are on top

$result = mysql_query("SELECT * FROM flights ORDER by last_update DESC LIMIT 25");

//html formatted output
echo "<table><tr><th>Country</th><th>Airline</th><th>Flight</th><th>Squawk</th><th>ICAO</th><th>Lat</th><th>Lon</th><th>Alt</th><th>VR</th><th>Heading</th><th>Speed</th><th>DF</th><th>Msgs</th><th>last updated</th></tr>";
while ($row = mysql_fetch_array($result))
        {
        
        echo "<tr style=background-color:#" . $row['icao'] . ">";
        echo "<td style=background-color:#000000>" . $row['country'] . "</td>";
	echo "<td style=background-color:#000000>" . "<img src=\"images/opflags/" . $row['airline'] . ".bmp\"</td>";
	echo "<td>" . $row['flight'] . "</td>";
        echo "<td>" . $row['squawk'] . "</td>";
        echo "<td>" . $row['icao'] . "</td>";
        echo "<td>" . $row['lat'] . "</td>";
        echo "<td>" . $row['lon'] . "</td>";
        echo "<td>" . $row['alt'] . "</td>";
        echo "<td>" . $row['vr'] . "</td>";
        echo "<td>" . $row['heading'] . "</td>";
        echo "<td>" . $row['speed'] . "</td>";
        echo "<td>" . $row['df'] . "</td>";
        echo "<td>" . $row['msgs'] . "</td>";
        echo "<td>" . $row['last_update'] . "</td>";
	echo "</tr>";
}


echo "</table>";

mysql_close($con);
?>

