#include "sdcard.h"

#if defined(SD_CS_PIN) && defined(SD_SCLK_PIN) && defined(SD_MOSI_PIN) && \
    defined(SD_MISO_PIN)  

File candump_file;

bool candump_is_stopped = true;
bool candump_file_is_open = false;

void delete_candump_file() {
  SD.remove(CADUMP_FILE);
}

void start_candump() {
  if (SD.cardType() == CARD_NONE) {
    init_sdcard();
    return;
  }

  candump_is_stopped = false;
  if (candump_file_is_open == false) {
    candump_file = SD.open(CADUMP_FILE, FILE_APPEND);
    candump_file_is_open = true;
  }
}

void stop_candump() {
  candump_is_stopped = true;
  if (candump_file_is_open) {
    candump_file.close();
    candump_file_is_open = false;
  }
}

long get_file_size() {
  long f_size = 0;

  if (SD.exists(CADUMP_FILE)) {
    if (candump_file_is_open == false) {
      candump_file = SD.open(CADUMP_FILE, FILE_APPEND);
      f_size = candump_file.size();
      candump_file.close();
    } else {
      f_size = candump_file.size();
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
  if (SD.exists(CADUMP_FILE)) {
    return true;
  }
  return false;
}


bool get_candump_status() {
  if (candump_is_stopped) {
    return false;
  }
  return true;
}

void log2sd(String s,int l) {
//  Serial.printf("Old P2:%d - %d,%d\n",l,s[l-1],s[l]);
//  Serial.println(s);
  int ret = candump_file.print(s);
  //if (ret < l) {
  //  Serial.println(ret);
 // }
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

  print_sdcard_details_on_serial();
}

void print_sdcard_details_on_serial() {

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
#endif  
