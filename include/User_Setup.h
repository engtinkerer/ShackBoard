#pragma once

#define USER_SETUP_LOADED

#define ILI9341_DRIVER
#define TFT_RGB_ORDER TFT_RGB

#ifndef TFT_WIDTH
  #define TFT_WIDTH  320
#endif
#ifndef TFT_HEIGHT
  #define TFT_HEIGHT 240
#endif

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1

#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH

#define TOUCH_CS 33

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6

#define SPI_FREQUENCY 40000000