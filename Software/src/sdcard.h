#ifndef SDCARD_H
#define SDCARD_H

#include <SD.h>
#include <SPI.h>
#include "../SETTINGS.h"
#include <ESP32CAN.h>

//#include "../../communication/can/comm_can.h"
//#include "../hal/hal.h"
/* CAN Frame structure */
typedef struct {
  bool FD;
  bool ext_ID;
  uint8_t DLC;
  uint32_t ID;
  union {
    uint8_t u8[64];
    uint32_t u32[2];
    uint64_t u64;
  } data;
} CAN_frame;

enum FRAME_DIRECTION { RX_FRANE, TX_FRAME }; 

enum frameDirection { MSG_RX, MSG_TX };  //RX = 0, TX = 1

typedef struct {
  CAN_frame frame;
  frameDirection direction;
} CAN_log_frame;

#if defined(SD_CS_PIN) && defined(SD_SCLK_PIN) && defined(SD_MOSI_PIN) && \
    defined(SD_MISO_PIN)  // ensure code is only compiled if all SD card pins are defined
#define CAN_LOG_FILE "/canlog.txt"

bool get_sdcard_status();
bool get_candump_status();
void log2sd(CAN_frame_t frame, FRAME_DIRECTION fdir); 
void init_logging_buffer();

void init_sdcard();
void print_sdcard_details();

void add_can_frame_to_buffer(CAN_frame frame, frameDirection msgDir);
void write_can_frame_to_sdcard();

void pause_can_writing();
void resume_can_writing();
void delete_can_log();
#endif  // defined(SD_CS_PIN) && defined(SD_SCLK_PIN) && defined(SD_MOSI_PIN) && defined(SD_MISO_PIN)

#endif  // SDCARD_H
