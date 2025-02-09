#include "sdcard.h"
//#include "freertos/ringbuf.h"

#if defined(SD_CS_PIN) && defined(SD_SCLK_PIN) && defined(SD_MOSI_PIN) && \
    defined(SD_MISO_PIN)  // ensure code is only compiled if all SD card pins are defined

File can_log_file;

bool can_logging_paused = true;
bool can_file_open = false;
bool delete_can_file = false;
bool sd_card_active = false;

void delete_can_log() {
  SD.remove(CAN_LOG_FILE);
  delete_can_file = false;
}

void resume_can_writing() {
  if (SD.cardType() == CARD_NONE) {
    init_sdcard();
    return;
  }

  can_logging_paused = false;
  if (can_file_open == false) {
    can_log_file = SD.open(CAN_LOG_FILE, FILE_APPEND);
    can_file_open = true;
  }
}

void pause_can_writing() {
  can_logging_paused = true;
  if (can_file_open) {
    can_log_file.close();
    can_file_open = false;
  }
}

long get_file_size() {
  long f_size = 0;

  if (SD.exists(CAN_LOG_FILE)) {
    if (can_file_open == false) {
      can_log_file = SD.open(CAN_LOG_FILE, FILE_APPEND);
      f_size = can_log_file.size();
      can_log_file.close();
    } else {
      f_size = can_log_file.size();
    }
  }
  return f_size;
}

bool get_sdcard_status() {
  if (SD.cardType() == CARD_NONE) {
    return false;
  }
  return true;
}

bool get_candump_file_status() {
  if (SD.exists(CAN_LOG_FILE)) {
    return true;
  }
  return false;
}


bool get_candump_status() {
  if (can_logging_paused) {
    return false;
  }
  return true;
}

void log2sd(String s,int l) {
//  Serial.printf("Old P2:%d - %d,%d\n",l,s[l-1],s[l]);
//  Serial.println(s);
  int ret = can_log_file.print(s);
  if (ret < l) {
    Serial.println(ret);
  }
  //Serial.printf("Writing %d bytes\n",l);
}


void init_sdcard() {

  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  pinMode(SD_SCLK_PIN, OUTPUT);
  pinMode(SD_MOSI_PIN, OUTPUT);
  pinMode(SD_MISO_PIN, INPUT);

  SPI.begin(SD_SCLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD Card initialization failed!");
    return;
  }

  Serial.println("SD Card initialization successful.");
  sd_card_active = true;

  print_sdcard_details();
}

void print_sdcard_details() {

  Serial.print("SD Card Type: ");
  switch (SD.cardType()) {
    case CARD_MMC:
      Serial.println("MMC");
      break;
    case CARD_SD:
      Serial.println("SD");
      break;
    case CARD_SDHC:
      Serial.println("SDHC");
      break;
    case CARD_UNKNOWN:
      Serial.println("UNKNOWN");
      break;
    case CARD_NONE:
      Serial.println("No SD Card found");
      break;
  }

  if (SD.cardType() != CARD_NONE) {
    Serial.print("SD Card Size: ");
    Serial.print(SD.cardSize() / 1024 / 1024);
    Serial.println(" MB");

    Serial.print("Total space: ");
    Serial.print(SD.totalBytes() / 1024 / 1024);
    Serial.println(" MB");

    Serial.print("Used space: ");
    Serial.print(SD.usedBytes() / 1024 / 1024);
    Serial.println(" MB");
  }
}
#endif  // defined(SD_CS_PIN) && defined(SD_SCLK_PIN) && defined(SD_MOSI_PIN) && defined(SD_MISO_PIN)
