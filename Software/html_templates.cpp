const char index_html[] = R"rawliteral(
<!doctypehtml><title>Leaf Timer</title>
<script type="text/javascript">
function xmlhttpGet(strURL) {
    var xmlHttpReq = false;
    var self = this;
    // Mozilla/Safari
    if (window.XMLHttpRequest) {
        self.xmlHttpReq = new XMLHttpRequest();
    }
    // IE
    else if (window.ActiveXObject) {
        self.xmlHttpReq = new ActiveXObject("Microsoft.XMLHTTP");
    }

    self.xmlHttpReq.overrideMimeType('Content-Type', 'text/html');
    self.xmlHttpReq.open('GET', strURL, true);
    self.xmlHttpReq.send();

    self.xmlHttpReq.onreadystatechange = function() {
        if (self.xmlHttpReq.readyState == 4) {
            update_div(self.xmlHttpReq.responseText);
        }
    }
}
function update_div(str){
  document.getElementById("div_overview").innerHTML=str;
}

function doLoop1()
{
  xmlhttpGet('div_overview');
  setTimeout(doLoop1,1000);
}

function load()
{
  doLoop1();
}
</script>
<meta content="width=device-width"name=viewport><style>html{font-family:Arial;display:inline-block;text-align:center}h2{font-size:3rem}body{max-width:800px;margin:0 auto}
a.button {
    padding: 1px 6px;
    border: 1px outset buttonborder;
    border-radius: 3px;
    color: buttontext;
    background-color: buttonface;
    text-decoration: none;
}</style>
<body onload="load()">
<h1>Leaf timer</h1>
%LINKS%<br>
<div id="div_overview">
</div>
</body>
)rawliteral";


const char div_overview_html[] = R"rawliteral(
%CLOCK%
%CAN_DATA%
%TIMER%<br>
%WAKE_UP%<br>
<div id="status" %DIV_STATUS_STYLE%>
%SOC%<br>
%SOH%<br>
%PLUGGED_IN%<br>
%CHARGING%<br>
%CAR_STATUS%<br>
%HV_STATUS%<br>
%HV_POWER%<br>
%CHARGE_TIME%<br>
%BATTERY_TYPE%
</div>
)rawliteral";

const char wifi_html[] = R"rawliteral(
<!doctypehtml><title>Leaf Wifi</title>
<meta content="width=device-width"name=viewport><style>html{font-family:Arial;display:inline-block;text-align:center}h2{font-size:3rem}body{max-width:800px;margin:0 auto}
a.button {
    padding: 1px 6px;
    border: 1px outset buttonborder;
    border-radius: 3px;
    color: buttontext;
    background-color: buttonface;
    text-decoration: none;
}</style>
<body>
<h1>Wifi settings</h1>
%LINKS%<br>
<form action='/wifi_set'>
AP Password:<input type='password' id='ap_password' name='ap_password' value='%AP_PASSWORD%'><br><br>
Connect to WLAN: <select name="wlan_connect">
  <option %WLAN_CONNECT_1% value="1">Yes</option>
  <option %WLAN_CONNECT_0% value="0">No</option>
</select><br><br>
<a href='/wifi_connect' class='button'>Connect now</a>&nbsp<br><br>
WLAN is %WLAN_STATUS%<br><br>
WLAN SSID:<input type='text' id='wlan_ssid' name='wlan_ssid' value='%WLAN_SSID%'><br><br>
WLAN Password:<input type='password' id='wlan_password' name='wlan_password' value='%WLAN_PASSWORD%'><br><br>
<br><input type="submit" value="Save" /><br><br>
Note: You may need to reset after configuration changes.
</form>
<div>
<br><br>
<div id="message" align="center" class="%DIV_MESSAGE_CLASS%">
<br>
%MESSAGE%<br>
<br>
</div>
</body>
)rawliteral";


const char timer_html[] = R"rawliteral(
<!doctypehtml><title>Leaf Timer</title>
<meta content="width=device-width"name=viewport><style>html{font-family:Arial;display:inline-block;text-align:center}h2{font-size:3rem}body{max-width:800px;margin:0 auto}
a.button {
    padding: 1px 6px;
    border: 1px outset buttonborder;
    border-radius: 3px;
    color: buttontext;
    background-color: buttonface;
    text-decoration: none;
}</style>
<body>
<h1>Leaf timer - Set timer</h1>
%LINKS%<br>
<form action='/timer_set'>
Timer enabled: <select name="timer_enabled">
  <option %TIMER_ENABLED_1% value="1">Yes</option>
  <option %TIMER_ENABLED_0% value="0">No</option>
</select><br><br>
Start:<input type='time' id='timer_start' name='timer_start' value='%TIMER_START%'><br><br>
Stop:<input type='time' id='timer_stop' name='timer_stop' value='%TIMER_STOP%'><br><br>
Stop charge at SoC: <input type='number' id='timer_soc' name='timer_soc' min='10' max='100' value='%TIMER_SOC%'><br>
<br><input type="submit" value="Set timer" />
</form>
</body>
)rawliteral";

const char clock_html[] = R"rawliteral(
<!doctypehtml><title>Leaf Timer</title>
<meta content="width=device-width"name=viewport><style>html{font-family:Arial;display:inline-block;text-align:center}h2{font-size:3rem}body{max-width:800px;margin:0 auto}
a.button {
    padding: 1px 6px;
    border: 1px outset buttonborder;
    border-radius: 3px;
    color: buttontext;
    background-color: buttonface;
    text-decoration: none;
}</style>
<script type="text/javascript">
function dateToUnixEpoch(date) {
        return Math.floor(date.getTime()) / 1000;
}
function getTime(param) {
  var time_now = (new Date().toLocaleTimeString()).substring(0,5);
  var date_now = (new Date().toISOString()).substring(0,10);
  var epoch = dateToUnixEpoch(new Date());
 console.log(time_now);
 console.log(epoch);
  if (param == "clock") {
    return time_now;
  }
  if (param == "date") {
    return date_now;
  }
}
function set_timestamp() {
  const zeroPad = (num, places) => String(num).padStart(places, '0')
  var tz_offset = new Date().getTimezoneOffset();
  var h_offset = parseInt(tz_offset / 60);
  var sign = "-";
  if (h_offset < 0) {
    sign = "+";
    h_offset = -h_offset;
  }
  var m_offset = Math.abs(tz_offset) - (Math.abs(h_offset) * 60);
  var datetime_value = document.getElementById("date").value + "T" + document.getElementById("clock").value + ":00" + sign + zeroPad(h_offset,2) + ":" + zeroPad(m_offset,2);
  const ms = parseInt(dateToUnixEpoch(new Date(datetime_value)));
  console.log(datetime_value);
  console.log(ms);
  document.getElementById("timestamp").setAttribute('value',ms);
  return true;
}
</script>
<body>
<h1>Leaf timer - Set clock</h1>
%LINKS%<br>
<form action='/clock_set'>
<input type="hidden" id="timestamp" name="timestamp" value="">
Clock:<input type='time' id='clock' name='clock'><br><br>
Date:<input type='date' id='date' name='date'><br><br>
<script type="text/javascript">
  document.getElementById("clock").setAttribute('value',getTime('clock'));
  document.getElementById("date").setAttribute('value',getTime('date'));
</script>
<br><input type="submit" value="Set clock" onclick="return set_timestamp();" />
</form>
</body>
)rawliteral";

const char control_html[] = R"rawliteral(
<!doctypehtml><title>Leaf Timer</title>
<meta content="width=device-width"name=viewport><style>html{font-family:Arial;display:inline-block;text-align:center}h2{font-size:3rem}body{max-width:800px;margin:0 auto}
a.button {
    padding: 1px 6px;
    border: 1px outset buttonborder;
    border-radius: 3px;
    color: buttontext;
    background-color: buttonface;
    text-decoration: none;
}
.border{
    box-shadow:0px 0px 0px 2px black inset;
    margin-bottom:10px;
}
.hidden {
    display: none;
}

</style>
<body>
<h1>Leaf timer - Manual Control</h1>
%LINKS%<br>
<a href='/wake_up' class='button'>Wake up VCM</a>&nbsp<a href='/sleep' class='button'>VCM Sleep</a>&nbsp<a href='/reset' class='button'>Reset</a><br><br>
<a href='/start_charging' class='button'>Start charging</a>&nbsp;<a href='/stop_charging' class='button'>Stop charging</a>&nbsp<a href='/start_acc' class='button'>Start ACC</a>&nbsp;<a href='/stop_acc' class='button'>Stop ACC</a>
<br><br>
<div align="left">
<li>"Wake up VCM" will pull a pin on the VCM to +12V, emulating a remote wake-up command from the TCU. After this, the VCM will start sending messages.
<li>"VCM Sleep" will pull a pin on the VCM to 0V. The VCM will not go to sleep right away.
<li>"Start charging" will send multiple can messages, emulating a remote command from Nissan Carwings. The VCM needs to be awake (can-bus is active).<br></li>
<li>"Stop charging" will send multiple can messages, emulating a battery full message from the LBC. After this, the VCM has to go to sleep before starting to charge again.</li>
<li>"Start ACC" will send multiple can messages, emulating a remote command from Nissan Carwings. The VCM needs to be awake (can-bus is active).<br></li>
<li>"Stop ACC" will send multiple can messages, emulating a remote command from Nissan Carwings. The VCM needs to be awake (can-bus is active).<br></li>
<div>
<br><br>
<div id="message" align="center" class="%DIV_MESSAGE_CLASS%">
<br>
%MESSAGE%<br>
<br>
</div>
</body>
)rawliteral";

const char message_html[] = R"rawliteral(
<!doctypehtml><title>Leaf Timer</title>
<meta content="width=device-width"name=viewport><style>html{font-family:Arial;display:inline-block;text-align:center}h2{font-size:3rem}body{max-width:800px;margin:0 auto}
a.button {
    padding: 1px 6px;
    border: 1px outset buttonborder;
    border-radius: 3px;
    color: buttontext;
    background-color: buttonface;
    text-decoration: none;
}</style>
<body>
<h1>Leaf timer</h1>
%LINKS%<br>
%MESSAGE%
</body>
)rawliteral";

const char log_html[] = R"rawliteral(
<!doctypehtml><title>Leaf Timer</title>
<meta content="width=device-width"name=viewport><style>html{font-family:Arial;display:inline-block;text-align:center}h2{font-size:3rem}body{max-width:800px;margin:0 auto}
a.button {
    padding: 1px 6px;
    border: 1px outset buttonborder;
    border-radius: 3px;
    color: buttontext;
    background-color: buttonface;
    text-decoration: none;
}</style>
<body>
<h1>Leaf timer - Log</h1>
%LINKS%<br>
<a href='/log?candump=1' class='button'>Start candump to SD</a>&nbsp<a href='log?candump=0' class='button'>Stop candump to SD</a>&nbsp<a href='log?candump=2' class='button'>Delete candump</a>&nbsp<a href='log?candump=3' class='button'>Download candump</a><br><br>
%CANDUMP%, %SD_CARD%
<br>
<br>
<div id="log" align="left">
%LOG%
</div>
</body>
)rawliteral";

const char climate_html[] = R"rawliteral(
<!doctypehtml><title>Leaf Timer</title>
<script type="text/javascript">
function xmlhttpGet(strURL) {
    var xmlHttpReq = false;
    var self = this;
    // Mozilla/Safari
    if (window.XMLHttpRequest) {
        self.xmlHttpReq = new XMLHttpRequest();
    }
    // IE
    else if (window.ActiveXObject) {
        self.xmlHttpReq = new ActiveXObject("Microsoft.XMLHTTP");
    }

    self.xmlHttpReq.overrideMimeType('Content-Type', 'text/html');
    self.xmlHttpReq.open('GET', strURL, true);
    self.xmlHttpReq.send();

    self.xmlHttpReq.onreadystatechange = function() {
        if (self.xmlHttpReq.readyState == 4) {
            update_div(self.xmlHttpReq.responseText);
        }
    }
}
function update_div(str){
  document.getElementById("div_climate").innerHTML=str;
}

function doLoop1()
{
  xmlhttpGet('div_climate');
  setTimeout(doLoop1,1000);
}

function load()
{
  doLoop1();
}
</script>
<meta content="width=device-width"name=viewport><style>html{font-family:Arial;display:inline-block;text-align:center}h2{font-size:3rem}body{max-width:800px;margin:0 auto}
a.button {
    padding: 1px 6px;
    border: 1px outset buttonborder;
    border-radius: 3px;
    color: buttontext;
    background-color: buttonface;
    text-decoration: none;
}</style>
<body onload="load()">
<h1>Leaf climate</h1>
%LINKS%<br>
<div id="div_climate">
</div>
</body>
)rawliteral";

const char div_climate_html[] = R"rawliteral(
%CLIMATE%
)rawliteral";



