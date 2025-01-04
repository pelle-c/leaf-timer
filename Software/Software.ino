#include "esp_system.h"
#include <Arduino.h>
#include <ESP32CAN.h>
#include <CAN_config.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include <ctime>
#include <Adafruit_NeoPixel.h>

#include "html_templates.cpp"


const char *ssid = "lily";
const char *password = "0123456789";

AsyncWebServer server(80);

CAN_device_t CAN_cfg;               // CAN Config
unsigned long previousMillis = 0;   // will store last time a CAN Message was send
const int interval_can = 100;       // at which interval to send CAN Messages (milliseconds)
const int interval_bat = 50;        // n * interval_can at which send CAN Messages to battery for detailed info
const int rx_queue_size = 10;       // Receive Queue size

#define CAN_SE_PIN 23
#define WAKE_UP_PIN 25

#define LED_PIN 4
#define PIXEL 0
#define NUMPIXELS 1
Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

#define LEAF_ZE0 0
#define LEAF_AZE0 1
#define LEAF_ZE1 2

int state_wake_up_pin = 0;

void setup() {
  init_time();
  Serial.begin(115200);
  Serial.println();
  Serial.println("Starting Leaf timer");
  Serial.println("Configuring access point...");
  init_ap();
  init_can();
  init_webserver();
  pinMode(WAKE_UP_PIN, OUTPUT);  
  clear_wake_up();
  blink_led();
}

void blink_led() {
  pixels.begin();
  pixels.clear();
  //int PIXEL = 0;
  for(int i=0; i<200; i++) {
    pixels.setPixelColor(PIXEL, pixels.Color(0, i, 0));
    pixels.show();
    delay(2);
  }
  for(int i=200; i>-1; i--) {
    pixels.setPixelColor(PIXEL, pixels.Color(0, i, 0));
    pixels.show();
    delay(5);
  }
  pixels.clear();
}

void clear_wake_up() {
  state_wake_up_pin = 0;
  digitalWrite(WAKE_UP_PIN,state_wake_up_pin);
}

void set_wake_up() {
  state_wake_up_pin = 1;
  digitalWrite(WAKE_UP_PIN,state_wake_up_pin);
}


void init_time() {
  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/ 3", 1); // https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
  tzset();
}

void init_can() {
  pinMode(CAN_SE_PIN, OUTPUT);
  digitalWrite(CAN_SE_PIN, LOW);
  CAN_cfg.speed = CAN_SPEED_500KBPS;
  CAN_cfg.tx_pin_id = GPIO_NUM_27;
  CAN_cfg.rx_pin_id = GPIO_NUM_26;
  CAN_cfg.rx_queue = xQueueCreate(rx_queue_size, sizeof(CAN_frame_t));
  ESP32Can.CANInit();
}

void init_ap() {
  if (!WiFi.softAP(ssid, password)) {
    Serial.println("Soft AP creation failed.");
    while (1);
  }
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
}

char status_plugged_in = 0;
char status_charging = 0;
char status_car_off = 1;
char status_lb_failsafe = 0;
float status_soc = 0;
float status_soh = 0;
int battery_type = LEAF_ZE0;

char timer_enabled = 0;
String timer_start = "23:00";
String timer_stop = "05:00";
String html_message = "";
int timer_soc = 80;

int timestamp_last_can_received = 0;
u_char group_message[255];
char group = 0;
int pointer_group_message = 0;
int counter_bat = 0;
unsigned long last_group_request_millis = 0;

void loop() {

  CAN_frame_t rx_frame;  
  unsigned long currentMillis = millis();


  // Receive next CAN frame from queue
  if (xQueueReceive(CAN_cfg.rx_queue, &rx_frame, 3 * portTICK_PERIOD_MS) == pdTRUE) {

    if (rx_frame.FIR.B.RTR != CAN_RTR) {
      timestamp_last_can_received = timestamp_now();

      if (rx_frame.MsgID == 0x1c2) {  
        battery_type = LEAF_ZE1;
      }

      if (rx_frame.MsgID == 0x59e) { 
        battery_type = LEAF_AZE0;
      }

      if (rx_frame.MsgID == 0x11a) { // Car status
        status_car_off = (rx_frame.data.u8[1] & 0xc0) >> 7;
        //Serial.printf("Car message: on:%d, charging:%d\n",status_car_off);       
      }

      if (rx_frame.MsgID == 0x1db) { // HVBAT status
        status_lb_failsafe = (rx_frame.data.u8[1] & 0x07);
        //Serial.printf("LB_Failsafe (0=OK):%d\n",status_lb_failsafe);     
      }  

      if (rx_frame.MsgID == 0x5bc) { // HVBAT
          status_soh = rx_frame.data.u8[4] >> 1;
          //Serial.printf("SoH:%.2f\n",status_soh);
      }

      if (rx_frame.MsgID == 0x5bf) { // On board charger information
        if (rx_frame.data.u8[2] > 0) {
          status_plugged_in = 1;
        } else {
          status_plugged_in = 0;
        } 
        if (rx_frame.data.u8[1] > 0) {
          status_charging = 1;
        } else {
          status_charging = 0;
        }
        //Serial.printf("OBC message: plugged in:%d, charging:%d\n",status_plugged_in,status_charging);       
      }

      if (rx_frame.MsgID == 0x7bb) { // LBC info - groups
        if (rx_frame.data.u8[0] == 0x10) { // First response
          for (int i = 0; i < rx_frame.FIR.B.DLC; i++) {
            group_message[i] = rx_frame.data.u8[i];
          }
          pointer_group_message = rx_frame.FIR.B.DLC;
          group = rx_frame.data.u8[3];
        } else {
          for (int i = 0; i < rx_frame.FIR.B.DLC; i++) {
            group_message[i+pointer_group_message] = rx_frame.data.u8[i];
          }
          pointer_group_message += rx_frame.FIR.B.DLC;
        }
        if (group == 1 && rx_frame.data.u8[0] == 0x25) { /* Done with group 1*/
          // Leaf 24kWh
          if (battery_type == 0) {
            status_soc = (( group_message[32 + 5] << 16) + (group_message[32 + 6] << 8) + (group_message[32 + 7])) / 10000;
          }
          if ((battery_type == 1) || (battery_type == 2)) {
            status_soc = (( group_message[32 + 7] << 16) + (group_message[40 + 1] << 8) + (group_message[40 + 2])) / 10000;
          }
          Serial.printf("SoC:%.2f\n",status_soc);
        } else { // Request more messages 300100FFFFFFFFFF
          CAN_frame_t tx_frame;
          tx_frame.FIR.B.FF = CAN_frame_std;
          tx_frame.MsgID = 0x79b;
          tx_frame.FIR.B.DLC = 8;
          tx_frame.data.u8[0] = 0x30;
          tx_frame.data.u8[1] = 0x01;
          tx_frame.data.u8[2] = 0x00;
          tx_frame.data.u8[3] = 0xff;
          tx_frame.data.u8[4] = 0xff;
          tx_frame.data.u8[5] = 0xff;
          tx_frame.data.u8[6] = 0xff;
          tx_frame.data.u8[7] = 0xff;
          ESP32Can.CANWriteFrame(&tx_frame);

        }

      }
    }
  }

  /*
  - Timer not active and charging: wake up if needed - stop charging - sleep
  - Timer active and plugged in and not charging and soc<limit: wake up if needed - start charging
  - Timer active and charging: check soc<limit: stop - sleep
  */

  // Interval tasks
  if (currentMillis - previousMillis >= interval_can) {
    previousMillis = currentMillis;
    counter_bat++;
    if (counter_bat > interval_bat) { // Request battery information
      pointer_group_message = 0; // Reset the pointer (group request timeout)
      group = 0;
      counter_bat = 0;
      CAN_frame_t tx_frame;
      tx_frame.FIR.B.FF = CAN_frame_std;
      tx_frame.MsgID = 0x79b;
      tx_frame.FIR.B.DLC = 8;
      tx_frame.data.u8[0] = 0x02;
      tx_frame.data.u8[1] = 0x21;
      tx_frame.data.u8[2] = 0x01;
      tx_frame.data.u8[3] = 0x00;
      tx_frame.data.u8[4] = 0x00;
      tx_frame.data.u8[5] = 0x00;
      tx_frame.data.u8[6] = 0x00;
      tx_frame.data.u8[7] = 0x00;
      ESP32Can.CANWriteFrame(&tx_frame);
    }
  }
}

void send_can_command(String command) {
  int counter = 0;
  CAN_frame_t tx_frame;
  tx_frame.FIR.B.FF = CAN_frame_std;
  if (command == "start_charging") {
    tx_frame.MsgID = 0x56e;
    tx_frame.FIR.B.DLC = 1;
    tx_frame.data.u8[0] = 0x66;
  }
  if (command == "start_acc") {
    tx_frame.MsgID = 0x56e;
    tx_frame.FIR.B.DLC = 1;
    tx_frame.data.u8[0] = 0x4e;
  }
  if (command == "stop_acc") {
    tx_frame.MsgID = 0x56e;
    tx_frame.FIR.B.DLC = 1;
    tx_frame.data.u8[0] = 0x56;
  }
  if (command == "stop_charging") { // 000200100000e3df
    tx_frame.MsgID = 0x1db;
    tx_frame.FIR.B.DLC = 8;
    tx_frame.data.u8[0] = 0x00;
    tx_frame.data.u8[1] = 0x02;
    tx_frame.data.u8[2] = 0x00;
    tx_frame.data.u8[3] = 0x10;
    tx_frame.data.u8[4] = 0x00;
    tx_frame.data.u8[5] = 0x00;
    tx_frame.data.u8[6] = 0xe3;
    tx_frame.data.u8[7] = 0xdf;
  }
  while (counter < 25) {
    ESP32Can.CANWriteFrame(&tx_frame);
    delay(interval_can);
    counter++;
  }
}


bool can_data_is_old(int last_timestamp) {
  int ts_now = timestamp_now();
  if (ts_now > last_timestamp + 10) {
    return true;
  }
  return false;
}

int seconds_since_can_data(int last_timestamp) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec - last_timestamp);
}


int timestamp_now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
}

String time_now() {
  time_t now;
  char strftime_buf[64];
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  return String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min);
}

int get_hour(String time) {
  char* temp_char = new char[255];
  std::copy(time.begin(),time.end()+1,temp_char);
  std::string temp_string = temp_char;
  std::string hour = temp_string.substr(0, temp_string.find(":"));
  std::copy(hour.begin(),hour.end()+1,temp_char);
  return string2int(temp_char);
}

int get_minute(String time) {
  char* temp_char = new char[255];
  std::copy(time.begin(),time.end()+1,temp_char);
  std::string temp_string = temp_char;
  std::string minute = temp_string.substr(temp_string.find(":") + 1,-1 );
  std::copy(minute.begin(),minute.end()+1,temp_char);
  return string2int(temp_char);
}


bool timer_is_active() {
  bool ret = false;
  String clock_now = time_now();
  int hour_now = get_hour(time_now());
  int minute_now = get_minute(time_now());
  int hour_timer_start = get_hour(timer_start);
  int minute_timer_start = get_minute(timer_start);
  int hour_timer_stop = get_hour(timer_stop);
  int minute_timer_stop = get_minute(timer_stop);

  int minutes = hour_now * 60 + minute_now;
  int minutes_start = hour_timer_start * 60 + minute_timer_start;
  int minutes_stop = hour_timer_stop * 60 + minute_timer_stop;

  if (minutes_start > minutes_stop) { // passing midnight
    if (((minutes > minutes_start) && (minutes < 24*60)) || (minutes < minutes_stop)) {
      ret = true;
    }
  } else {
    if ((minutes > minutes_start) && (minutes < minutes_stop)) {
      ret = true;
    }
  }
  //Serial.printf("m,m1,m2:%d,%d,%d\n",minutes,minutes_start,minutes_stop);     
  return ret;

}

// Web-server related below...

void notFound(AsyncWebServerRequest* request) {
  request->send(404, "text/plain", "Not found");
}

int string2int(String temp_string) {
  char* temp_char = new char[255];
  std::copy(temp_string.begin(),temp_string.end() + 1,temp_char);
  // Serial.printf("Timer:%s,%s\n",temp_string,temp_char);     
  return atoi(temp_char);
}

void init_webserver() {

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", index_html, processor);
  });

  server.on("/timer", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", timer_html, processor);
  });

  server.on("/clock", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", clock_html, processor);
  });

  server.on("/control", HTTP_GET, [](AsyncWebServerRequest* request) {
    html_message = "";
    request->send_P(200, "text/html", control_html, processor);
  });

  server.on("/start_charging", HTTP_GET, [](AsyncWebServerRequest* request) {
    send_can_command("start_charging");
    html_message = "Start charging can messages sent!";
    request->send_P(200, "text/html", control_html, processor);
  });

  server.on("/stop_charging", HTTP_GET, [](AsyncWebServerRequest* request) {
    send_can_command("stop_charging");
    html_message = "Stop charging can messages sent!";
    request->send_P(200, "text/html", control_html, processor);
  });

  server.on("/start_acc", HTTP_GET, [](AsyncWebServerRequest* request) {
    send_can_command("start_acc");
    html_message = "Start ACC can messages sent!";
    request->send_P(200, "text/html", control_html, processor);
  });

  server.on("/stop_acc", HTTP_GET, [](AsyncWebServerRequest* request) {
    send_can_command("stop_acc");
    html_message = "Stop ACC can messages sent!";
    request->send_P(200, "text/html", control_html, processor);
  });

  server.on("/wake_up", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (can_data_is_old(timestamp_last_can_received)) {
      set_wake_up();
      html_message = "Tried to wake up VCM in car!";
    } else {
      html_message = "No need to wake up VCM in car, can data is recent!";
    }
    request->send_P(200, "text/html", control_html, processor);
  });

  server.on("/sleep", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (state_wake_up_pin == 1) {
      clear_wake_up();
      html_message = "VCM wake-up pin has now been cleared.";
    } else {
      html_message = "VCM wake-up pin is already zero!";
    }
    request->send_P(200, "text/html", control_html, processor);
  });

  server.on("/timer_set", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (request->hasParam("timer_enabled")) {
      timer_enabled = string2int(request->getParam("timer_enabled")->value());
      timer_start = request->getParam("timer_start")->value();
      timer_stop = request->getParam("timer_stop")->value();
      timer_soc = string2int(request->getParam("timer_soc")->value());
    } else {
      html_message = "No params found in request";
    }
    html_message = "Timer set!";
    request->send_P(200, "text/html", message_html, processor);
  });

  server.on("/clock_set", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (request->hasParam("timestamp")) {
      int timestamp = string2int(request->getParam("timestamp")->value());
      struct timeval tv_temp;
      tv_temp.tv_sec = timestamp; 
      tv_temp.tv_usec = 0; 
      settimeofday(&tv_temp, NULL);
    } else {
      html_message = "No timestamp found in request";
    }
    html_message = "Clock set!";
    request->send_P(200, "text/html", message_html, processor);
  });

  server.onNotFound(notFound);

  server.begin();
}

String processor(const String& var)
{
  if(var == "LINKS") {
    String content = "<a href='/' class='button'>Overview</a>&nbsp;<a href='/timer' class='button'>Set timer</a>&nbsp;<a href='/clock' class='button'>Set clock</a>&nbsp;<a href='/control' class='button'>Manual Control</a><br><hr>";
    return content;
  }

  if(var == "DIV_STATUS_STYLE") {
    String content = "";
    if (timestamp_last_can_received == 0) {
      content = "style='display: none;'";
    }
    return content;
  } 

  if(var == "CLOCK") {
    String content = "";
    time_t now;
    char strftime_buf[64];
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    content += "Time: " + String(strftime_buf) + "<br>";
    return content;
  } 
  if(var == "SOC") {
    String content = "";
    content += "SoC: " + String(status_soc) + "&#37;<br>";
    return content;
  } 
  if(var == "SOH") {
    String content = "";
    content += "SoH: " + String(status_soh) + "&#37;<br>";
    return content;
  } 
  if(var == "CAR_OFF") {
    String content = "";
    String status = "";
    if (status_car_off == 0) { status = "On"; }
    if (status_car_off == 1) { status = "Off"; }
    content += "Car: " + String(status) + "<br>";
    return content;
  } 
  if(var == "PLUGGED_IN") {
    String content = "";
    String status = "";
    if (status_plugged_in == 1) { status = "Yes"; }
    if (status_plugged_in == 0) { status = "No"; }
    content += "Plugged in: " + String(status) + "<br>";
    return content;
  } 
  if(var == "CAN_DATA") {
    String content = "";
    String font_p = "<p style='color:MediumSeaGreen;'>";
    if ((seconds_since_can_data(timestamp_last_can_received) > 60) or (timestamp_last_can_received == 0)) {
      font_p = "<p style='color:red;'>";
    }
    if (timestamp_last_can_received == 0) {
      content += String(font_p) + "No can messages seen yet</p>";
    } else {
      content += String(font_p) + "Last can message seen: " + String(seconds_since_can_data(timestamp_last_can_received)) + "s ago</p>";
    }
    return content;
  } 
  if(var == "TIMER") {
    String content = "";
    if (timer_enabled) {
      if (timer_is_active()) {
        content += "Timer is now active: " + String(timer_start) + " - " + String(timer_stop) + ", charge to SoC: " + String(timer_soc) + "&#37;";
      } else {
        content += "Timer will be active: " + String(timer_start) + " - " + String(timer_stop) + ", charge to SoC: " + String(timer_soc) + "&#37;";
      }
    } else {
      content += "Timer is disabled";
    }
    return content + "<br>";
  } 

  if(var == "CHARGING") {
    String content = "";
    String status = "";
    if (status_charging == 1) { status = "Yes"; }
    if (status_charging == 0) { status = "No"; }
    content += "Charging: " + String(status) + "<br>";
    return content;
  } 
  if(var == "HV_STATUS") {
    String content = "";
    String status = "";
    if (status_lb_failsafe == 0) { status = "Normal"; }
    if (status_lb_failsafe & 1) { status = "EV system error"; }
    if (status_lb_failsafe & 2) { status = "Charge disabled"; }
    if (status_lb_failsafe & 4) { status = "Turtle mode"; }
    content += "HV Status: " + String(status) + "<br>";
    return content;
  }

  if(var == "BATTERY_TYPE") {
    String content = "";
    String status = "";
    if (battery_type == 0) { status = "ZE0"; }
    if (battery_type == 1) { status = "AZE0"; }
    if (battery_type == 2) { status = "ZE1"; }
    content += "Battery type: " + String(status) + "<br>";
    return content;
  }


  if(var == "TIMER_ENABLED_0") {
    String content = "";
    if (timer_enabled == 0) { content = "selected"; }
    return content;
  } 
  if(var == "TIMER_START") {
    return String(timer_start);
  } 
  if(var == "TIMER_STOP") {
    return String(timer_stop);
  } 
  if(var == "TIMER_SOC") {
    return String(timer_soc);
  } 
  if(var == "TIMER_ENABLED_1") {
    String content = "";
    if (timer_enabled == 1) { content = "selected"; }
    return content;
  } 
  if(var == "DIV_MESSAGE_CLASS") {
    String content = "";
    if (html_message == "") {
      content = "hidden";      
    } else {
      content = "border";      
    }
    return content;
  } 

  if(var == "MESSAGE") {
    return html_message;
  } 


  
  return String();
}

