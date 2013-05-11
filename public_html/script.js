Map       = null;
CenterLat = 45.0;
CenterLon = 9.0;
ZoomLvl   = 5;
Planes    = {};
PlanesOnMap  = 0;
PlanesOnGrid = 0;
Selected     = null;

if (localStorage['CenterLat']) { CenterLat = Number(localStorage['CenterLat']); }
if (localStorage['CenterLon']) { CenterLon = Number(localStorage['CenterLon']); }
if (localStorage['ZoomLvl'])   { ZoomLvl   = Number(localStorage['ZoomLvl']); }

function getIconForPlane(plane) {
    var r = 255, g = 255, b = 0;
    var maxalt = 40000; // Max altitude in the average case
    var invalt = maxalt-plane.altitude;
    var selected = (Selected == plane.hex);

    if (invalt < 0) invalt = 0;
    b = parseInt(255/maxalt*invalt);
    return {
        strokeWeight: (selected ? 2 : 1),
        path: google.maps.SymbolPath.FORWARD_CLOSED_ARROW,
        scale: 5,
        fillColor: 'rgb('+r+','+g+','+b+')',
        fillOpacity: 0.9,
        rotation: plane.track
    };
}

function selectPlane(selectedPlane) {
    if (selectedPlane.length) this.planehex = selectedPlane;
    if (!Planes[this.planehex]) return;
    var old = Selected;
    Selected = this.planehex;
    if (Planes[old]) {
        /* Remove the highlight in the previously selected plane. */
        Planes[old].marker.setIcon(getIconForPlane(Planes[old]));
    }
    Planes[Selected].marker.setIcon(getIconForPlane(Planes[Selected]));
    
    refreshSelectedInfo();
    refreshTableInfo();
}

function refreshGeneralInfo() {
    var i = document.getElementById('geninfo');

    i.innerHTML  = PlanesOnGrid + ' planes on grid.<br>';
    i.innerHTML += PlanesOnMap + ' planes on map.';
}

function refreshSelectedInfo() {
    var i = document.getElementById('selinfo');
    var p = Planes[Selected];
    
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
    html += '<tr><td colspan=2><b>'+p.flight+'&nbsp;</b></td></tr>';
    html += '<tr><td>ICAO:</td><td>'+p.hex+'</td></tr>';
    html += '<tr><td>Squawk:</td><td>'+p.squawk+'</td></tr>';
    html += '<tr><td>Altitude:</td><td>'+p.altitude+' feet</td></tr>';
    html += '<tr><td>Speed:</td><td>'+p.speed+' knots</td></tr>';
    html += '<tr><td>Coordinates:</td><td>'+p.lat+', '+p.lon+'</td></tr>';
    html += '<tr><td>Messages:</td><td>'+p.messages+'</td></tr>';
    html += '<tr><td>Seen:</td><td>'+p.seen+' sec</td></tr>';
    html += '</table>';
    i.innerHTML = html;
}

function refreshTableInfo() {
    var i = document.getElementById('tabinfo');

    var html = '<table id="tableinfo" width="100%">';
    html += '<thead style="background-color: #CCCCCC;">';
    html += '<td>Flight</td><td>Squawk</td><td align="right">Altitude</td>';
    html += '<td align="center">Speed</td><td align="center">Track</td><td>Seen</td>';
    html += '<td>Msgs</td></thead>';
    for (var p in Planes) {
        if (p == Selected) {
            html += '<tr style="background-color: #E0E0E0;">';
        } else {
            html += '<tr id="tableinforow" onClick="selectPlane(\''+p+'\');">';
        }
        html += '<td>' + Planes[p].flight + '</td>';
        html += '<td align="right">' + Planes[p].squawk + '</td>';
        html += '<td align="right">' + Planes[p].altitude + '</td>';
        html += '<td align="right">' + Planes[p].speed + '</td>';
        html += '<td align="right">' + Planes[p].track + '</td>';
        html += '<td align="right">' + Planes[p].seen + '</td>';
        html += '<td align="right">' + Planes[p].messages + '</td>';
        html += '</tr>';
    }
    html += '</table>';
    i.innerHTML = html;
}

function fetchData() {
    $.getJSON('data.json', function(data) {
        var stillhere = {}
        PlanesOnMap = 0;
        
        for (var j=0; j < data.length; j++) {
            var plane = data[j];
            stillhere[plane.hex] = true;
            plane.flight = $.trim(plane.flight);
            
            // Show only planes with position
            if (plane.validposition == 1) {
                var marker = null;
                PlanesOnMap++;
                
                if (Planes[plane.hex]) {
                    // Move and refresh old plane on map
                    var myplane = Planes[plane.hex];
                    marker = myplane.marker;
                    var icon = marker.getIcon();
                    var newpos = new google.maps.LatLng(plane.lat, plane.lon);
                    marker.setPosition(newpos);
                    marker.setIcon(getIconForPlane(plane));
                    myplane.altitude = plane.altitude;
                    myplane.speed = plane.speed;
                    myplane.lat = plane.lat;
                    myplane.lon = plane.lon;
                    myplane.track = plane.track;
                    myplane.flight = plane.flight;
                    myplane.seen = plane.seen;
                    myplane.squawk = plane.squawk;
                    myplane.messages = plane.messages;
                    if (myplane.hex == Selected)
                        refreshSelectedInfo();
                } else {
                    // Add new plane to map
                    marker = new google.maps.Marker({
                        position: new google.maps.LatLng(plane.lat, plane.lon),
                        map: Map,
                        icon: getIconForPlane(plane)
                    });
                    plane.marker = marker;
                    marker.planehex = plane.hex;
                    Planes[plane.hex] = plane;

                    // Trap clicks for this marker.
                    google.maps.event.addListener(marker, 'click', selectPlane);
                }
                
                if (plane.flight.length == 0) {
                    marker.setTitle(plane.hex)
                } else {
                    marker.setTitle(plane.flight+' ('+plane.hex+')')
                }
            }
        }

        PlanesOnGrid = data.length;
        
        /* Remove idle planes. */
        for (var p in Planes) {
            if (!stillhere[p]) {
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

function resetMap() {
    localStorage['CenterLat'] = 45.0;
    localStorage['CenterLon'] = 9.0;
    localStorage['ZoomLvl']   = 5;
    Map.setZoom(parseInt(localStorage['ZoomLvl']));
    Map.setCenter(new google.maps.LatLng(parseInt(localStorage['CenterLat']), parseInt(localStorage['CenterLon'])));
    Selected = null;
    document.getElementById('selinfo').innerHTML = '';
}

function resizeMap() {
    var windWidth = parseInt($(window).width());
    var infoWidth = parseInt($('#info').width());
    var mapWidth = windWidth - infoWidth;
    $('#map_canvas').css('width', mapWidth);
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
    
    $(window).resize(function(e){
        resizeMap();
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
        selectPlane("xyzxyz"); // Select not valid ICAO24 address to clear selection.
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
    resizeMap();
}
