#include "esp_system.h"
#include <Arduino.h>
#include <ESP32CAN.h>
#include <CAN_config.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include <ctime>

#include "index_html.cpp"

AsyncWebServer server(80);

const char *ssid = "lily";
const char *password = "0123456789";

CAN_device_t CAN_cfg;               // CAN Config
unsigned long previousMillis = 0;   // will store last time a CAN Message was send
const int interval_can = 100;          // interval_can at which send CAN Messages (milliseconds)
const int interval_bat = 50;          // n * interval_can at which send CAN Messages to battery for detailed info
const int rx_queue_size = 10;       // Receive Queue size

#define CAN_SE_PIN 23
#define LED_BUILTIN 2 

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Configuring access point...");
  if (!WiFi.softAP(ssid, password)) {
    Serial.println("Soft AP creation failed.");
    while (1);
  }
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  pinMode(CAN_SE_PIN, OUTPUT);
  digitalWrite(CAN_SE_PIN, LOW);
  CAN_cfg.speed = CAN_SPEED_500KBPS;
  CAN_cfg.tx_pin_id = GPIO_NUM_27;
  CAN_cfg.rx_pin_id = GPIO_NUM_26;
  CAN_cfg.rx_queue = xQueueCreate(rx_queue_size, sizeof(CAN_frame_t));
  ESP32Can.CANInit();
  Serial.println("Starting Leaf timer");
  init_webserver();
}


char status_plugged_in = 0;
char status_charging = 0;
char status_car_off = 0;
char status_lb_failsafe = 0;
u_char group_message[255];
char group = 0;
int pointer_group_message = 0;
float soc = 0;
float soh = 0;
int counter_bat = 0;
unsigned long last_group_request_millis = 0;

void loop() {

  CAN_frame_t rx_frame;  


  unsigned long currentMillis = millis();

  // Receive next CAN frame from queue
  if (xQueueReceive(CAN_cfg.rx_queue, &rx_frame, 3 * portTICK_PERIOD_MS) == pdTRUE) {

    if (rx_frame.FIR.B.RTR != CAN_RTR) {

      if (rx_frame.MsgID == 0x11a) { // Car status
        status_car_off = (rx_frame.data.u8[1] & 0xc0) >> 7;
        Serial.printf("Car message: on:%d, charging:%d\n",status_car_off);       
      }

      if (rx_frame.MsgID == 0x1db) { // HVBAT status
        status_lb_failsafe = (rx_frame.data.u8[1] & 0x07);
        Serial.printf("LB_Failsafe (0=OK):%d\n",status_lb_failsafe);     
      }  

      if (rx_frame.MsgID == 0x5bc) { // HVBAT
          soh = rx_frame.data.u8[4] >> 1;
          Serial.printf("SoH:%.2f\n",soh);
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
        Serial.printf("OBC message: plugged in:%d, charging:%d\n",status_plugged_in,status_charging);       
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
          soc = (( group_message[32 + 5] << 16) + (group_message[32 + 6] << 8) + (group_message[32 + 7])) / 10000;
          Serial.printf("SoC:%.2f\n",soc);
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


/*      Serial.printf(" from 0x%08X, DLC %d, Data ", rx_frame.MsgID,  rx_frame.FIR.B.DLC);
      for (int i = 0; i < rx_frame.FIR.B.DLC; i++) {
        Serial.printf("0x%02X ", rx_frame.data.u8[i]);
      }
      Serial.printf("\n");
      */
    }
  }
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

void init_webserver() {

  server.on("/logout", HTTP_GET, [](AsyncWebServerRequest* request) { request->send(401); });

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", index_html, processor);
  });

  // Start server
  server.begin();
}

String processor(const String& var)
{
  Serial.printf("Var:%s\n",var);
  if(var == "X") {
    String content = "";
    content += "SoC: " + String(soc) + "&#37;<br>";
    content += "SoH: " + String(soh) + "&#37;<br>";
    return content;
  } 
  return String();
}

