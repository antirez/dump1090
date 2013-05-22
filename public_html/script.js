var Map       = null;
var CenterLat = 45.0;
var CenterLon = 9.0;
var ZoomLvl   = 5;
var Planes    = {};
var PlanesOnMap  = 0;
var PlanesOnGrid = 0;
var Selected     = null;

var iSortCol=-1;
var bSortASC=true;
var bDefaultSortASC=true;
var iDefaultSortCol=3;

if (localStorage['CenterLat']) { CenterLat = Number(localStorage['CenterLat']); }
if (localStorage['CenterLon']) { CenterLon = Number(localStorage['CenterLon']); }
if (localStorage['ZoomLvl'])   { ZoomLvl   = Number(localStorage['ZoomLvl']); }

function getIconForPlane(plane, deselect) {
    var selected = false;
    var track = 0;
    var r = 255, g = 255, b = 0;
    var maxalt = 40000; // Max altitude in the average case
    var invalt = 0;
    
    // If there is plane object
    if (plane) {
        invalt = maxalt-plane.altitude;
        if (Selected == plane.hex && !deselect) {
            selected = true;
        }
        track = plane.track;
    }
    
    if (invalt < 0) invalt = 0;
    b = parseInt(255/maxalt*invalt);
    
    return {
        strokeWeight: (selected ? 2 : 1),
        path: google.maps.SymbolPath.FORWARD_CLOSED_ARROW,
        scale: 5,
        fillColor: 'rgb('+r+','+g+','+b+')',
        fillOpacity: 0.9,
        rotation: track
    };
}

/* Gets hex code of selected plane as string or nothing.     *
 * Select not valid ICAO24 (hex) address to clear selection. */
function selectPlane(selectedPlane) {
    if (selectedPlane.length) this.planehex = selectedPlane;
    
    // Deselect planes
    if (!Planes[this.planehex]) {
        if (Planes[Selected].marker) {
            Planes[Selected].marker.setIcon(getIconForPlane(Planes[Selected], true));
        }
        Selected = null;
        refreshSelectedInfo();
        refreshTableInfo();
        return;
    }
    
    var old = Selected;
    Selected = this.planehex;
    
    if (Planes[old] && Planes[old].validposition) {
        // Remove the highlight in the previously selected plane.
        Planes[old].marker.setIcon(getIconForPlane(Planes[old]));
    }
    
    if (Planes[Selected].validposition) {
        Planes[Selected].marker.setIcon(getIconForPlane(Planes[Selected]));
    }
    
    refreshSelectedInfo();
    refreshTableInfo();
}

function refreshGeneralInfo() {
    var i = document.getElementById('geninfo');

    i.innerHTML  = PlanesOnMap + ' planes on the map.  ';
    i.innerHTML += PlanesOnGrid + ' planes on the grid.';
}

function refreshSelectedInfo() {
    var i = document.getElementById('selinfo');
    var p = Planes[Selected];
    
    // If no plane is selected
    if (!p) {
        p = {};
        p.flight = "";
        p.hex = "";
        p.squawk = "";
        p.altitude = "0";
        p.speed = "0";
        p.lat = "lat";
        p.lon = "lon";
        p.messages = "0";
        p.seen = "0";
    }
    
    var html = '<table id="selectedinfo">';
    if (p.flight != "") {
        html += '<tr><td colspan=2><b>'+p.flight+'&nbsp;&nbsp;</b>';
        html += '[<a href="http://www.flightstats.com/go/FlightStatus/flightStatusByFlight.do?';
        html += 'flightNumber='+p.flight+'" target="_blank">FlightStats</a>]</td></tr>';
    } else {
        html += '<tr><td colspan=2><b>&nbsp;</b></td></tr>';
    }
    html += '<tr><td>ICAO:</td><td>'+p.hex+'</td></tr>';
    if (p.squawk != "0000") {
        html += '<tr><td>Squawk:</td><td>'+p.squawk+'</td></tr>';
    } else {
        html += '<tr><td>Squawk:</td><td>n/a</td></tr>';
    }
    html += '<tr><td>Altitude:</td><td>'+p.altitude+' feet</td></tr>';
    html += '<tr><td>Speed:</td><td>'+p.speed+' knots</td></tr>';
    if (p.validposition) {
        html += '<tr><td>Coordinates:</td><td>'+p.lat+', '+p.lon+'</td></tr>';
    } else {
        html += '<tr><td>Coordinates:</td><td>n/a</td></tr>';
    }
    html += '<tr><td>Messages:</td><td>'+p.messages+'</td></tr>';
    html += '<tr><td>Seen:</td><td>'+p.seen+' sec</td></tr>';
    html += '</table>';
    i.innerHTML = html;
}

function refreshTableInfo() {
	var html = '<table id="tableinfo" width="100%">';
	html += '<thead style="background-color: #CCCCCC; cursor: pointer;">';
	html += '<td onclick="setASC_DESC(\'0\');sortTable(\'tableinfo\',\'0\');">hex</td>';
	html += '<td onclick="setASC_DESC(\'1\');sortTable(\'tableinfo\',\'1\');">Flight</td>';
	html += '<td onclick="setASC_DESC(\'2\');sortTable(\'tableinfo\',\'2\');" align="right">Squawk</td>';
	html += '<td onclick="setASC_DESC(\'3\');sortTable(\'tableinfo\',\'3\');" align="right">Altitude</td>';
	html += '<td onclick="setASC_DESC(\'4\');sortTable(\'tableinfo\',\'4\');" align="right">Speed</td>';
	html += '<td onclick="setASC_DESC(\'5\');sortTable(\'tableinfo\',\'5\');" align="right">Track</td>';
	html += '<td onclick="setASC_DESC(\'6\');sortTable(\'tableinfo\',\'6\');" align="right">Msgs</td>';
	html += '<td onclick="setASC_DESC(\'7\');sortTable(\'tableinfo\',\'7\');" align="right">Seen</td></thead>';
	for (var p in Planes) {
		var specialStyle = "";
		if (p == Selected) {
			html += '<tr id="tableinforow" style="background-color: #E0E0E0;">';
		} else {
			html += '<tr id="tableinforow">';
		}
		if (Planes[p].validposition) {
			specialStyle = 'bold';
		}
		html += '<td class="' + specialStyle + '">' + Planes[p].hex + '</td>';
		html += '<td class="' + specialStyle + '">' + Planes[p].flight + '</td>';
		html += '<td class="' + specialStyle + '" align="right">' + Planes[p].squawk + '</td>';
		html += '<td class="' + specialStyle + '" align="right">' + Planes[p].altitude + '</td>';
		html += '<td class="' + specialStyle + '" align="right">' + Planes[p].speed + '</td>';
		html += '<td class="' + specialStyle + '" align="right">' + Planes[p].track + '</td>';
		html += '<td class="' + specialStyle + '" align="right">' + Planes[p].messages + '</td>';
		html += '<td class="' + specialStyle + '" align="right">' + Planes[p].seen + '</td>';
		html += '</tr>';
	}
	html += '</table>';

	document.getElementById('tabinfo').innerHTML = html;

	// Click event for table - lags sometimes for some reason?
	$('#tableinfo').find('tr').click( function(){
		var hex = $(this).find('td:first').text();
		selectPlane(hex);
	});

	sortTable("tableinfo");
}

// Credit goes to a co-worker that needed a similar functions for something else
// we get a copy of it free ;)
function setASC_DESC(iCol) {
	if(iSortCol==iCol) {
		bSortASC=!bSortASC;
	} else {
		bSortASC=bDefaultSortASC;
	}
}

function sortTable(szTableID,iCol) { 
	//if iCol was not provided, and iSortCol is not set, assign default value
	if (typeof iCol==='undefined'){
		if(iSortCol!=-1){
			var iCol=iSortCol;
		} else {
			var iCol=iDefaultSortCol;
		}
	}

	//retrieve passed table element
	var oTbl=document.getElementById(szTableID).tBodies[0];
	var aStore=[];

	//If supplied col # is greater than the actual number of cols, set sel col = to last col
	if (oTbl.rows[0].cells.length<=iCol)
		iCol=(oTbl.rows[0].cells.length-1);

	//store the col #
	iSortCol=iCol;

	//determine if we are delaing with numerical, or alphanumeric content
	bNumeric=!isNaN(parseFloat(oTbl.rows[0].cells[iSortCol].textContent||oTbl.rows[0].cells[iSortCol].innerText))?true:false;

	//loop through the rows, storing each one inro aStore
	for (var i=0,iLen=oTbl.rows.length;i<iLen;i++){
		var oRow=oTbl.rows[i];
		vColData=bNumeric?parseFloat(oRow.cells[iSortCol].textContent||oRow.cells[iSortCol].innerText):String(oRow.cells[iSortCol].textContent||oRow.cells[iSortCol].innerText);
		aStore.push([vColData,oRow]);
	}

	//sort aStore ASC/DESC based on value of bSortASC
	if(bNumeric){//numerical sort
		aStore.sort(function(x,y){return bSortASC?x[0]-y[0]:y[0]-x[0];});
	}else{//alpha sort
		aStore.sort();
		if(!bSortASC)
			aStore.reverse();
	}

	//rewrite the table rows to the passed table element
	for(var i=0,iLen=aStore.length;i<iLen;i++){
		oTbl.appendChild(aStore[i][1]);
	}
	aStore=null;
}

function fetchData() {
	$.getJSON('data.json', function(data) {
		// Planes that are still with us, and set map count to 0
		var stillhere = {}
		PlanesOnMap = 0;

		// Loop through all the planes in the data packet
		for (var j=0; j < data.length; j++) {

			// Set plane to be this particular plane in the data set
			var plane = data[j];
			// Add to the still here list
			stillhere[plane.hex] = true;
			plane.flight = $.trim(plane.flight);

			// Set the marker to null, for now
			var marker = null;

			// Either update the data or add it
			if (Planes[plane.hex]) {
				// Declare our plane that we are working with from our old data set
				var myplane = Planes[plane.hex];

				// If the has valid position, we should make a marker for it
				if (plane.validposition) {
					if (myplane.marker != null) {
						marker = myplane.marker;
						var icon = marker.getIcon();
						var newpos = new google.maps.LatLng(plane.lat, plane.lon);
						marker.setPosition(newpos);
						marker.setIcon(getIconForPlane(plane));
						PlanesOnMap++;
					} else {
						// Add new plane to map, dont ask me why this is needed here now...
						marker = new google.maps.Marker({
							position: new google.maps.LatLng(plane.lat, plane.lon),
							map: Map,
							icon: getIconForPlane(plane)
						});
						myplane.marker = marker;
						marker.planehex = plane.hex;
						PlanesOnMap++;

						// Trap clicks for this marker.
						google.maps.event.addListener(marker, 'click', selectPlane);
					}
				}

				// Update all the other information
				myplane.altitude = plane.altitude;
				myplane.speed = plane.speed;
				myplane.lat = plane.lat;
				myplane.lon = plane.lon;
				myplane.track = plane.track;
				myplane.flight = plane.flight;
				myplane.seen = plane.seen;
				myplane.messages = plane.messages;
				myplane.squawk = plane.squawk;
				myplane.validposition = plane.validposition;
				myplane.validtrack = plane.validtrack;

				// If this is a selected plane, refresh its data outside of the table
				if (myplane.hex == Selected)
					refreshSelectedInfo();
			} else {
				// This is a new plane
				// Do we have a lat/long that is not 0?
				if (plane.validposition) {
					// Add new plane to map
					marker = new google.maps.Marker({
						position: new google.maps.LatLng(plane.lat, plane.lon),
						map: Map,
						icon: getIconForPlane(plane)
					});
					plane.marker = marker;
					marker.planehex = plane.hex;
					PlanesOnMap++;

					// Trap clicks for this marker.
					google.maps.event.addListener(marker, 'click', selectPlane);
				}

				// Copy the plane into Planes
				Planes[plane.hex] = plane;
			}

			// If we have lat/long, we must have a marker, so lets set the marker title
			if (plane.validposition) {
				if (plane.flight.length == 0) {
					marker.setTitle(plane.hex)
				} else {
					marker.setTitle(plane.flight+' ('+plane.hex+')')
				}
			}

		}

		// How many planes have we heard from?
		PlanesOnGrid = data.length;

		/* Remove idle planes. */
		for (var p in Planes) {
			if (!stillhere[p]) {
				if (Planes[p].marker != null)
					Planes[p].marker.setMap(null);
				delete Planes[p];
			}
		}

		refreshTableInfo();
		refreshSelectedInfo();
	});
}

function checkTime(i) {
    if (i < 10) {
        return "0" + i;
    }
    return i;
}

function printTime() {
    var currentTime = new Date();
    var hours = checkTime(currentTime.getUTCHours());
    var minutes = checkTime(currentTime.getUTCMinutes());
    var seconds = checkTime(currentTime.getUTCSeconds());
    
    if (document.getElementById) {
        document.getElementById('utcTime').innerHTML =
            hours + ":" + minutes + ":" + seconds;
    }
}

function placeSettings() {
    // Settings link
    var marginLeft = $('#header').width() - $('#info_settings').width();
    $('#info_settings').css('left', marginLeft);
    $('#info_settings').css('top', parseInt($('#utcTime').offset().top));
    
    // Settings area
    $('#info_settings_area').css('top', parseInt($('#geninfo').offset().top));
    $('#info_settings_area').css('left', 5);
    $('#info_settings_area').css('width', parseInt($('#info').width() - 40));
}

function toggleSettings() {
    if ($('#info_settings_area').css('display') != 'none') {
        $('#info_settings_area').hide(350);
    } else {
        // Open settings
        $('#info_settings_area').show(350);
    }
}

function resetMap() {
    localStorage['CenterLat'] = 45.0;
    localStorage['CenterLon'] = 9.0;
    localStorage['ZoomLvl']   = 5;
    CenterLat = 45.0;
    CenterLon = 9.0;
    ZoomLvl   = 5;
    Map.setZoom(parseInt(localStorage['ZoomLvl']));
    Map.setCenter(new google.maps.LatLng(parseFloat(localStorage['CenterLat']),
            parseFloat(localStorage['CenterLon'])));
    Selected = null;
    refreshSelectedInfo();
}

function initialize() {
    var mapTypeIds = [];
    for(var type in google.maps.MapTypeId) {
        mapTypeIds.push(google.maps.MapTypeId[type]);
    }
    mapTypeIds.push("OSM");

    var mapOptions = {
        center: new google.maps.LatLng(CenterLat, CenterLon),
        zoom: ZoomLvl,
        mapTypeId: google.maps.MapTypeId.ROADMAP,
        mapTypeControlOptions: {
            mapTypeIds: mapTypeIds,
        }
    };
    Map = new google.maps.Map(document.getElementById("map_canvas"), mapOptions);

    //Define OSM map type pointing at the OpenStreetMap tile server
    Map.mapTypes.set("OSM", new google.maps.ImageMapType({
        getTileUrl: function(coord, zoom) {
           return "http://tile.openstreetmap.org/" + zoom + "/" + coord.x + "/" + coord.y + ".png";
        },
        tileSize: new google.maps.Size(256, 256),
        name: "OpenStreetMap",
        maxZoom: 18
    }));
    
    // show settings at info-area
    $(function(){
        $(window).resize(function(e){
            placeSettings();
        });
        placeSettings();
        // hide it before it's positioned
        $('#info_settings').css('display','inline');
    });
    
    // Listener for newly created Map
    google.maps.event.addListener(Map, 'center_changed', function() {
        localStorage['CenterLat'] = Map.getCenter().lat();
        localStorage['CenterLon'] = Map.getCenter().lng();
    });
    
    google.maps.event.addListener(Map, 'zoom_changed', function() {
        localStorage['ZoomLvl']  = Map.getZoom();
    });
    
    google.maps.event.addListener(Map, 'click', function() {
        if (Selected) {
            selectPlane("xyzxyz"); // Select not valid ICAO24 (hex) address to clear selection.
        }
        Selected = null;
        refreshSelectedInfo();
        refreshTableInfo();
    });

    // Setup our timer to poll from the server.
    window.setInterval(function() {
        fetchData();
        refreshGeneralInfo();
    }, 1000);
    
    // Faster timer, smoother things
    window.setInterval(function() {
        printTime();
    }, 250);
    
    refreshGeneralInfo();
    refreshSelectedInfo();
    refreshTableInfo();
}
