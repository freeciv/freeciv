/*****************************************************************************\
 Freeciv web client, graphics rendering. 
\*****************************************************************************/

var tileset = [];
var mapview_canvas;
var mapview_context;
var minimap_canvas;
var minimap_context;

var tileset_loaded = false;

function init_mapview()
{
  mapview_canvas = document.getElementById("mapview");
  mapview_context = mapview_canvas.getContext("2d");
  minimap_canvas = document.getElementById("minimap");
  minimap_context = minimap_canvas.getContext("2d");

  mapview_canvas.setAttribute('width', 1024);
  mapview_canvas.setAttribute('height', 650);

  minimap_context.fillStyle = "rgb(0,0,0)";
  minimap_context.fillRect (0, 0, 200, 200);

  mapview_context.fillStyle = "rgb(0,0,0)";
  mapview_context.fillRect (0, 0, 2000, 1000);


  var pl = new SOAPClientParameters();
  try {
    SOAPClient.invoke(url, "getFreecivTileset", pl, true, tileset_callBack);
  } catch(e) {
    alert("An error has occured!");
  }


}

/* Loads the tileset from webservice. */
function tileset_callBack(tilespec)
{
  /* Store all tilespecifications in the tilespec dictionary. */
  for (var i = 0; i < tilespec.length; i += 6) {
    tileset[tilespec[i]] = {tag : tilespec[i],
                file : tilespec[i+1],
                x : parseInt(tilespec[i+2]),
                y : parseInt(tilespec[i+3]),
                width : parseInt(tilespec[i+4]),
                height : parseInt(tilespec[i+5]),
		image : 0,
                sprite : 0}; 
  }

  /* Preload all images. */
  var loaded = 0;
  for (var t in tileset) {
    var Tile = tileset[t];
    var img = new Image();
    Tile.image = img;
    img.src = "/freeciv/data/" + Tile.file;
    img.onload = function(){
      loaded++;
      if (loaded == tilespec.length / 6) crop_sprites();
    }
  }

}


/* Loads the tileset from webservice. */
function crop_sprites()
{
  for (var t in tileset) {
    if (tileset[t].width <= 0 || tileset[t].width > 5000) tileset[t].width = tileset[t].image.width;
    if (tileset[t].height <= 0 || tileset[t].height > 5000) tileset[t].height = tileset[t].image.height;
    if (tileset[t].x <= 0 || tileset[t].x > 5000) tileset[t].x = 0;
    if (tileset[t].y <= 0 || tileset[t].y > 5000) tileset[t].y = 0;
    if (tileset[t].file == undefined || tileset[t].file == "undefined") continue;

    tileset[t].sprite = document.createElementNS('http://www.w3.org/1999/xhtml', 'canvas');

    tileset[t].sprite.width = tileset[t].width;
    tileset[t].sprite.height = tileset[t].height;
    var sprite_context = tileset[t].sprite.getContext('2d');
    try {
      sprite_context.drawImage(tileset[t].image, tileset[t].x, tileset[t].y, tileset[t].width, tileset[t].height,
		               0, 0, tileset[t].width, tileset[t].height);
      //mapview_context.drawImage(Tile.sprite, Tile.x, Tile.y, Tile.width, Tile.height, 0, 0, Tile.width, Tile.height);

    } catch(e) {
      //alert("An error has occured!");
    }

  }
  tileset_loaded = true;

}

/* Draw() does all the mapview rendering. */
function draw_mapview() 
{

  if (tileset_loaded == true && current_game_state >= CLIENT_GAME_RUNNING_STATE) {
    var pl = new SOAPClientParameters();
    try {
      SOAPClient.invoke(url, "getFreecivMapview", pl, true, mapview_callBack);
    } catch(e) {
      alert("An error has occured!");
    }
  }

  framerate_update ();

}

/* Renders the mapview.  */
function mapview_callBack(mapdef)
{

  for (var i = 0; i < mapdef.length; i += 3) {
    try {
    Tile = tileset[mapdef[i]];

    mapview_context.drawImage(Tile.sprite, 0, 0, Tile.width, Tile.height, mapdef[i+1] , mapdef[i+2], 
			      Tile.width , Tile.height);

//    mapview_context.drawImage(Tile.image, Tile.x, Tile.y, Tile.width, Tile.height, mapdef[i+1] , mapdef[i+2], 
//			      Tile.width , Tile.height);
  } catch(e) {
    }

  }

}
