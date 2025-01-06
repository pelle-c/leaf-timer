#include "esp_system.h"
#include <Arduino.h>
#include <ESP32CAN.h>
#include <CAN_config.h>
#include <WiFi.h>
//#include <WiFiAP.h>
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include <ctime>
#include <Adafruit_NeoPixel.h>
#include <cstring>
#include "html_templates.cpp"

#define CONFIG_ASYNC_TCP_MAX_ACK_TIME=5000   // (keep default)
#define CONFIG_ASYNC_TCP_PRIORITY=10         // (keep default)
#define CONFIG_ASYNC_TCP_QUEUE_SIZE=64       // (keep default)
#define CONFIG_ASYNC_TCP_RUNNING_CORE=1      // force async_tcp task to be on same core as Arduino app (default is any core)
#define CONFIG_ASYNC_TCP_STACK_SIZE=4096     // reduce the stack size (default is 16K)


const char *ssid = "lily";
const char *password = "0123456789";

AsyncWebServer server(80);

CAN_device_t CAN_cfg;               // CAN Config
unsigned long previousMillis = 0;   // will store last time a CAN Message was send
const int interval_base = 100;       // at which interval to send CAN Messages (milliseconds)
const int interval_bat = 50;        // n * interval_base at which interval to send CAN Messages to battery for detailed info
const int rx_queue_size = 10;       // Receive Queue size
const int interval_check_timer = 10;// n * interval_base at which interval to check the timer

#define CAN_SE_PIN 23
#define WAKE_UP_PIN 25

#define LED_PIN 4
#define PIXEL 0
#define NUMPIXELS 1
Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

#define LEAF_ZE0 0
#define LEAF_AZE0 1
#define LEAF_ZE1 2

#define LOG_BUFFER_SIZE 8192

int state_wake_up_pin = 0;

char is_plugged_in = 0;
char is_charging = 0;
char status_car_off = 1;
char status_lb_failsafe = 0;
float status_soc = 0;
float status_soh = 0;
int battery_type = LEAF_ZE0;

int timer_enabled = 0;
String timer_start = "23:00";
String timer_stop = "05:00";
String html_message = "";
int timer_soc = 80;
int soc_hysteresis = 0;

long timestamp_last_can_received = -600;
long last_wake_up_timestamp = 0;
long retry_wake_up_at = 0;
u_char group_message[255];
char group = 0;
int pointer_group_message = 0;
int counter_check_timer = 0;
int counter_bat = 0;
unsigned long last_group_request_millis = 0;
char log_buffer[LOG_BUFFER_SIZE];
int pointer_log_buffer = 0;
int log_semaphore = 0;
u_char old_can_1db[16];
int retry_stop_charge_counter = 0;

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
  state_wake_up_pin = 0;
  digitalWrite(WAKE_UP_PIN,state_wake_up_pin);
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
  if (state_wake_up_pin == 1) {
    logger("Clearing wake-up bit");
    state_wake_up_pin = 0;
    digitalWrite(WAKE_UP_PIN,state_wake_up_pin);
  }
}

bool set_wake_up() {
  if (state_wake_up_pin == 0) {
    logger("Setting wake-up bit");
    state_wake_up_pin = 1;
    digitalWrite(WAKE_UP_PIN,state_wake_up_pin);
    return true;
  } 
  return false;
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
  // = [0x00,0x02,0x00,0x10,0x00,0x00,0xe3,0xdf];
  old_can_1db[0] = 0x00;
  old_can_1db[1] = 0x02;
  old_can_1db[2] = 0x00;
  old_can_1db[3] = 0x10;
  old_can_1db[4] = 0x00;
  old_can_1db[5] = 0x00;
  old_can_1db[6] = 0xe3;
  old_can_1db[7] = 0xdf;
  logger("Initialized can bus");
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

void logger(String s) {
  Serial.println(s);
  while (log_semaphore == 1) {
    delay(1);
  }
  log_semaphore = 1;
  if (pointer_log_buffer + s.length() + 20 < LOG_BUFFER_SIZE ) {
    struct timeval tv;
    gettimeofday(&tv, NULL); 
    double ts = (1.0 * tv.tv_sec + 0.000001 * tv.tv_usec);
    char temp_char[255];
    sprintf(temp_char,"%.3f - %s<br>",ts,s.c_str());
    for (int i = 0; i < strlen(temp_char) + 1; i++) {
      log_buffer[i + pointer_log_buffer] = temp_char[i];
    }
    pointer_log_buffer += strlen(temp_char);  
  } else {
    log_semaphore = 0;
    pointer_log_buffer = 0;
    logger("Log full, rotated");
  }
  log_semaphore = 0;
}

String get_log() {
  return String(log_buffer);
}

unsigned char leafcrc(int l, unsigned char * b){
  const unsigned char crcTable[]={0x0, 0x85, 0x8F, 0x0A, 0x9B, 0x1E, 0x14, 0x91, 0xB3, 0x36, 0x3C, 0xB9, 0x28, 0xAD, 0xA7, 0x22,
                         0xE3, 0x66, 0x6C, 0xE9, 0x78, 0xFD, 0xF7, 0x72, 0x50, 0xD5, 0xDF, 0x5A, 0xCB, 0x4E, 0x44, 0xC1,
                         0x43, 0xC6, 0xCC, 0x49, 0xD8, 0x5D, 0x57, 0xD2, 0xF0, 0x75, 0x7F, 0xFA, 0x6B, 0xEE, 0xE4, 0x61,
                         0xA0, 0x25, 0x2F, 0xAA, 0x3B, 0xBE, 0xB4, 0x31, 0x13, 0x96, 0x9C, 0x19, 0x88, 0x0D, 0x07, 0x82,
                         0x86, 0x03, 0x09, 0x8C, 0x1D, 0x98, 0x92, 0x17, 0x35, 0xB0, 0xBA, 0x3F, 0xAE, 0x2B, 0x21, 0xA4,
                         0x65, 0xE0, 0xEA, 0x6F, 0xFE, 0x7B, 0x71, 0xF4, 0xD6, 0x53, 0x59, 0xDC, 0x4D, 0xC8, 0xC2, 0x47,
                         0xC5, 0x40, 0x4A, 0xCF, 0x5E, 0xDB, 0xD1, 0x54, 0x76, 0xF3, 0xF9, 0x7C, 0xED, 0x68, 0x62, 0xE7,
                         0x26, 0xA3, 0xA9, 0x2C, 0xBD, 0x38, 0x32, 0xB7, 0x95, 0x10, 0x1A, 0x9F, 0x0E, 0x8B, 0x81, 0x04,
                         0x89, 0x0C, 0x06, 0x83, 0x12, 0x97, 0x9D, 0x18, 0x3A, 0xBF, 0xB5, 0x30, 0xA1, 0x24, 0x2E, 0xAB,
                         0x6A, 0xEF, 0xE5, 0x60, 0xF1, 0x74, 0x7E, 0xFB, 0xD9, 0x5C, 0x56, 0xD3, 0x42, 0xC7, 0xCD, 0x48,
                         0xCA, 0x4F, 0x45, 0xC0, 0x51, 0xD4, 0xDE, 0x5B, 0x79, 0xFC, 0xF6, 0x73, 0xE2, 0x67, 0x6D, 0xE8,
                         0x29, 0xAC, 0xA6, 0x23, 0xB2, 0x37, 0x3D, 0xB8, 0x9A, 0x1F, 0x15, 0x90, 0x01, 0x84, 0x8E, 0x0B,
                         0x0F, 0x8A, 0x80, 0x05, 0x94, 0x11, 0x1B, 0x9E, 0xBC, 0x39, 0x33, 0xB6, 0x27, 0xA2, 0xA8, 0x2D,
                         0xEC, 0x69, 0x63, 0xE6, 0x77, 0xF2, 0xF8, 0x7D, 0x5F, 0xDA, 0xD0, 0x55, 0xC4, 0x41, 0x4B, 0xCE,
                         0x4C, 0xC9, 0xC3, 0x46, 0xD7, 0x52, 0x58, 0xDD, 0xFF, 0x7A, 0x70, 0xF5, 0x64, 0xE1, 0xEB, 0x6E,
                         0xAF, 0x2A, 0x20, 0xA5, 0x34, 0xB1, 0xBB, 0x3E, 0x1C, 0x99, 0x93, 0x16, 0x87, 0x02, 0x08, 0x8D



  };
  unsigned char crc = 0;
  for (int i = 0; i < l; i++)
    crc = crcTable[crc ^ b[i]];
  return crc;
}


void check_can_bus() {
  CAN_frame_t rx_frame;  
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
        for (int i = 0; i < rx_frame.FIR.B.DLC; i++) {
          old_can_1db[i] = rx_frame.data.u8[i];
        }
        status_lb_failsafe = (rx_frame.data.u8[1] & 0x07);
        //Serial.printf("LB_Failsafe (0=OK):%d\n",status_lb_failsafe);     
      }  
      if (rx_frame.MsgID == 0x5bc) { // HVBAT
          status_soh = rx_frame.data.u8[4] >> 1;
          //Serial.printf("SoH:%.2f\n",status_soh);
      }
      if (rx_frame.MsgID == 0x5bf) { // On board charger information
        if (rx_frame.data.u8[2] > 0) {
          is_plugged_in = 1;
        } else {
          is_plugged_in = 0;
        } 
        if (rx_frame.data.u8[1] > 0) {
          is_charging = 1;
        } else {
          is_charging = 0;
        }
        //Serial.printf("OBC message: plugged in:%d, charging:%d\n",is_plugged_in,is_charging);       
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
}


void loop() {

  check_can_bus();

/*
State:
No can messages -> sleeping
can-messages - car-on/car-off, plugged in/not

No can, no timer => do nothing
No can, timer => wake up...retry_at

Can, no timer, not charging => do nothing
Can, no timer, charging => stop charging
can, timer, not charging, plugged in => check soc-start charge
can, timer, charging, plugged_in => check soc-stop charge

vars:
- timer_is_active
- is_charging
- is_plugged_in
- vcm_is_sleeping (no can messages)

  When charging: check soc all the time
  Not charging, plugged in: check soc every 15 min 
  Handle: wake up, when not plugged in - set retry timer!
  TODO: test situations below
  TODO: "Stop charging" will send multiple can messages, emulating a battery full message from the LBC. After this, the VCM has to go to sleep before starting to charge again.
*/

  // Interval tasks
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval_base) {
    previousMillis = currentMillis;
    counter_check_timer++;  
    counter_bat++;

    if (counter_check_timer > interval_check_timer) { // check timer data
      Serial.println("Checking timer");     
      if (vcm_is_sleeping()) {
        retry_stop_charge_counter = 0;
        if ((timer_is_active()) && (timestamp_now() > retry_wake_up_at)) {
          bool changed = set_wake_up();
          if (changed) {
            last_wake_up_timestamp = timestamp_now();
          }
        }
        if ((timer_is_active()) && (timestamp_now() > last_wake_up_timestamp + 60) && (timestamp_now() > retry_wake_up_at)) { // No can messages, but should be awake
          logger("VCM should be awake, but I'm not getting anything. Let's sleep and check later.");
          clear_wake_up();
          retry_wake_up_at = timestamp_now() + 600;
        }
      } else { // can bus is active
        if ((!timer_is_active()) && (is_charging)) {
          if (retry_stop_charge_counter < 5) {
            if (retry_stop_charge_counter == 0) { logger("Timer is not active, trying to stop."); }
            send_can_command("stop_charging");
            clear_wake_up();
            retry_wake_up_at = timestamp_now() + 900; // The VCM needs to be asleep a while before charging can be turned on again
            retry_stop_charge_counter++;
          } else {
            if (retry_stop_charge_counter < 6) {
              logger("Failed to stop charging when timer is not active, giving up now.");
              retry_stop_charge_counter++;
            }
          }
        }
        if ((!timer_is_active()) && (!is_charging)) {
          retry_stop_charge_counter = 0;
          clear_wake_up();
        }
        if ((timer_is_active()) && (!is_charging) && (status_soc<(timer_soc - soc_hysteresis)) && (is_plugged_in)) {
          logger("Timer is active and soc is lower than wanted, let's start to charge.");
          send_can_command("start_charging");
          soc_hysteresis = 0;
        }
        if ((timer_is_active()) && (is_charging) && (status_soc>timer_soc) ) {
          soc_hysteresis = 2;
          Serial.println(String(retry_stop_charge_counter));
          if (retry_stop_charge_counter < 5) {
            if (retry_stop_charge_counter == 0) { logger("Wanted SoC has been reached, trying to stop."); }
            send_can_command("stop_charging");
            clear_wake_up();
            retry_stop_charge_counter++;
          } else {
            if (retry_stop_charge_counter < 6) {
              logger("Failed to stop charging when wanted SoC has been reached, giving up now.");
              retry_stop_charge_counter++;
            }
          }
        }
        if ((timer_is_active()) && (!is_plugged_in)) {
          logger("Can't start to charge - not plugged in or vcm is sleeping. Let's sleep and check later.");
          clear_wake_up();
          retry_wake_up_at = timestamp_now() + 600;
        }
      }

      counter_check_timer = 0;
    }

    if ((timer_is_active()) && (!vcm_is_sleeping()) && (counter_bat > interval_bat)) { // Request battery information
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
  CAN_frame_t tx_frame;
  tx_frame.FIR.B.FF = CAN_frame_std;
  int num_frames_to_send = 25;
  int interval_frame = 100;

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
    unsigned char new1db[8];
    tx_frame.MsgID = 0x1db;
    tx_frame.FIR.B.DLC = 8;
    for (int i = 0; i < tx_frame.FIR.B.DLC; i++) {
      new1db[i] = old_can_1db[i];
    }
    new1db[1] = (new1db[1] & 0xe0) | 2; // Charging Mode Stop Request
    new1db[6] = (new1db[6] + 1) % 4; // Increment PRUN counter
    new1db[7] = leafcrc(7, new1db); // Sign message with correct CRC
    for (int i = 0; i < tx_frame.FIR.B.DLC; i++) {
      tx_frame.data.u8[i] = new1db[i];
    }
    interval_frame = 1;
    num_frames_to_send = 10;
  }
 
  logger("Sending can messages for:" + command);
  int counter = 0;
  while (counter < num_frames_to_send) {
    ESP32Can.CANWriteFrame(&tx_frame);
    delay(interval_frame);
    counter++;
  }
}


bool vcm_is_sleeping() {
  int ts_now = timestamp_now();
  if (ts_now > (timestamp_last_can_received + 10)) {
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
  String h = String(timeinfo.tm_hour);
  String m = String(timeinfo.tm_min);
  if (timeinfo.tm_hour < 10) {
    h = "0" + h;
  }
  if (timeinfo.tm_min < 10) {
    m = "0" + m;
  }
  return h + ":" + m;
}

int get_hour(String time) {
  char temp_char1[255];
  sprintf(temp_char1,"%s",time.c_str());
  std::string temp_string = temp_char1;
  std::string hour = temp_string.substr(0, temp_string.find(":"));
  char temp_char2[255];
  sprintf(temp_char2,"%s",hour.c_str());
  return string2int(temp_char2);
}

int get_minute(String time) {
  char temp_char1[255];
  sprintf(temp_char1,"%s",time.c_str());
  std::string temp_string = temp_char1;
  std::string minute = temp_string.substr(temp_string.find(":") + 1, temp_string.length() - temp_string.find(":") - 1);
  char temp_char2[255];
  sprintf(temp_char2,"%s",minute.c_str());
  return string2int(temp_char2);
}


bool timer_is_active() {
  bool ret = false;
  if (timer_enabled == 0) {
    return false;
  }
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
  return ret;

}

int string2int(String temp_string) {
  char temp_char[255];
  sprintf(temp_char,"%s",temp_string.c_str());
  std::string::size_type sz;
  int temp = std::stoi(temp_char,&sz);
  return temp;
}


long string2long(String temp_string) {
  char temp_char[255];
  sprintf(temp_char,"%s",temp_string.c_str());
  std::string::size_type sz;
  long temp = std::stol(temp_char,&sz);
  return temp;
}

// Web-server related below...

void notFound(AsyncWebServerRequest* request) {
  request->send(404, "text/plain", "Not found");
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
    if (vcm_is_sleeping()) {
      bool changed = set_wake_up();
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
    logger("Timer set");
  });

  server.on("/clock_set", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (request->hasParam("timestamp")) {
      long timestamp = string2long(request->getParam("timestamp")->value());
      struct timeval tv_temp;
      tv_temp.tv_sec = timestamp; 
      tv_temp.tv_usec = 0; 
      settimeofday(&tv_temp, NULL);
    } else {
      html_message = "No timestamp found in request";
    }
    html_message = "Clock set!";
    request->send_P(200, "text/html", message_html, processor);
    logger("Clock set");
  });

  server.on("/log", HTTP_GET, [](AsyncWebServerRequest* request) {
    while (log_semaphore == 1) {
      delay(1);
    }
    log_semaphore = 1;
    request->send_P(200, "text/html", log_html, processor);
    log_semaphore = 0;
  });

  server.onNotFound(notFound);

  server.begin();
}


String processor(const String& var)
{
  if(var == "LINKS") {
    String content = "<a href='/' class='button'>Overview</a>&nbsp;<a href='/timer' class='button'>Set timer</a>&nbsp;<a href='/clock' class='button'>Set clock</a>&nbsp;<a href='/control' class='button'>Manual Control</a>&nbsp;<a href='/log' class='button'>Log</a><br><hr>";
    return content;
  }

  if(var == "DIV_STATUS_STYLE") {
    String content = "";
    if (timestamp_last_can_received <= 0) {
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
    if (is_plugged_in == 1) { status = "Yes"; }
    if (is_plugged_in == 0) { status = "No"; }
    content += "Plugged in: " + String(status) + "<br>";
    return content;
  } 
  if(var == "CAN_DATA") {
    String content = "";
    String font_p = "<p style='color:MediumSeaGreen;'>";
    if ((seconds_since_can_data(timestamp_last_can_received) > 60) or (timestamp_last_can_received == 0)) {
      font_p = "<p style='color:red;'>";
    }
    if (timestamp_last_can_received <= 0) {
      content += String(font_p) + "No can messages seen yet</p>";
    } else {
      content += String(font_p) + "Last can message seen: " + String(seconds_since_can_data(timestamp_last_can_received)) + "s ago</p>";
    }
    return content;
  } 
  if(var == "TIMER") {
    String content = "";
    if (timer_enabled == 1) {
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
    if (is_charging == 1) { status = "Yes"; }
    if (is_charging == 0) { status = "No"; }
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

  if(var == "LOG") {
    return get_log();
  } 
  
  return String();
}

