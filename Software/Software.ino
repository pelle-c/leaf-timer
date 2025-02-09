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
#include "SETTINGS.h"
#include "Preferences.h"
#include "src/sdcard.h"


#define CONFIG_ASYNC_TCP_MAX_ACK_TIME=5000   // (keep default)
#define CONFIG_ASYNC_TCP_PRIORITY=10         // (keep default)
#define CONFIG_ASYNC_TCP_QUEUE_SIZE=64       // (keep default)
#define CONFIG_ASYNC_TCP_RUNNING_CORE=1      // force async_tcp task to be on same core as Arduino app (default is any core)
#define CONFIG_ASYNC_TCP_STACK_SIZE=4096     // reduce the stack size (default is 16K)

//#define RX_FRANE 0
//#define X_FRANE 1
enum FRAME_DIRECTION { RX_FRAME, TX_FRAME }; 

enum COMMAND_TYPE { NONE,START_CHARGING,STOP_CHARGING,STOP_CHARGING_NOW,START_ACC,STOP_ACC,WAKE_UP,IDLE_CAR_ON,IDLE }; 


//const char *ssid = AP_SSID;
//const char *password = AP_PASSWORD;

char ap_ssid[64];
char ap_password[64];
char wlan_ssid[64];
char wlan_password[64];


AsyncWebServer server(80);

CAN_device_t CAN_cfg;               // CAN Config
const int interval_base = 100;       // at which interval to send CAN Messages (milliseconds)
const int interval_bat = 100;        // n * interval_base at which interval to send CAN Messages to battery for detailed info
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

int counter_can = 0;
int counter_retry_wlan = 3;
int command2send = 0;
int candump = 0;
int state_wake_up_pin = 0;
int charge_complete = 0;
char is_plugged_in = 0;
char is_charging = 0;
int status_car = 0;
char status_lb_failsafe = 0;
float status_soc = 0;
float hq_status_soc = 0;
float status_soh = 0;
int battery_type = LEAF_ZE0;
float hv_voltage = 0;
float hv_current = 0;
float hq_hv_voltage = 0;
float hq_hv_current = 0;
int timer_enabled = 0;
int wlan_connect = 0;
String timer_start = "23:00";
String timer_stop = "05:00";
String html_message = "";
int timer_soc = 80;
int soc_hysteresis = 0;

int last_steering_wheel_button = 0;
float cc_outside_temp = 0;
float cc_inside_temp = 0;
float cc_set_temp = 0;
int cc_status = 0;
int cc_fanspeed = 0;
int cc_fan_target = 0;
int cc_fan_intake = 0;
long timestamp_last_soc_received = -600;  // Timestamp when can message with soc was rec
long timestamp_last_11a_received = -600;  // Timestamp when can message with 11a was rec
long timestamp_last_1d4_received = -600;  // Timestamp when can message with 1d4 was rec
long timestamp_car_went_on = -600;  // Timestamp when can off->on
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
char web_log_buffer[LOG_BUFFER_SIZE];
int pointer_web_log_buffer = 0;
char can_log_buffer1[LOG_BUFFER_SIZE];
char can_log_buffer2[LOG_BUFFER_SIZE];
int can_log_buffer_rx = 1;
int pointer_can_log_buffer = 0;

int log_semaphore = 0;
u_char old_can_1db[16];
int retry_stop_charge_counter = 0;
int previous_loop_timer_active = 0;
int should_send_stop_charge = 0;

TaskHandle_t th_wifi_loop_task;
#define CORE_WIFI 0
#define TASK_WIFI_PRIO 2

TaskHandle_t th_logging_loop_task;
#define CORE_LOGGING 0
#define TASK_LOGGING_PRIO 8

TaskHandle_t th_can_rx_loop_task;
#define CORE_MAIN 1
#define TASK_CANRX_PRIO 6

TaskHandle_t th_can_tx_loop_task;
#define CORE_MAIN 1
#define TASK_CANTX_PRIO 4

//#define TASK_CORE_PRIO 4
//#define TASK_MODBUS_PRIO 8


void setup() {
  read_preferences();
  init_time();
  blink_led();
  Serial.begin(115200);
  Serial.println();
  Serial.println("Starting Leaf timer");
  Serial.println("Configuring access point...");
  pinMode(WAKE_UP_PIN, OUTPUT);  
  state_wake_up_pin = 0;
  digitalWrite(WAKE_UP_PIN,state_wake_up_pin);
  show_led_wakeup_pin(0);
  init_can();
  createCoreTasks();
  init_sdcard();
}

void createCoreTasks() {
  xTaskCreatePinnedToCore(wifi_loop, "wifi_loop", 4096, NULL,
                          TASK_WIFI_PRIO, &th_wifi_loop_task, CORE_WIFI);
  xTaskCreatePinnedToCore(logging_loop, "logging_loop", 4096, NULL,
                          TASK_LOGGING_PRIO, &th_logging_loop_task, CORE_LOGGING);
  xTaskCreatePinnedToCore((TaskFunction_t)&can_rx_loop, "can_rx_loop", 4096, NULL,
                          TASK_CANRX_PRIO, &th_can_rx_loop_task, CORE_MAIN);
  xTaskCreatePinnedToCore((TaskFunction_t)&can_tx_loop, "can_tx_loop", 4096, NULL,
                          TASK_CANTX_PRIO, &th_can_tx_loop_task, CORE_MAIN);
}

void shutdown_tasks() {
   vTaskDelete( th_can_rx_loop_task );
   vTaskDelete( th_can_tx_loop_task );
   vTaskDelete( th_logging_loop_task );
   vTaskDelete( th_wifi_loop_task );
}


void can_rx_loop() {
  unsigned long previousMillis = 0; 

  while (true) {
    check_can_bus();

    if (timestamp_last_11a_received - timestamp_now() > 1) {
      status_car = 2;
    }

    if (is_charging) {
      status_car = 0;
    }

    if (FAKE_TCU == 1) {
      // Car is on, wakeup pin should go low->high->low
      if (status_car == 1) { 
        if ((timestamp_now() - timestamp_car_went_on) < 3) {
          clear_wake_up();
        }
        if ( ((timestamp_now() - timestamp_car_went_on) < 8) && ((timestamp_now() - timestamp_car_went_on) >3) ) {
          set_wake_up();
        }
        if ((timestamp_now() - timestamp_car_went_on) > 8) {
          clear_wake_up();
        }
      }
    }

    // Reset status
    if (vcm_is_sleeping()) {
      hq_hv_current = 0;
      hq_hv_voltage = 0;
      hq_status_soc = 0;
      is_charging = 0;
    }

    // Interval tasks
    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= interval_base) {
      previousMillis = currentMillis;
      counter_check_timer++;  
      counter_bat++;

      if (FAKE_TCU == 1) {
        // Send idle can message when vcm is awake - fool car that we still have a TCU
        if (!vcm_is_sleeping()) {
          if (status_car != 1) { clear_wake_up(); }
          if (!is_charging) {
            if (status_car == 1) {
              send_can_command(IDLE_CAR_ON); // 0x46 is sent from TCU when car is on
            } else {
              send_can_command(IDLE); // 0x86 is sent from TCU when car is off
            } 
          } else {
              send_can_command(IDLE);
          }
        }
      }

      // Fix if FAKING TCU...
      if (!vcm_is_sleeping()) {
        clear_wake_up();
      }


      // Timer is enabled, unless car is on

      if ( (status_car != 1) && (timer_is_enabled()) ) {
        // check timer data
        if (counter_check_timer > interval_check_timer) { 
          Serial.printf("Checking timer\n");     
          if (!timer_is_active()) {
            previous_loop_timer_active = 0;
          }

          // If timer has changed from inactive to active, wake up the vcm (maybe we missed plugged in state etc)
          if ( (timer_is_active()) && (previous_loop_timer_active == 0) && (vcm_is_sleeping()) ) {
            logger("Timer went to active state, VCM is asleep - wake up...");
            bool changed = set_wake_up();
            previous_loop_timer_active = 1;
          }

          // Since the car will always try to charge when we plug in, we will probably know if it's plugged in or not. And a fairly updated SoC.
          if ((timer_is_active()) && (status_soc<(timer_soc - soc_hysteresis)) && (is_plugged_in)) { // Only wake up when timer is enabled and soc is low
            if (vcm_is_sleeping()) {
              retry_stop_charge_counter = 0;
              if ((timestamp_now() > retry_wake_up_at)) {
                logger("Timer is active, VCM is asleep");
                bool changed = set_wake_up();
              }
              if ((timestamp_now() > last_wake_up_timestamp + 60) && (timestamp_now() > retry_wake_up_at)) { // No can messages, but should be awake
                logger("VCM should be awake, but I'm not getting anything.");
                clear_wake_up();
                retry_wake_up_at = timestamp_now() + 600;
              }
            } else { // can bus is active, soc is low
              if ((!is_charging) && (status_soc<(timer_soc - soc_hysteresis)) ) {
                logger("Timer is active and soc is lower than wanted, let's start to charge.");
                send_can_command(START_CHARGING);
                charge_complete = 0;
                soc_hysteresis = 0;
              }
            } 
          } 

          if ((timer_is_active()) && (is_charging) && (status_soc>timer_soc) ) {
            charge_complete = 1;
            soc_hysteresis = 2;
            Serial.println(String(retry_stop_charge_counter));
            if (retry_stop_charge_counter < 100) {
              if (retry_stop_charge_counter == 0) { logger("Wanted SoC has been reached, trying to stop."); }
              send_can_command(STOP_CHARGING);
              retry_stop_charge_counter++;
            } else {
              if (retry_stop_charge_counter < 101) {
                logger("Failed to stop charging when wanted SoC has been reached, giving up now.");
                retry_stop_charge_counter++;
              }
            }
          }
          // when cc is on and plugged in, it will show charging
          if ((!timer_is_active()) && (is_charging) && (!cc_is_on())) {
            charge_complete = 1;
            if (retry_stop_charge_counter < 100) {
              if (retry_stop_charge_counter == 0) { logger("Timer is not active, trying to stop charging."); }
              send_can_command(STOP_CHARGING);
              retry_wake_up_at = timestamp_now() + 900; // The VCM needs to be asleep a while before charging can be turned on again
              retry_stop_charge_counter++;
            } else {
              if (retry_stop_charge_counter < 101) {
                logger("Failed to stop charging when timer is not active, giving up now.");
                retry_stop_charge_counter++;
              }
            }
          }
          // Stopped and disabled charging, let's go back to normal
          if ( (charge_complete == 1) && (!is_charging) ) {
            charge_complete = 2;
            logger("No longer charging, go back to normal");
          }
          counter_check_timer = 0;
        }
      }

      // When charging, request battery details
      if (GET_HIGH_PRECISION_INFO_FROM_BATTERY) {
        if ((is_charging) && (!vcm_is_sleeping()) && (counter_bat > interval_bat)) { 
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
          Serial.println("Sending 79b");
          send_can_frame(tx_frame);     
        }
      }
    }
  }
}


void loop() {
  vTaskDelay(10);
}


void wifi_loop(void* task_time_us) {
  init_ap();
  init_webserver();
  while (true) {
    vTaskDelay(2000);
    if (counter_retry_wlan > 0) {
      if (!wifi_connected()) {
        if (wlan_connect == 1) {
          connect_wlan();
        }
        counter_retry_wlan--;
      }
    }
  }
}


void logging_loop(void* task_time_us) {
  unsigned long timestamp_prev_clear = 0;
  unsigned long previousMillis = 0; 

  while (true) {

    unsigned long currentMillis = millis();
    long diffMillis = currentMillis - previousMillis;

    if ((diffMillis >= 10) || (pointer_can_log_buffer > 4096)) {
      previousMillis = currentMillis;

      // Logbuffer for webserver

      if(pointer_log_buffer > 0) {
        if (pointer_web_log_buffer + pointer_log_buffer + 20 < LOG_BUFFER_SIZE ) {
          for (int i = 0; i < pointer_log_buffer + 1; i++) {
            web_log_buffer[i + pointer_web_log_buffer] = log_buffer[i];
          }
          pointer_web_log_buffer += pointer_log_buffer;  
        } else {
          String s = "Log full, rotated<br>";
          char temp_char[30];
          sprintf(temp_char,"%s",s.c_str());
          for (int i = 0; i < strlen(temp_char) + 1; i++) {
            web_log_buffer[i + pointer_web_log_buffer] = temp_char[i];
          }
          pointer_web_log_buffer = strlen(temp_char);        
        }
        pointer_log_buffer = 0;
      }

      // Logbuffer for canframes (save to sd card)
      if(pointer_can_log_buffer > 0) {
        int old_buffer_rx = can_log_buffer_rx;
        int old_pointer = pointer_can_log_buffer;
        if (can_log_buffer_rx == 1) {
          can_log_buffer_rx = 2;
        } else {
          can_log_buffer_rx = 1;
        }
        pointer_can_log_buffer = 0;
        if (get_candump_status()) { 
          if (old_buffer_rx == 1) {
//            Serial.printf("Old P1a:%d - %d,%d\n",old_pointer,can_log_buffer1[old_pointer-1],can_log_buffer1[old_pointer]);
            log2sd(can_log_buffer1,old_pointer); 
          } else {
//            Serial.printf("Old P1b:%d - %d,%d\n",old_pointer,can_log_buffer2[old_pointer-1],can_log_buffer2[old_pointer]);
            log2sd(can_log_buffer2,old_pointer); 
          }   
        }
      }
    }
    vTaskDelay(1);
  }
}

void logger(String s) {
  time_t now = time(NULL);
  struct tm *tm_struct = localtime(&now);
  int hour = tm_struct->tm_hour;
  int min = tm_struct->tm_min;
  int sec = tm_struct->tm_sec;

  if (pointer_log_buffer + s.length() + 20 < LOG_BUFFER_SIZE ) {
    char temp_char[255];
    sprintf(temp_char,"%02d:%02d:%02d - %s<br>",hour,min,sec,s.c_str());
    for (int i = 0; i < strlen(temp_char) + 1; i++) {
      log_buffer[i + pointer_log_buffer] = temp_char[i];
    }
    pointer_log_buffer += strlen(temp_char);  
  } 
}

void logger_can(CAN_frame_t frame, FRAME_DIRECTION fdir) {
  char temp_char[255];
  char temp_string[255];
  char hexdata[255];
  char log_string[255];
  struct timeval tv;
  gettimeofday(&tv, NULL); 
  double ts = (1.0 * tv.tv_sec + 0.000001 * tv.tv_usec);
  char time_stamp[255];
  sprintf(time_stamp,"%.3f",ts);
  for (int i = 0; i < frame.FIR.B.DLC; i++) {
    unsigned char b = frame.data.u8[i];
    sprintf(temp_char,"%02X",b);
    hexdata[i * 2 + 0] = temp_char[0];
    hexdata[i * 2 + 1] = temp_char[1];
  }
  hexdata[frame.FIR.B.DLC * 2 + 0] = 0;
  sprintf(temp_string,"%03X",frame.MsgID);
  char direction[10];
  sprintf(direction,"RX");
  if (fdir == TX_FRAME) { sprintf(direction,"TX"); }
  sprintf(log_string,"(%s) %s %s#%s\n",time_stamp,direction,temp_string,hexdata);
  if (pointer_can_log_buffer + strlen(log_string) + 20 < LOG_BUFFER_SIZE ) {
    for (int i = 0; i < strlen(log_string) + 1; i++) {
      if (can_log_buffer_rx == 1) {
        can_log_buffer1[i + pointer_can_log_buffer] = log_string[i];
      } else {
        can_log_buffer2[i + pointer_can_log_buffer] = log_string[i];
      }
    }
    pointer_can_log_buffer += strlen(log_string);  
  } 
  counter_can++;

//  Serial.println(can_log_buffer);
//if (get_candump_status()) { 

}



void read_preferences() {
  Preferences preferences;
  char temp_string[64];

  preferences.begin("leaf-timer", true);
  timer_enabled = preferences.getUInt("timer_enabled", 0);
  logger("Timer enabled:" + String(timer_enabled));
  timer_soc = preferences.getUInt("timer_soc", 0);
  size_t l_string = preferences.getString("timer_start", temp_string, sizeof(temp_string));
  if (l_string > 0) { 
    timer_start = temp_string;
  } else {
    timer_start = "23:15";
  }
  l_string = preferences.getString("timer_stop", temp_string, sizeof(temp_string));
  if (l_string > 0) { 
    timer_stop = temp_string;
  } else {
    timer_stop = "05:15";
  }

  l_string = preferences.getString("ap_password", temp_string, sizeof(temp_string));
  if (l_string > 0) { 
    memcpy(ap_password,temp_string,sizeof(temp_string));
  } else {
    memcpy(ap_password,AP_PASSWORD_DEFAULT,sizeof(AP_PASSWORD_DEFAULT));
  }
  memcpy(ap_ssid,AP_SSID_DEFAULT,sizeof(AP_SSID_DEFAULT));

  wlan_connect = preferences.getUInt("wlan_connect", 0);
  l_string = preferences.getString("wlan_ssid", temp_string, sizeof(temp_string));
  if (l_string > 0) { 
    memcpy(wlan_ssid,temp_string,sizeof(temp_string));
  } else {
    wlan_connect = 0;
  }
  l_string = preferences.getString("wlan_password", temp_string, sizeof(temp_string));
  if (l_string > 0) { 
    memcpy(wlan_password,temp_string,sizeof(temp_string));
  } else {
    wlan_connect = 0;
  }

  preferences.end();
}

void write_preferences() {
  Preferences preferences;
  preferences.begin("leaf-timer", false);
  preferences.putUInt("timer_enabled", timer_enabled);
  preferences.putUInt("timer_soc", timer_soc);
  preferences.putString("timer_start", String(timer_start.c_str()));
  preferences.putString("timer_stop", String(timer_stop.c_str()));
  preferences.putString("ap_password", String(ap_password));
  preferences.putUInt("wlan_connect", wlan_connect);
  preferences.putString("wlan_password", String(wlan_password));
  preferences.putString("wlan_ssid", String(wlan_ssid));
  preferences.end();
}


void blink_led() {
  pixels.begin();
  pixels.clear();
  //int PIXEL = 0;
  for(int i=0; i<200; i++) {
    pixels.setPixelColor(PIXEL, pixels.Color(0, i, 0));
    pixels.show();
    vTaskDelay(2);
  }
  for(int i=200; i>-1; i--) {
    pixels.setPixelColor(PIXEL, pixels.Color(0, i, 0));
    pixels.show();
    vTaskDelay(5);
  }
  pixels.clear();
}

void show_led_wakeup_pin(int state) {
  pixels.setPixelColor(PIXEL, pixels.Color(state*50, state*50, 0));
  pixels.show();
}

void clear_wake_up() {
  if (state_wake_up_pin == 1) {
    logger("Clearing wake-up bit");
    state_wake_up_pin = 0;
    digitalWrite(WAKE_UP_PIN,state_wake_up_pin);
    show_led_wakeup_pin(0);
  }
}

bool set_wake_up() {
  if (state_wake_up_pin == 0) {
    logger("Setting wake-up bit");
    state_wake_up_pin = 1;
    digitalWrite(WAKE_UP_PIN,state_wake_up_pin);
    last_wake_up_timestamp = timestamp_now();
    show_led_wakeup_pin(1);
    // wake up using can...
    send_can_command(WAKE_UP);

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
  logger("Initialized can bus");
  // = [0x00,0x02,0x00,0x10,0x00,0x00,0xe3,0xdf];
  old_can_1db[0] = 0x00;
  old_can_1db[1] = 0x00;
  old_can_1db[2] = 0xc0;
  old_can_1db[3] = 0xea;
  old_can_1db[4] = 0x00;
  old_can_1db[5] = 0x00;
  old_can_1db[6] = 0x02;
  old_can_1db[7] = 0x00;
}

void send_can_stop_charge() {
  // We use the frame that the LBC just sent, and modify a few bits (like ovms does).
  // But, instead of spamming. We send eight frames (1ms interval) right after a 1db frame
  // has been received. By sending eight, the next message from the LBC will have a correct
  // prune counter. The interval for the LBC for 1db frames is 10ms, so we will fit those eight
  // before next one is sent.
  CAN_frame_t tx_frame;
  unsigned char new1db[8];
  logger("Sending stop_charge frames");
  tx_frame.FIR.B.FF = CAN_frame_std;
  tx_frame.MsgID = 0x1db;
  tx_frame.FIR.B.DLC = 8;
  for (int j = 0; j < 8; j++) {  
    for (int i = 0; i < tx_frame.FIR.B.DLC; i++) {
      new1db[i] = old_can_1db[i];
    }
    new1db[1] = (new1db[1] & 0xe0) | 2; // LB_Relay_Cut_Request
    new1db[3] = new1db[3] | 0x10; // LB_Full_CHARGE_flag
    new1db[6] = (new1db[6] + 1) % 4; // Increment LB_PRUN_1DB counter
    new1db[7] = leafcrc(7, new1db); //  CRC
    for (int i = 0; i < tx_frame.FIR.B.DLC; i++) {
      tx_frame.data.u8[i] = new1db[i];
      old_can_1db[i] = new1db[i];
    }
    send_can_frame(tx_frame);
    vTaskDelay(1);
  }
  should_send_stop_charge = 0;
}

/*
void send_can_charge_complete() {
  // Get back to normal
  CAN_frame_t tx_frame;
  unsigned char new1db[8];
  logger("Remove relay_cut request and full_charge flag");
  tx_frame.FIR.B.FF = CAN_frame_std;
  tx_frame.MsgID = 0x1db;
  tx_frame.FIR.B.DLC = 8;
  for (int j = 0; j < 8; j++) {  
    for (int i = 0; i < tx_frame.FIR.B.DLC; i++) {
      new1db[i] = old_can_1db[i];
    }
    new1db[1] = (new1db[1] & 0xe7); // Remove LB_Relay_Cut_Request
    new1db[3] = new1db[3] & 0xEF; // LB_Full_CHARGE_flag
    new1db[6] = (new1db[6] + 1) % 4; // Increment LB_PRUN_1DB counter
    new1db[7] = leafcrc(7, new1db); //  CRC
    for (int i = 0; i < tx_frame.FIR.B.DLC; i++) {
      tx_frame.data.u8[i] = new1db[i];
      old_can_1db[i] = new1db[i];
    }
    send_can_frame(tx_frame);
    vTaskDelay(1);
  }
}
*/


void init_ap() {
  if (wlan_connect == 1) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_AP);
  }
  if (!WiFi.softAP(ap_ssid, ap_password)) {
    Serial.println("Soft AP creation failed.");
    while (1);
  }
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  // Set WiFi to auto reconnect
  WiFi.setAutoReconnect(false);
}



void connect_wlan() {
  const uint8_t wifi_channel = 0; 
  logger("Connecting to wlan");
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connecting to wlan...");
    WiFi.begin(wlan_ssid, wlan_password, wifi_channel);
  } else {
    Serial.println("wlan already connected.");
  }
}

bool wifi_connected() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  return false;
}

String wifi_ip_address() {
  if (wifi_connected()) {
    IPAddress myIP = WiFi.localIP();
    Serial.print("AP IP address: ");
    Serial.println(myIP);
    return myIP.toString();
  }
  return "";
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

//  if (xQueueReceive(CAN_cfg.rx_queue, &rx_frame, 0) == pdTRUE) {
  if (xQueueReceive(CAN_cfg.rx_queue, &rx_frame, 3 * portTICK_PERIOD_MS) == pdTRUE) {
    if (rx_frame.FIR.B.RTR != CAN_RTR) {

      // only dump these frames...
/*
      if ( (rx_frame.MsgID == 0x1c2) || (rx_frame.MsgID == 0x59e) || (rx_frame.MsgID == 0x1d4) ||
        (rx_frame.MsgID == 0x1db) ||  (rx_frame.MsgID == 0x55b) ||  (rx_frame.MsgID == 0x5bc) ||
        (rx_frame.MsgID == 0x5bf) || (rx_frame.MsgID == 0x7bb) || (rx_frame.MsgID == 0x11a) ) {  
        logger_can(rx_frame,RX_FRAME);
      }
*/
      logger_can(rx_frame,RX_FRAME);

      if (rx_frame.MsgID == 0x1c2) {  
        battery_type = LEAF_ZE1;
      }
      if (rx_frame.MsgID == 0x59e) { 
        battery_type = LEAF_AZE0;
      }
      if (rx_frame.MsgID == 0x11a) { // Car status
        // 2 = car off, 1 = car on, 0 = no data
        int old_button = last_steering_wheel_button;
        last_steering_wheel_button = rx_frame.data.u8[2];
        if (last_steering_wheel_button != old_button) {
          if (LOG_STEERING_WHEEL_BUTTON_CHANGES) {
            logger("Buton change:" + String(last_steering_wheel_button));
          }
        }

        int old_status_car = status_car;
        status_car = (rx_frame.data.u8[1] & 0xc0) >> 6;
        // Emulate TCU when car turns on
        if ( ((old_status_car == 2) || (old_status_car == 0)) && (status_car == 1)) {
          timestamp_car_went_on = timestamp_now(); 
        }
        if ( (old_status_car == 1) && (status_car != 1)) {
          counter_retry_wlan = 3;
        }

        timestamp_last_11a_received = timestamp_now();        
      } 
      if (rx_frame.MsgID == 0x1d4) { // VCM sending
        timestamp_last_1d4_received = timestamp_now();        
      } 

      if (rx_frame.MsgID == 0x1db) { // HVBAT status
        hv_voltage = 0.5 * (rx_frame.data.u8[2] * 4 + ((rx_frame.data.u8[3] & 0xc0) >> 6));

        int hv_current_int = (rx_frame.data.u8[0] * 8 + ((rx_frame.data.u8[1] & 0xe0) >> 5));
        if (hv_current_int > 1023) {
          hv_current_int = - (2048 - hv_current_int);
        }
        hv_current = 0.5 * hv_current_int;
        //Serial.printf("I1:%d,%f,%f\n",hv_current_int,hv_current,hv_voltage);

        for (int i = 0; i < rx_frame.FIR.B.DLC; i++) {
          old_can_1db[i] = rx_frame.data.u8[i];
        }
        status_lb_failsafe = (rx_frame.data.u8[1] & 0x07);
        if (should_send_stop_charge == 1) {
          send_can_stop_charge();
        }
      }  

      if (rx_frame.MsgID == 0x54a) { // HVAC
        int TEMP = (rx_frame.data.u8[7]);
        if (TEMP != 0xff) {
          cc_inside_temp = (TEMP - 40);
        }
        TEMP = (rx_frame.data.u8[4]);
        if (TEMP != 0x00) {
          cc_set_temp = (0.5 * TEMP);
        }
      }

      if (rx_frame.MsgID == 0x54c) { // HVAC
        int TEMP = (rx_frame.data.u8[6]);
        if (TEMP != 0xff) {
          cc_outside_temp = (0.5 * TEMP - 40);
        }
      }

      if (rx_frame.MsgID == 0x54b) { // HVAC
        cc_status = rx_frame.data.u8[1];
        cc_fan_target = rx_frame.data.u8[2];
        cc_fan_intake = rx_frame.data.u8[3];
        Serial.printf("intake:%f\n",cc_fan_intake);
        cc_fanspeed = (rx_frame.data.u8[4] & 0x38) >> 3;
      }

      if (rx_frame.MsgID == 0x55b) { // HVBAT, soc etc
        int LB_TEMP = (rx_frame.data.u8[0] << 2 | rx_frame.data.u8[1] >> 6);
        if (LB_TEMP != 0x3ff) {  //3FF is unavailable value
          timestamp_last_soc_received = timestamp_now();
          status_soc = 0.1 * LB_TEMP;
//          Serial.printf("SoC:%f\n",status_soc);
        }
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
/*
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
        if (group == 1 && rx_frame.data.u8[0] == 0x25) { //Last frame
          // Leaf 24kWh
          if (battery_type == 0) {
            hq_status_soc = (( group_message[32 + 5] << 16) + (group_message[32 + 6] << 8) + (group_message[32 + 7])) / 10000;
          }
          if ((battery_type == 1) || (battery_type == 2)) {
            hq_status_soc = (( group_message[32 + 7] << 16) + (group_message[40 + 1] << 8) + (group_message[40 + 2])) / 10000;
          }
          hq_hv_voltage = (( group_message[24 + 1] << 8) | group_message[24 + 2]) / 100;
          long batt_i_2 = ( group_message[8 + 3] << 24) | ( group_message[8 + 4] << 16 | (( group_message[8 + 5] << 8) | group_message[8 + 6]));
          if (batt_i_2 & 0x8000000 == 0x8000000) {
            batt_i_2 = (batt_i_2 | -0x100000000 );
          }
          hq_hv_current = batt_i_2 / 1024;
          logger("High precision soc received...");
          Serial.printf("7bb I:%.2f\n",hq_hv_current);
          Serial.printf("7bb U:%.2f\n",hq_hv_voltage);
          Serial.printf("7bb SoC:%.2f\n",hq_status_soc);
        } else { // Request more messages 300100FFFFFFFFFF
          if (GET_HIGH_PRECISION_INFO_FROM_BATTERY) {
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
            send_can_frame(tx_frame);
          }
        }
      }
      */
    }
  }
}


void send_can_frame(CAN_frame_t frame) {
  ESP32Can.CANWriteFrame(&frame);
  logger_can(frame,TX_FRAME);
}

void can_tx_loop() {
  while (true) {
    CAN_frame_t tx_frame;
    tx_frame.FIR.B.FF = CAN_frame_std;
    int num_frames_to_send = 25;
    int interval_frame = 100;
    int command = command2send;

    if (command == WAKE_UP) { 
      num_frames_to_send = 1;
      tx_frame.MsgID = 0x679;
      tx_frame.FIR.B.DLC = 1;
      tx_frame.data.u8[0] = 0x00;
    }

    if (command == START_CHARGING) {
      logger("Sending can messages to start charging");
      tx_frame.MsgID = 0x56e;
      tx_frame.FIR.B.DLC = 1;
      tx_frame.data.u8[0] = 0x66;
    }

    if (command == IDLE_CAR_ON) {
      num_frames_to_send = 1;
      tx_frame.MsgID = 0x56e;
      tx_frame.FIR.B.DLC = 1;
      tx_frame.data.u8[0] = 0x46;
    }
    if (command == IDLE) {
      num_frames_to_send = 1;
      tx_frame.MsgID = 0x56e;
      tx_frame.FIR.B.DLC = 1;
      tx_frame.data.u8[0] = 0x86;
    }
    if (command == START_ACC) {
      tx_frame.MsgID = 0x56e;
      tx_frame.FIR.B.DLC = 1;
      tx_frame.data.u8[0] = 0x4e;
    }
    if (command == STOP_ACC) {
      tx_frame.MsgID = 0x56e;
      tx_frame.FIR.B.DLC = 1;
      tx_frame.data.u8[0] = 0x56;
    }

    if (command == STOP_CHARGING) { // 000200100000e3df
      logger("Will send can messages to stop charging when next 1db frame arrives");
      should_send_stop_charge = 1; // wait for LBC to send a 1db message, then immediately send x frames
      num_frames_to_send = 0;
    }

    if (command == STOP_CHARGING_NOW) { 
      // We use the frame that the LBC just sent, and modify a few bits (like ovms does).
      // But, instead of spamming. We send eight frames (1ms interval) right after a 1db frame
      // has been received. By sending eight, the next message from the LBC will have a correct
      // prune counter. The interval for the LBC for 1db frames is 10ms, so we will fit those eight
      // before next one is sent.
      unsigned char new1db[8];
      tx_frame.MsgID = 0x1db;
      tx_frame.FIR.B.DLC = 8;
      for (int i = 0; i < tx_frame.FIR.B.DLC; i++) {
        new1db[i] = old_can_1db[i];
      }
      new1db[1] = (new1db[1] & 0xe0) | 2; // LB_Relay_Cut_Request
      new1db[3] = new1db[3] | 0x10; // LB_Full_CHARGE_flag
      new1db[6] = (new1db[6] + 1) % 4; // Increment LB_PRUN_1DB counter
      new1db[7] = leafcrc(7, new1db); //  CRC
      for (int i = 0; i < tx_frame.FIR.B.DLC; i++) {
        tx_frame.data.u8[i] = new1db[i];
        old_can_1db[i] = new1db[i];
      }
      num_frames_to_send = 4;
      interval_frame = 1;
      logger("Sending stop_charge frames");
      should_send_stop_charge = 0;
    }

    if (command != NONE) {  
      int counter = 0;
      while (counter < num_frames_to_send) {
        send_can_frame(tx_frame);
        vTaskDelay(interval_frame);
        counter++;
      }
      command2send = NONE;
    }
    vTaskDelay(1);
  }
}


void send_can_command(int command) {
  command2send = command;
}


bool vcm_is_sleeping() {
  if ((seconds_since_can_data(timestamp_last_1d4_received) > 30) or (timestamp_last_soc_received == 0)) {
    return true;
  }
  return false;
}

int seconds_since_can_data(long last_timestamp) {
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

bool timer_is_enabled() {
  if (timer_enabled == 0) {
    return false;
  }
  return true;
}


bool timer_is_active() {
  if (timer_is_enabled() == 0) {
    return false;
  }
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
    if (((minutes >= minutes_start) && (minutes < 24*60)) || (minutes < minutes_stop)) {
      ret = true;
    }
  } else {
    if ((minutes >= minutes_start) && (minutes < minutes_stop)) {
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
String get_log() {
  return String(web_log_buffer);
}

void notFound(AsyncWebServerRequest* request) {
  request->send(404, "text/plain", "Not found");
}

void init_webserver() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", index_html, processor);
  });

  server.on("/div_overview", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", div_overview_html, processor);
  });

  server.on("/climate", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", climate_html, processor);
  });

  server.on("/div_climate", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", div_climate_html, processor);
  });

  server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest* request) {
    html_message = "";
    request->send_P(200, "text/html", wifi_html, processor);
  });

  server.on("/wifi_connect", HTTP_GET, [](AsyncWebServerRequest* request) {
    connect_wlan();
    html_message = "Trying to connect to wlan!";
    request->send_P(200, "text/html", wifi_html, processor);
  });

  server.on("/wifi_set", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (request->hasParam("wlan_connect")) {
      wlan_connect = string2int(request->getParam("wlan_connect")->value());
      String tmp_string = request->getParam("ap_password")->value();
      memcpy(ap_password,tmp_string.c_str(),64);
      tmp_string = request->getParam("wlan_password")->value();
      memcpy(wlan_password,tmp_string.c_str(),64);
      tmp_string = request->getParam("wlan_ssid")->value();
      memcpy(wlan_ssid,tmp_string.c_str(),64);
      html_message = "Wifi settings saved!";
      write_preferences();
      Serial.printf("wlan_ssid:%s,%s\n",wlan_ssid,wlan_password);
      logger("Wifi settings saved");
    } else {
      html_message = "No params found in request";
    }
    request->send_P(200, "text/html", message_html, processor);
  });


  server.on("/timer", HTTP_GET, [](AsyncWebServerRequest* request) {
    html_message = "";
    request->send_P(200, "text/html", timer_html, processor);
  });

  server.on("/clock", HTTP_GET, [](AsyncWebServerRequest* request) {
    html_message = "";
    request->send_P(200, "text/html", clock_html, processor);
  });

  server.on("/control", HTTP_GET, [](AsyncWebServerRequest* request) {
    html_message = "";
    request->send_P(200, "text/html", control_html, processor);
  });

  server.on("/start_charging", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!timer_is_enabled()) {
      send_can_command(START_CHARGING);
      html_message = "Start charging can messages sent!";
    } else {
      html_message = "Refuse to start when timer is enabled!";
    }
    request->send_P(200, "text/html", control_html, processor);
  });

  server.on("/stop_charging", HTTP_GET, [](AsyncWebServerRequest* request) {
    send_can_command(STOP_CHARGING);
    html_message = "Stop charging can messages sent!";
    request->send_P(200, "text/html", control_html, processor);
  });

  server.on("/start_acc", HTTP_GET, [](AsyncWebServerRequest* request) {
    send_can_command(START_ACC);
    html_message = "Start ACC can messages sent!";
    request->send_P(200, "text/html", control_html, processor);
  });

  server.on("/stop_acc", HTTP_GET, [](AsyncWebServerRequest* request) {
    send_can_command(STOP_ACC);
    html_message = "Stop ACC can messages sent!";
    request->send_P(200, "text/html", control_html, processor);
  });

  server.on("/wake_up", HTTP_GET, [](AsyncWebServerRequest* request) {
    bool changed = set_wake_up();
    if (changed) {
      html_message = "Tried to wake up VCM in car!";
    } else {
      html_message = "No need to wake up VCM in car, bit already set!";
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
      html_message = "Timer set!";
      write_preferences();
      logger("Timer set");
    } else {
      html_message = "No params found in request";
    }
    request->send_P(200, "text/html", message_html, processor);
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

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest* request) {
    html_message = "Will reset in 5 seconds!";
    request->send_P(200, "text/html", message_html, processor);
    logger("Resetting");
    vTaskDelay(5000);
    shutdown_tasks();
    ESP.restart();
  });

  server.on("/log", HTTP_GET, [](AsyncWebServerRequest* request) {
    candump = -1;
    if (request->hasParam("candump")) {
      candump = string2long(request->getParam("candump")->value());
      if (candump == 1) {
        resume_can_writing();
        logger("start candump");
        counter_can = 0;
      } 
      if (candump == 0) {
        pause_can_writing();
        logger("stop candump, got:" + String(counter_can) + " frames");
      }
      if (candump == 2) {
        delete_can_log();
        logger("delete candump file");
      }
      if (candump == 3) {
        request->send(SD, CAN_LOG_FILE, String(), true);
        logger("Downloading file");
      }
    }
    if (candump != 3) {
      vTaskDelay(100);
      request->send_P(200, "text/html", log_html, processor);
    }
  });

  server.onNotFound(notFound);

  server.begin();
}

bool cc_is_on() {
  if ((cc_status == 0x76) || (cc_status == 0x78)) {
    return true;
  }
  return false;
}

String get_string_for_cc_status(int cc_status) {
  String s = "";
  switch (cc_status) {
  case 0x08:
    s = "off";
    break;
  case 0x76:
    s = "auto";
    break;
  case 0x78:
    s = "manual";
    break;
  default:
    s = "unknown";
    break;
  }
  return s;
}


String get_string_for_cc_fan_target(int cc_fan_target) {
  String s = "";

  switch (cc_fan_target)  {
  case 0x80:
    s = "off";
    break;
  case 0x88:
    s = "face";
    break;
  case 0x90:
    s = "face & feet";
    break;
  case 0x98:
    s = "feet";
    break;
  case 0xA0:
    s = "windshield & feet";
    break;
  case 0xA8:
    s = "windshield";
    break;
  default:
    s = "unknown";
    break;
  }
  return s;
}


String get_string_for_cc_fan_intake(int cc_fan_intake) {
  String s = "";

  switch (cc_fan_intake)  {
  case 0x09:
    s = "recirculate";
    break;
  case 0x12:
    s = "fresh air";
    break;
  case 0x92:
    s = "defrost";
    break;
  default:
    s = "unknown";
    break;
  }
  return s;
}


String processor(const String& var)
{
  if(var == "LINKS") {
    String content = "<a href='/' class='button'>Overview</a>&nbsp;<a href='/timer' class='button'>Set timer</a>&nbsp;<a href='/clock' class='button'>Set clock</a>&nbsp;<a href='/climate' class='button'>Climate info</a>&nbsp;<a href='/control' class='button'>Manual Control</a>&nbsp;<a href='/wifi' class='button'>Wifi</a>&nbsp;<a href='/log' class='button'>Log</a><br><hr>";
    return content;
  }

  if(var == "DIV_STATUS_STYLE") {
    String content = "";
//    if (timestamp_last_soc_received <= 0) {
//      content = "style='display: none;'";
//    }
    return content;
  } 

  if(var == "CLIMATE") {
    String content = "";
    content += "Temp outside: " + String(cc_outside_temp) + "<br><br>";
    content += "Temp inside: " + String(cc_inside_temp) + "<br><br>";
    String str_cc_status = "";
    str_cc_status = get_string_for_cc_status(cc_status);
    content += "CC status: " + str_cc_status + "<br><br>";
    content += "CC temp set: " + String(cc_set_temp) + "<br><br>";
    content += "Fan speed: " + String(cc_fanspeed) + "<br><br>";
    String str_cc_fan_target = "";
    str_cc_fan_target = get_string_for_cc_fan_target(cc_fan_target);
    content += "Fan target: " + str_cc_fan_target + "<br><br>";
    String str_cc_fan_intake = "";
    str_cc_fan_intake = get_string_for_cc_fan_intake(cc_fan_intake);
    content += "Fan intake: " + str_cc_fan_intake + "<br><br>";

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
    float soc = status_soc;
    if (hq_status_soc > 0) {
      soc = hq_status_soc;
    }
    content += "SoC: " + String(soc) + "&#37;<br>";
    return content;
  } 
  if(var == "SOH") {
    String content = "";
    content += "SoH: " + String(status_soh) + "&#37;<br>";
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

  if(var == "CAR_STATUS") {
    String content = "";
    String status = "";
    if (status_car == 2) { status = "off"; }
    if (status_car == 1) { status = "on"; }
    if (status_car == 0) { status = "idle"; }
    if (status_car > 2) { status = String(status_car); }
    
    content += "Car is: " + String(status) + "<br>";
    return content;
  } 

  if(var == "CAN_DATA") {
    String content = "";
    String font_p = "<p style='color:MediumSeaGreen;'>";
    if (vcm_is_sleeping()) {
      font_p = "<p style='color:red;'>";
    }
    if (timestamp_last_1d4_received <= 0) {
      content += String(font_p) + "No can messages from VCM seen yet</p>";
    } else {
      content += String(font_p) + "Last can message from VCM seen: " + String(seconds_since_can_data(timestamp_last_1d4_received)) + "s ago</p>";
    }
    return content;
  } 
  if(var == "TIMER") {
    String content = "";
    if (timer_is_enabled()) {
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

  if(var == "CHARGE_TIME") {
    String content = "";
    String status = "";
    float capacity_left_to_soc = 0.01 * BATTERY_CAPACITY_NEW * status_soh * (timer_soc - status_soc);
    float c = std::trunc(capacity_left_to_soc / 1000) / 100.0;
    float time_left_hours = c/2;
    float time_left = std::trunc(time_left_hours * 100) / 100.0;
    content += "Charge to wanted SoC: " + String(c) + "kWh, " + String(time_left) + "h @2kW<br>";
    return content;
  } 

  if(var == "HV_POWER") {
    String content = "";
    String status = "";
    String precision = "~";
    if (status_car == 2) { status = "off"; }
    if (status_car == 1) { status = "on"; }
    if (status_car == 0) { status = "idle"; }
    float power = hv_voltage * hv_current;
    //Serial.printf("I2:%f,%f,%f\n",hv_current,hv_voltage,power);

    if (hq_hv_voltage > 0) {
      precision = "";
      power = hq_hv_voltage * hq_hv_current;
    }
    power = std::trunc(power / 10) / 100.0;
    content += "HV Power: " + precision + String(power) + "kW<br>";
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
  if(var == "WAKE_UP") {
    String content = "";
    content += "Wake up bit: " + String(state_wake_up_pin) + "<br>";
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
  if(var == "TIMER_ENABLED_0") {
    String content = "";
    if (timer_enabled == 0) { content = "selected"; }
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
  if(var == "WLAN_CONNECT_0") {
    String content = "";
    if (wlan_connect == 0) { content = "selected"; }
    return content;
  } 
  if(var == "WLAN_CONNECT_1") {
    String content = "";
    if (wlan_connect == 1) { content = "selected"; }
    return content;
  } 
  if(var == "AP_PASSWORD") {
    return String(ap_password);
  }

  if(var == "WLAN_STATUS") {
    if (wifi_connected()) {
      return String("connected, " + wifi_ip_address());
    }
    return String("not connected");
  }

  if(var == "WLAN_SSID") {
    return String(wlan_ssid);
  }
  if(var == "WLAN_PASSWORD") {
    return String(wlan_password);
  }

  if(var == "SD_CARD") {
    String content = "";
    String content2 = "";
    if (get_sdcard_status()) {
      content = "SD Card is ok";      
    } else {
      content = "SD Card is not ok";      
    }
    if (get_candump_file_status()) {
      long fs = get_file_size();
      content2 = ", file is there, " + String(fs) + " bytes";      
    } else {
      content2 = ", no file";      
    }


    return content + content2;
  } 

  if(var == "CANDUMP") {
    String content = "";
    if (get_candump_status()) {
      content = "Candump is active";      
    } else {
      content = "Candump is not active";      
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

