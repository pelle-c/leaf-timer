const char index_html[] = R"rawliteral(
<!doctypehtml><title>Leaf Timer</title>
<script type="text/javascript">
function load()
{
setTimeout("window.open('/', '_self');", 5000);
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
<h1>Leaf charging timer</h1>
%LINKS%<br>
%CLOCK%
%CAN_DATA%
%TIMER%<br>
%SOC%<br>
%SOH%<br>
%CAR_OFF%<br>
%PLUGGED_IN%<br>
%CHARGING%<br>
%HV_STATUS%
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
<h1>Leaf charging timer - Set timer</h1>
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
<body>
<h1>Leaf charging timer - Set clock</h1>
%LINKS%<br>
<form action='/clock_set'>
Epoch timestamp now: <input type='number' id='timestamp' name='timestamp' ><br>
<br><input type="submit" value="Set clock" />
</form></body>
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
}</style>
<body>
<h1>Leaf charging timer - Control</h1>
%LINKS%<br>
<a href='/start_charging' class='button'>Start charging</a>&nbsp;<a href='/stop_charging' class='button'>Stop charging</a>&nbsp<a href='/wake_up' class='button'>Wake up</a>
</body>
)rawliteral";
