//Password to WLAN access point - default values, if not saved in flash
#define AP_SSID_DEFAULT "caroftheyear2011"
#define AP_PASSWORD_DEFAULT "1234123412341234"

//Name and password of Wifi network you want the emulator to connect to
#define WIFI_SSID "MY_SSID"          
#define WIFI_PASSWORD "MY_WLAN_PASSWORD" 

// Send queries for extra information, may collide with LeafSpy.
// Not required. Will give better readings for power.
#define GET_HIGH_PRECISION_INFO_FROM_BATTERY 0

// SD card
#define SD_MISO_PIN 2
#define SD_MOSI_PIN 15
#define SD_SCLK_PIN 14
#define SD_CS_PIN 13

// If ADC is used
#define I2C_SDA 18
#define I2C_SCL 5


#define CAN_SE_PIN 23
#define LED_PIN 4


#define WAKE_UP_PIN 32
#define REVERSE_PIN 18
#define HEADUNIT_TX_PIN 5
#define HEADUNIT_RX_PIN 25

#define FAKE_TCU 0
#define BATTERY_CAPACITY_NEW 40000

//#define LOG_STEERING_WHEEL_BUTTON_CHANGES 1
