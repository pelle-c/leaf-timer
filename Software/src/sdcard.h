#ifndef SDCARD_H
#define SDCARD_H

#include <SD.h>
#include <SPI.h>
#include "../SETTINGS.h"
#include <ESP32CAN.h>

#if defined(SD_CS_PIN) && defined(SD_SCLK_PIN) && defined(SD_MOSI_PIN) && \
    defined(SD_MISO_PIN)  // ensure code is only compiled if all SD card pins are defined
#define CAN_LOG_FILE "/canlog.txt"

long get_file_size();
bool get_candump_file_status();
bool get_sdcard_status();
bool get_candump_status();
void log2sd(String s,int l); 
void init_logging_buffer();

void init_sdcard();
void print_sdcard_details();

void pause_can_writing();
void resume_can_writing();
void delete_can_log();
#endif  // defined(SD_CS_PIN) && defined(SD_SCLK_PIN) && defined(SD_MOSI_PIN) && defined(SD_MISO_PIN)

#endif  // SDCARD_H
