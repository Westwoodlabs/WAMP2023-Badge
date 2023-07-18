const alltagsdata = {};
const imageQueue = [];
let hideStatusClasses = [];
let isProcessing = false;
let reconnectInterval = 1000; // Time interval (in milliseconds) between reconnection attempts
let websocketUrl = "ws://" + window.location.hostname + ":8765";
const colorTable = {
  0: [255, 255, 255],
  1: [0, 0, 0],
  2: [255, 0, 0],
  3: [150, 150, 150],
};

$(document).ready(function () {
  // Start WebSocket connection
  connectWebSocket();

  // Update tags every second
  setInterval(updateTags, 1000);

  $("#navStatus").on("click", function (event) {
    let text = "";
    Object.keys(alltagsdata).forEach(function (mac) {
      text = text + mac.replaceAll(":", "") + "\n";
    });
    console.log(text);
  });

  $(".statusBtn").on("click", function (event) {
    hideStatusClasses = [];
    // Toggle active class
    $(this).toggleClass("active");

    // Get all active status
    $(".statusBtn").each((index, element) => {
      if (!$(element).hasClass("active")) {
        hideStatusClasses.push($(element).attr("data-status"));
      }
    });

    console.log(hideStatusClasses);

    $(".tagcard").each((index, element) => {
      let found = false;
      for (var i = 0; i < hideStatusClasses.length; i++) {
        if ($(element).hasClass(hideStatusClasses[i])) {
          //console.log("found " + active[i] + " in " + $(element).attr("id"));
          found = true;
          break;
        }
      }
      if (found) {
        $(element).hide();
      } else {
        $(element).show();
      }
    });
  });

  $("#navSearch").on("input", function () {
    var searchTerm = $(this).val().toLowerCase();
    searchTerm = searchTerm.replaceAll(":", "");
    $(".tagcard").each(function () {
      var listItemText = $(this).attr("id").toLowerCase();
      if (listItemText.indexOf(searchTerm) === -1) {
        $(this).hide();
      } else {
        $(this).show();
      }
    });
  });
});

function connectWebSocket() {
  // Create a new WebSocket connection
  const socket = new WebSocket(websocketUrl);

  // Event: Connection established
  socket.onopen = () => {
    console.log("WebSocket connection established.");
  };

  // Event: Received message from the server
  socket.onmessage = (event) => {
    const message = event.data;
    //console.log("Received message:", message);
    processMsg(message);
  };

  // Event: Connection closed
  socket.onclose = (event) => {
    console.log("WebSocket closed with code:", event.code);

    setTimeout(connectWebSocket, reconnectInterval);
  };

  // Event: Error
  socket.onerror = (error) => {
    console.error("WebSocket error:", error);
    socket.close(); // Close the socket in case of an error
  };
}

function processMsg(msg) {
  var json = JSON.parse(msg);
  //console.log(json);
  if (json.hasOwnProperty("tag_updates")) {
    var updates = json.tag_updates;
    Object.keys(updates).forEach(function (key) {
      alltagsdata[key] = updates[key];
      alltagsdata[key].new = true;
    });
  }
}

function updateTags() {
  let count = {
    total: 0,
    pending: 0,
    offline: 0,
    online: 0,
  };
  Object.keys(alltagsdata).forEach(function (key) {
    var mac = key;
    var data = alltagsdata[key];
    var id = "tag-" + mac.replaceAll(":", "");
    var tag = $("#" + id);
    if (tag.length == 0) {
      //console.log("add tag");
      $(
        `<div id="${id}" class="tagcard card">
          <canvas id="canvas" width="296" height="128" class="card-img-top img"></canvas>
          <!--<div class="card-img-overlay">
            Test
          </div>-->
          <div class="card-body">
            <h5 class="card-title mac"></h5>
            <div class="batteryMv"></div>
            <div class="lastPacketLQI"></div>
            <div class="lastPacketRSSI"></div>
            <div class="temperature"></div>
            <div class="mqtt_id"></div>
          </div> 
           <div class="card-footer text-center">
            <small class="lastCheckin text-body-secondary"></small>
          </div>
        </div>`
      ).appendTo("#taglist");
    } else {
      // console.log("tag " + id + " found");
    }

    if (data.new) {
      loadImage(
        id,
        "../cache/" + mac.replaceAll(":", "") + ".raw?" + Date.now()
      );
      data.new = false;
    }

    // Calculate the time difference in milliseconds
    const timediff =
      Math.floor(Date.now() / 1000) - parseInt(data.last_checkin, 10);
    // console.log(
    //   Date.now() +
    //     " | " +
    //     parseInt(data.lastCheckin, 10) * 1000 +
    //     " | " +
    //     (Date.now() - parseInt(data.lastCheckin, 10) * 1000)
    // );

    $("#" + id + " .mac").html(mac);
    $("#" + id + " .lastCheckin").html("Last seen: " + displayTime(timediff));
    $("#" + id + " .batteryMv").html(
      "Battery: " + data.battery_mv / 1000 + "V"
    );
    $("#" + id + " .lastPacketLQI").html("LQI: " + data.last_lqi + "%");
    $("#" + id + " .lastPacketRSSI").html("RSSI: " + data.last_rssi + "dBm");
    $("#" + id + " .temperature").html("Temp: " + data.temperature + "&deg;C");
    if (typeof data.mqtt_id !== "undefined") {
      $("#" + id + " .mqtt_id").html("Last AP: " + data.mqtt_id.toUpperCase());
    } else {
      $("#" + id + " .mqtt_id").html("Last AP: ---");
    }

    let curClass = "online";

    if (data.pendingVersion != data.imageVersion) {
      curClass = "pending";
      count.pending++;
    }

    if (timediff > 120 || isNaN(timediff)) {
      curClass = "offline";
      count.offline++;
    }

    $("#" + id)
      .removeClass("online pending offline")
      .addClass(curClass);

    if (hideStatusClasses.includes(curClass)) {
      $("#" + id).hide();
    }

    if (
      id
        .toLowerCase()
        .indexOf($("#navSearch").val().replaceAll(":", "").toLowerCase()) === -1
    ) {
      $("#" + id).hide();
    }

    count.total++;
    count.online = count.total - count.offline;
  });

  $("#navStatus").html(
    `Online: ${count.online} | Pending: ${count.pending} | Offline: ${count.offline} | <b>Total: ${count.total}</b>`
  );
}

function displayTime(seconds) {
  if (isNaN(seconds)) {
    return "unknown";
  }
  let hours = Math.floor(Math.abs(seconds) / 3600);
  let minutes = Math.floor((Math.abs(seconds) % 3600) / 60);
  let remainingSeconds = Math.abs(seconds) % 60;
  return (
    (seconds < 0 ? "-" : "") +
    (hours > 0
      ? `${hours}:${String(minutes).padStart(2, "0")}`
      : `${String(minutes).padStart(2, "0")}`) +
    `:${String(remainingSeconds).padStart(2, "0")}`
  );
}

function loadImage(id, imageSrc) {
  imageQueue.push({ id, imageSrc });
  if (!isProcessing) {
    processQueue();
  }
}

function processQueue() {
  if (imageQueue.length === 0) {
    isProcessing = false;
    return;
  }

  isProcessing = true;
  const { id, imageSrc } = imageQueue.shift();

  const canvas = $("#" + id + " .img");

  fetch(imageSrc)
    .then((response) => {
      if (!response.ok) {
        if (response.status === 404) {
	  throw new Error("Resource not found");
        } else {
          throw new Error("An error occurred");
        }
      }
      return response.arrayBuffer();
    })
    .then((buffer) => {
      renderImg(buffer, canvas[0]);
      processQueue();
    })
    .catch((error) => {
      console.error(error.message);
      blankImg(canvas[0]);
      processQueue();
    });
}

function blankImg(canvas) {
  const ctx = canvas.getContext("2d");
  const squareSize = 4;

  // Draw the chessboard
  for (let row = 0; row * squareSize < canvas.height; row++) {
    for (let col = 0; col * squareSize < canvas.width; col++) {
      const x = col * squareSize;
      const y = row * squareSize;

      // Alternate the color of the squares
      if ((row + col) % 2 === 0) {
        ctx.fillStyle = "white";
      } else {
        ctx.fillStyle = "gray";
      }

      // Draw the square
      ctx.fillRect(x, y, squareSize, squareSize);
    }
  }

  const text = "NO IMAGE";
  const fontSize = 30;

  // Set font properties
  ctx.font = "bold 35px Arial";
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillStyle = "black";
  ctx.strokeStyle = "white";

  // Calculate text position
  const textX = canvas.width / 2;
  const textY = canvas.height / 2;

  // Draw text at the center
  ctx.fillText(text, textX, textY);
  ctx.strokeText(text, textX, textY);
}

function renderImg(buffer, canvas) {
  var inMemoryCanvas = document.createElement("canvas");
  inMemoryCanvas.width = canvas.height;
  inMemoryCanvas.height = canvas.width;

  const ctx = inMemoryCanvas.getContext("2d");

  const imageData = ctx.createImageData(
    inMemoryCanvas.width,
    inMemoryCanvas.height
  );
  const data = new Uint8ClampedArray(buffer);
  const offsetRed =
    data.length >= ((inMemoryCanvas.width * inMemoryCanvas.height) / 8) * 2
      ? (inMemoryCanvas.width * inMemoryCanvas.height) / 8
      : 0;
  var pixelValue = 0;
  for (let i = 0; i < data.length; i++) {
    for (let j = 0; j < 8; j++) {
      const pixelIndex = i * 8 + j;
      if (offsetRed) {
        pixelValue =
          (data[i] & (1 << (7 - j)) ? 1 : 0) |
          ((data[i + offsetRed] & (1 << (7 - j)) ? 1 : 0) << 1);
      } else {
        pixelValue = data[i] & (1 << (7 - j)) ? 1 : 0;
      }
      imageData.data[pixelIndex * 4] = colorTable[pixelValue][0];
      imageData.data[pixelIndex * 4 + 1] = colorTable[pixelValue][1];
      imageData.data[pixelIndex * 4 + 2] = colorTable[pixelValue][2];
      imageData.data[pixelIndex * 4 + 3] = 255;
    }
  }

  ctx.putImageData(imageData, 0, 0);

  const ctx2 = canvas.getContext("2d");
  ctx2.rotate(Math.PI / 2);
  ctx2.drawImage(inMemoryCanvas, 0, -canvas.width);
  ctx2.rotate(-Math.PI / 2);
}
