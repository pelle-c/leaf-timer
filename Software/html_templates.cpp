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
%LINKS%<br><br>
<br>%CLOCK%<br>%SOC%<br>%SOH%<br>
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
%LINKS%<br><br>
<form action='/timer_set'>
Start:<input type='time' id='timer_start' name='timer_start'><br><br>
Stop:<input type='time' id='timer_stop' name='timer_stop'><br><br>
Stop charge at SoC: <input type='number' id='timer_soc' name='timer_soc' min='10' max='100' value='80'><br>
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
%LINKS%<br><br>
<form action='/clock_set'>
Epoch timestamp now: <input type='number' id='timestamp' name='timestamp' ><br>
<br><input type="submit" value="Set clock" />
</form></body>
)rawliteral";
