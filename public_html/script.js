// Define our global variables
var GoogleMap     = null;
var CenterLat     = 35.21928;
var CenterLon     = -80.94406;
var ZoomLvl       = 9;
var Planes        = {};
var PlanesOnMap   = 0;
var PlanesOnTable = 0;
var PlanesToReap  = 0;
var SelectedPlane = null;
var planeObject   = null;

var iSortCol=-1;
var bSortASC=true;
var bDefaultSortASC=true;
var iDefaultSortCol=3;

planeObject = {
	oldlat		: null,
	oldlon		: null,
	oldalt		: null,

	// Basic location information
	altitude	: null,
	speed		: null,
	track		: null,
	latitude	: null,
	longitude	: null,
	
	// Info about the plane
	flight		: null,
	squawk		: null,
	icao		: null,	

	// Data packet numbers
	messages	: null,
	seen		: null,

	// Vaild...
	vPosition	: false,
	vTrack		: false,

	// GMap Details
	marker		: null,
	lines		: [],
	trackdata	: new Array(),
	trackline	: new Array(),

	// When was this last updated?
	updated		: null,
	reapable	: false,

	// Appends data to the running track so we can get a visual tail on the plane
	// Only useful for a long running browser session.
	funcAddToTrack	: function(){
			// TODO: Write this function out
			this.trackdata.push([this.latitude, this.longitude, this.altitude, this.track, this.speed]);
			this.trackline.push(new google.maps.LatLng(this.latitude, this.longitude));
		},

	// This is to remove the line from the screen if we deselect the plane
	funcClearLine	: function() {
			console.log("Clearing line for: " + this.icao);
			if (this.line) {
				this.line.setMap(null);
				this.line = null;
			}
		},

	// Should create an icon for us to use on the map...
	funcGetIcon	: function() {
			var selected = false;
			var r = 255, g = 255, b = 0;

			return {
				strokeWeight: (selected ? 2 : 1),
				path: google.maps.SymbolPath.FORWARD_CLOSED_ARROW,
				scale: 5,
				fillColor: 'rgb('+r+','+g+','+b+')',
				fillOpacity: 0.9,
				rotation: this.track
			};
		},

	// TODO: Trigger actions of a selecting a plane
	funcSelectPlane	: function(selectedPlane){
			selectPlaneByHex(this.icao);
		},

	// Update our data
	funcUpdateData	: function(data){
			// So we can find out if we moved
			var oldlat 	= this.latitude;
			var oldlon	= this.longitude;
			var oldalt	= this.altitude;

			// Update all of our data
			this.updated	= new Date().getTime();
			this.altitude	= data.altitude;
			this.speed	= data.speed;
			this.track	= data.track;
			this.latitude	= data.lat;
			this.longitude	= data.lon;
			this.flight	= data.flight;
			this.squawk	= data.squawk;
			this.icao	= data.hex;
			this.messages	= data.messages;
			this.seen	= data.seen;

			// If no packet in over 58 seconds, consider the plane reapable
			// This way we can hold it, but not show it just in case the plane comes back
			if (this.seen > 58) {
				this.reapable = true;
				if (this.marker) {
					this.marker.setMap(null);
					this.marker = null;
				}
				if (this.line) {
					this.line.setMap(null);
					this.line = null;
				}
				if (SelectedPlane == this.icao) {
					SelectedPlane = null;
				}
			} else {
				if (this.reapable == true) {
					console.log(this.icao + ' has come back into range before the reaper!');
				}
				this.reapable = false;
			}

			// Is the position valid?
			if ((data.validposition == 1) && (this.reapable == false)) {
				this.vPosition = true;

				// Detech if the plane has moved
				changeLat = false;
				changeLon = false;
				changeAlt = false;
				if (oldlat != this.latitude) {
					changeLat = true;
				}
				if (oldlon != this.longitude) {
					changeLon = true;
				}
				if (oldalt != this.altitude) {
					changeAlt = true;
				}
				// Right now we only care about lat/long, if alt is updated only, oh well
				if ((changeLat == true) || (changeLon == true)) {
					this.funcAddToTrack();
					if (this.icao == SelectedPlane) {
						this.line = this.funcUpdateLines();
					}
				}
				this.marker = this.funcUpdateMarker();
				PlanesOnMap++;
			} else {
				this.vPosition = false;
			}

			// Do we have a valid track for the plane?
			if (data.validtrack == 1)
				this.vTrack = true;
			else
				this.vTrack = false;
		},

	// Update our marker on the map
	funcUpdateMarker: function() {
			if (this.marker) {
				this.marker.setPosition(new google.maps.LatLng(this.latitude, this.longitude));
				this.marker.setIcon(this.funcGetIcon());
			} else {
				this.marker = new google.maps.Marker({
					position: new google.maps.LatLng(this.latitude, this.longitude),
					map: GoogleMap,
					icon: this.funcGetIcon(),
					visable: true,
				});

				// This is so we can match icao address
				this.marker.icao = this.icao;

				// Trap clicks for this marker.
				google.maps.event.addListener(this.marker, 'click', this.funcSelectPlane);
			}

			// Setting the marker title
			if (this.flight.length == 0) {
				this.marker.setTitle(this.hex);
			} else {
				this.marker.setTitle(this.flight+' ('+this.icao+')');
			}
			return this.marker;
		},

	// Update our planes tail line,
	// TODO: Make this multi colored based on options
	//		altitude (default) or speed
	funcUpdateLines: function() {
			if (this.line) {
				var path = this.line.getPath();
				path.push(new google.maps.LatLng(this.latitude, this.longitude));
			} else {
				console.log("Starting new line");
				this.line = new google.maps.Polyline({
					strokeColor: '#000000',
					strokeOpacity: 1.0,
					strokeWeight: 3,
					map: GoogleMap,
					path: this.trackline,
				});
			}
			return this.line;
		},
};

function fetchData() {
	$.getJSON('/dump1090/data.json', function(data) {
		PlanesOnMap = 0
		
		// Loop through all the planes in the data packet
		for (var j=0; j < data.length; j++) {
			// Do we already have this plane object in Planes?
			// If not make it.
			if (Planes[data[j].hex]) {
				var plane = Planes[data[j].hex];
			} else {
				var plane = jQuery.extend(true, {}, planeObject);
			}

			// Call the function update
			plane.funcUpdateData(data[j]);
			
			// Copy the plane into Planes
			Planes[plane.icao] = plane;
		}

		PlanesOnTable = data.length;
	});
}

// Initalizes the map and starts up our timers to call various functions
function initialize() {
	// Make a list of all the available map IDs
	var mapTypeIds = [];
	for(var type in google.maps.MapTypeId) {
		mapTypeIds.push(google.maps.MapTypeId[type]);
	}
	// Push OSM on to the end
	mapTypeIds.push("OSM");
	mapTypeIds.push("dark_map");

	// Styled Map to outline airports and highways
	var styles = [
		{
			"featureType": "administrative",
			"stylers": [
				{ "visibility": "off" }
			]
		},{
			"featureType": "landscape",
			"stylers": [
				{ "visibility": "off" }
			]
		},{
			"featureType": "poi",
			"stylers": [
				{ "visibility": "off" }
			]
		},{
			"featureType": "road",
			"stylers": [
				{ "visibility": "off" }
			]
		},{
			"featureType": "transit",
			"stylers": [
				{ "visibility": "off" }
			]
		},{
			"featureType": "landscape",
			"stylers": [
				{ "visibility": "on" },
				{ "weight": 8 },
				{ "color": "#000000" }
			]
		},{
			"featureType": "water",
			"stylers": [
			{ "lightness": -74 }
			]
		},{
			"featureType": "transit.station.airport",
			"stylers": [
				{ "visibility": "on" },
				{ "weight": 8 },
				{ "invert_lightness": true },
				{ "lightness": 27 }
			]
		},{
			"featureType": "road.highway",
			"stylers": [
				{ "visibility": "simplified" },
				{ "invert_lightness": true },
				{ "gamma": 0.3 }
			]
		},{
			"featureType": "road",
			"elementType": "labels",
			"stylers": [
				{ "visibility": "off" }
			]
		}
	]

	// Add our styled map
	var styledMap = new google.maps.StyledMapType(styles, {name: "Dark Map"});

	// Define the Google Map
	var mapOptions = {
		center: new google.maps.LatLng(CenterLat, CenterLon),
		zoom: ZoomLvl,
		mapTypeId: google.maps.MapTypeId.ROADMAP,
		mapTypeControlOptions: {
			mapTypeIds: mapTypeIds,
		}
	};

	GoogleMap = new google.maps.Map(document.getElementById("map_canvas"), mapOptions);

	//Define OSM map type pointing at the OpenStreetMap tile server
	GoogleMap.mapTypes.set("OSM", new google.maps.ImageMapType({
		getTileUrl: function(coord, zoom) {
			return "http://tile.openstreetmap.org/" + zoom + "/" + coord.x + "/" + coord.y + ".png";
		},
		tileSize: new google.maps.Size(256, 256),
		name: "OpenStreetMap",
		maxZoom: 18
	}));

	GoogleMap.mapTypes.set("dark_map", styledMap);

	// Setup our timer to poll from the server.
	window.setInterval(function() {
		fetchData();
		refreshTableInfo();
		refreshSelected()
		reaper();
	}, 1000);

	// Faster timer, smoother things
	//window.setInterval(function() {
	//	printTime();
	//}, 250);
}

// This looks for planes to reap out of the master Planes variable
function reaper() {
	PlanesToReap = 0;
	// When did the reaper start?
	reaptime = new Date().getTime();
	// Loop the planes
	for (var reap in Planes) {
		// Is this plane possibly reapable?
		if (Planes[reap].reapable == true) {
			// Has it not been seen for 5 minutes?
			// This way we still have it if it returns before then
			// Due to loss of signal or other reasons
			if ((reaptime - Planes[reap].updated) > 300000) {
				// Reap it.
				delete Planes[reap];
			}
			PlanesToReap++;
		}
	};
} 

// Refresh the detail window about the plane
function refreshSelected() {
	if ((SelectedPlane != "ICAO") && (SelectedPlane != null)) {
		var selected = Planes[SelectedPlane];
		if (selected.flight == "") {
			selected.flight="N/A (" + selected.icao + ")";
		}
		var html = '<table id="selectedinfo" width="100%">';
		html += '<tr><td colspan="2" id="selectedinfotitle"><b>' + selected.flight + '</b><td></tr>';
		// Lets hope we never see this... Aircraft Hijacking
		if (selected.squawk == 7500) {
			html += '<tr><td colspan="2"id="selectedinfotitle">Squawking: Aircraft Hijacking</td>'
		}
		// Radio Failure
		if (selected.squawk == 7600) {
			html += '<tr><td colspan="2" id="selectedinfotitle">Squawking: Communication Loss (Radio Failure)</td>'
		}
		// Emergancy
		if (selected.squawk == 7700) {
			html += '<tr><td colspan="2" id="selectedinfotitle">Squawking: Emergancy</td>'
		}
		html += '<tr><td>Altitude: ' + selected.altitude + '</td><td>Squawk: ' + selected.squawk + '</td></tr>';
		html += '<tr><td>Track: ' + selected.track + ' (' + normalizeTrack(selected.track, selected.vTrack)[1] +')</td><td>ICAO (hex): ' + selected.icao + '</td></tr>';
		html += '<tr><td colspan="2" align="center">Lat/Long: ' + selected.latitude + ', ' + selected.longitude + '</td></tr>';
		html += '</table>';
	} else {
		var html = '';
	}
	document.getElementById('plane_detail').innerHTML = html;
}

// Right now we have no means to validate the speed is good
// Want to return (n/a) when we dont have it
// TODO: Edit C code to add a valid speed flag
// TODO: Edit js code to use said flag
function normalizeSpeed(speed, valid) {
	return speed	
}

// Returns back a long string, short string, and the track if we have a vaild track path
function normalizeTrack(track, valid){
	x = []
	if ((track > -1) && (track < 22.5)) {
		x = ["North", "N", track]
	}
	if ((track > 22.5) && (track < 67.5)) {
		x = ["North East", "NE", track]
	}
	if ((track > 67.5) && (track < 112.5)) {
		x = ["East", "E", track]
	}
	if ((track > 112.5) && (track < 157.5)) {
		x = ["South East", "SE", track]
	}
	if ((track > 157.5) && (track < 202.5)) {
		x = ["South", "S", track]
	}
	if ((track > 202.5) && (track < 247.5)) {
		x = ["South West", "SW", track]
	}
	if ((track > 247.5) && (track < 292.5)) {
		x = ["West", "W", track]
	}
	if ((track > 292.5) && (track < 337.5)) {
		x = ["North West", "NW", track]
	}
	if ((track > 337.5) && (track < 361)) {
		x = ["North", "N", track]
	}
	if (!valid) {
		x = [" ", "n/a", ""]
	}
	return x
}

// Refeshes the larger table of all the planes
function refreshTableInfo() {
	var html = '<table id="tableinfo" width="100%">';
	html += '<thead style="background-color: #BBBBBB; cursor: pointer;">';
	html += '<td onclick="setASC_DESC(\'0\');sortTable(\'tableinfo\',\'0\');">ICAO</td>';
	html += '<td onclick="setASC_DESC(\'1\');sortTable(\'tableinfo\',\'1\');">Flight</td>';
	html += '<td onclick="setASC_DESC(\'2\');sortTable(\'tableinfo\',\'2\');" align="right">Squawk</td>';
	html += '<td onclick="setASC_DESC(\'3\');sortTable(\'tableinfo\',\'3\');" align="right">Altitude</td>';
	html += '<td onclick="setASC_DESC(\'4\');sortTable(\'tableinfo\',\'4\');" align="right">Speed</td>';
	html += '<td onclick="setASC_DESC(\'5\');sortTable(\'tableinfo\',\'5\');" align="right">Track</td>';
	html += '<td onclick="setASC_DESC(\'6\');sortTable(\'tableinfo\',\'6\');" align="right">Msgs</td>';
	html += '<td onclick="setASC_DESC(\'7\');sortTable(\'tableinfo\',\'7\');" align="right">Seen</td></thead><tbody>';
	for (var tablep in Planes) {
		var tableplane = Planes[tablep]
		if (!tableplane.reapable) {
			var specialStyle = "";
			// Is this the plane we selected?
			if (tableplane.icao == SelectedPlane) {
				specialStyle += " selected";
			}
			// Lets hope we never see this... Aircraft Hijacking
			if (tableplane.squawk == 7500) {
				specialStyle += " squawk7500";
			}
			// Radio Failure
			if (tableplane.squawk == 7600) {
				specialStyle += " squawk7600";
			}
			// Emergancy
			if (tableplane.squawk == 7700) {
				specialStyle += " squawk7700";
			}
			if (tableplane.vPosition == true)
				html += '<tr class="plane_table_row vPosition' + specialStyle + '">';
			else
				html += '<tr class="plane_table_row ' + specialStyle + '">';
			html += '<td>' + tableplane.icao + '</td>';
			html += '<td>' + tableplane.flight + '</td>';
			html += '<td align="right">' + tableplane.squawk + '</td>';
			html += '<td align="right">' + tableplane.altitude + '</td>';
			html += '<td align="right">' + tableplane.speed + '</td>';
			html += '<td align="right">' + normalizeTrack(tableplane.track, tableplane.vTrack)[2] + ' (' + normalizeTrack(tableplane.track, tableplane.vTrack)[1] + ')</td>';
			html += '<td align="right">' + tableplane.messages + '</td>';
			html += '<td align="right">' + tableplane.seen + '</td>';
			html += '</tr>';
		}
	}
	html += '</tbody></table>';

	document.getElementById('planes_table').innerHTML = html;

	// Click event for table
	$('#planes_table').find('tr').click( function(){
		var hex = $(this).find('td:first').text();
		if (hex != "ICAO") {
			selectPlaneByHex(hex);
			refreshTableInfo()
			refreshSelected()
		}
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

function selectPlaneByHex(hex) {
	if (SelectedPlane != null) {
		Planes[SelectedPlane].funcClearLine();
	}
	SelectedPlane = hex;
	if (Planes[SelectedPlane].marker) {
		Planes[SelectedPlane].funcUpdateLines();
	}

}
