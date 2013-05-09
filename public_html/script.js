Map = null;
CenterLat = 45.0;
CenterLon = 9.0;
ZoomLvl   = 5;
Planes={};
NumPlanes = 0;
Selected=null

if (localStorage['CenterLat']) { CenterLat = Number(localStorage['CenterLat']); }
if (localStorage['CenterLon']) { CenterLon = Number(localStorage['CenterLon']); }
if (localStorage['ZoomLvl'])   { ZoomLvl   = Number(localStorage['ZoomLvl']); }

function getIconForPlane(plane) {
    var r = 255, g = 255, b = 0;
    var maxalt = 40000; /* Max altitude in the average case */
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

function selectPlane() {
    if (!Planes[this.planehex]) return;
    var old = Selected;
    Selected = this.planehex;
    if (Planes[old]) {
        /* Remove the highlight in the previously selected plane. */
        Planes[old].marker.setIcon(getIconForPlane(Planes[old]));
    }
    Planes[Selected].marker.setIcon(getIconForPlane(Planes[Selected]));
    refreshSelectedInfo();
}

function refreshGeneralInfo() {
    var i = document.getElementById('geninfo');

    i.innerHTML = NumPlanes+' planes on screen.';
}

function refreshSelectedInfo() {
    var i = document.getElementById('selinfo');
    var p = Planes[Selected];

    if (!p) return;
    var html = 'ICAO: '+p.hex+'<br>';
    if (p.flight.length) {
        html += '<b>'+p.flight+'</b><br>';
    }
    html += 'Altitude: '+p.altitude+' feet<br>';
    html += 'Speed: '+p.speed+' knots<br>';
    html += 'Coordinates: '+p.lat+', '+p.lon+'<br>';
    i.innerHTML = html;
}

function fetchData() {
    $.getJSON('/data.json', function(data) {
        var stillhere = {}
        for (var j=0; j < data.length; j++) {
            var plane = data[j];
            var marker = null;
            stillhere[plane.hex] = true;
            plane.flight = $.trim(plane.flight);

            if (Planes[plane.hex]) {
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
                if (myplane.hex == Selected)
                    refreshSelectedInfo();
            } else {
                marker = new google.maps.Marker({
                    position: new google.maps.LatLng(plane.lat, plane.lon),
                    map: Map,
                    icon: getIconForPlane(plane)
                });
                plane.marker = marker;
                marker.planehex = plane.hex;
                Planes[plane.hex] = plane;

                /* Trap clicks for this marker. */
                google.maps.event.addListener(marker, 'click', selectPlane);
            }
            if (plane.flight.length == 0)
                marker.setTitle(plane.hex)
            else
                marker.setTitle(plane.flight+' ('+plane.hex+')')
        }
        NumPlanes = data.length;

        /* Remove idle planes. */
        for (var p in Planes) {
            if (!stillhere[p]) {
                Planes[p].marker.setMap(null);
                delete Planes[p];
            }
        }
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

function placeFooter() {    
    var windHeight = $(window).height();
    var footerHeight = $('#info_footer').height();
    var offset = parseInt(windHeight) - parseInt(footerHeight);
    
    var footerWidth = parseInt($('#info_footer').width());
    var infoWidth = parseInt($('#info').width());
    var marginLeft = parseInt((infoWidth / 2) - (footerWidth / 2));
    
    $('#info_footer').css('top',offset);
    $('#info_footer').css('margin-left',marginLeft);
}

function initialize() {
    var mapOptions = {
        center: new google.maps.LatLng(CenterLat, CenterLon),
        zoom: ZoomLvl,
        mapTypeId: google.maps.MapTypeId.ROADMAP
    };
    Map = new google.maps.Map(document.getElementById("map_canvas"), mapOptions);
    
    // show footer at info-area
    $(function(){
        $(window).resize(function(e){
            placeFooter();
        });
        placeFooter();
        // hide it before it's positioned
        $('#info_footer').css('display','inline');
    });
    
    // Listener for newly created Map
    google.maps.event.addListener(Map, 'center_changed', function() {
        localStorage['CenterLat'] = Map.getCenter().lat();
        localStorage['CenterLon'] = Map.getCenter().lng();
    });
    
    google.maps.event.addListener(Map, 'zoom_changed', function() {
        localStorage['ZoomLvl']  = Map.getZoom();
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
}
