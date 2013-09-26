var listKMLType = ['Approch', 'Departure', 'Transit', 'Custom1', 'Custom2'];
var listKMLs = localStorage['listKMLs'] || [];

function optionsInitalize() {
	// Write your initalization here
	// Gets called just before the 1-sec function call loop is setup
	$( "#dialog-modal" ).dialog({
		height: 140,
		modal: true,
		autoOpen: false,
		closeOnEscape: false
	});
}

function optionsModal() {
	$( "#dialog-modal" ).dialog( "open");
}
