///////////////////////////////////////////////////////////////////////////////
//
// cristinel.ababei
// dec.31.2025
//
// Description:
// This is a weather device built with an ESP32 board and an e-paper display.
// Weather data is pulled from OpenWeatherMap. Note it is not the forecast, but the current data.
// One could get also the forecast.
// Time and date is also retrieved and displayed.
// Device needs access to a WiFi router.
//
// Hardware needed:
// (1) ESP32-C3 Supermini Development Board
//     https://www.amazon.com/Waveshare-Development-ESP32-C3FN4-Single-Core-Processor/dp/B0CR2XP59Y/
// (2) 2.9" Black-White E-Paper Display Module -- 296x128 pixels
//     https://www.amazon.com/2-9inch-Module-Resolution-Display-Electronic/dp/B07P6MJPTD
// (3) TP4056 Charging Module with booster
//     https://www.amazon.com/Battery-Charger-Discharge-Integrated-Lithium/dp/B098989NRZ
// (4) MakerHawk 3.7V Lipo Battery 1000mAh
//     https://www.amazon.com/MakerHawk-Rechargeable-Integrated-Protection-Electronic/dp/B0DPZVBKMY
//
// Software libraries needed:
// --esp32: installed via Boards Manager
// --ArduinoJson: in Library Manager search "ArduinoJson" and install the one by Benoit Blanchon
// --GxEPD2: in Library Manager search "GxEPD2" and install the one by Jean-Mark Zingg
//
// NOTES:
// --In Arduino IDE select as Board the "ESP32C3 Dev Module" board.
// --In Arduino IDE, enable "Tools->USB CDC On Boot" because this little ESP32 board 
//   lacks a dedicated USB-to-UART chip. 
// --Make sure that you first, separately set the output voltage of the "Charging Module with booster"
//   to 5V by tuning its potentiometer with an isolated screwdriver (no metal handle for the screw driver);
//   and having the rechargeable battery connected to its input. Only after that connect the ESP32 board 
//   to the power module! Do this with the ESP32 board disconnected!
// --If your ESP32-C3-Zero disconnects during programming, it can be due to 
//   an improper boot mode sequence; try holding BOOT, pressing/releasing RESET, 
//   then releasing BOOT before uploading.
// --Connect 3.3V of ESPE32-C3 to display and not 5V; even though Waveshare display supports 5V too;
//   I had some issues with the display update with messages of type "busy timeout";
//   those issues disappeared when I used 3.3V;  
//
// CREDITS:
// --Initial project that served as source of inspiration:
// https://www.instructables.com/WeatherPaper-Real-Time-Weather-on-E-Paper-Display/
//
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//
// NOTE: R1,R2,R3, LED1 are optional because battery has integrated protection;
// HW Connections:
// 
//  ----------------------------------
// |                                  |
// |    2.9" E-Paper Display          |
// |                                  |
// | BUSY RES DC CS SCL SDA GND VCC   |
//  ----------------------------------
//    |    |   |  |  |   |   |   |
//    |    |   |  |  |   |   |   | 
//  ----------------------------------
// |  3    8   1  7  4   6   GND 3.3V |<----Connect to 3.3V and not 5V; even though the Waveshare claims to work with 5V as well.
// |                                  |
// |    Waveshare ESP32S-C3-Zero      |<----USB1
// |          0             10        |
//  ---------------------------------- 
//            |              |
//  GND---R2-----R1---|      |---LED1---R3---GND
//                    |
//                    |                    VCC   GND
//                    |                     |     |
//  ----------------  |  SW1    -----------------------
// |               +|----/ ----|BAT+       VOUT+ VOUT- |
// | Battery 3.7V   |          |     TP4056 Booster    |<----USB2
// |               -|----------|BAT-                   |
//  ----------------            -----------------------
//
///////////////////////////////////////////////////////////////////////////////

#include <Adafruit_NeoPixel.h> // for turning Off RGB LED of ESP32 board;
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <string.h>
#include "time.h"
#include "icons.h"

#define ENABLE_GxEPD2_GFX 0
#define uS_TO_S_FACTOR 1000000 // Conversion factor for micro seconds to seconds
#define TIME_TO_SLEEP  1800    // Time ESP32 will sleep, in seconds 1800s = 30min

// ---Pin connections to Waveshare e-paper display;
#define EPD_BUSY    3
#define EPD_RST     8 // avoid using GPIO2 for EPD_RST, because ESP32-C3 GPIO2 is a strapping pin;
#define EPD_DC      1
#define EPD_CS      7
#define EPD_CLK     4
#define EPD_DIN     6

// ---WiFi
// to connect to WiFi and openweathermap.org
char *ssid     = "[Your-wifi-router-name-here]"; // SSID of local network
char *password = "[Your-wifi-password-here]"; // Password on network

int wifi_timeout = 0;
bool connection_to_wifi_ok = false;

// ---OpenWeather
// to get an API key on OpenWeather, you must first create a free account 
// and verify your email address. The API key (also known as APPID) is 
// generated automatically upon registration and can be found in your 
// account settings. 
const String city = "Oshkosh"; // change this to your city
const String apiKey = "[YOUR-KEY-HERE]";

// we will read weather data via URL, which includes city name and apyKey;
// change "imperial" to "metric" if you want temp in Celcius
// http://api.openweathermap.org/data/2.5/weather?q=Oshkosh&appid=[YOUR-KEY-HERE]&units=imperial
const String weatherURL = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "&appid=" + apiKey + "&units=imperial";

// we will display temp and humidity in addition to the icon that shows the weather forecast;
String temperature = "";
String humidity = "";

RTC_DATA_ATTR float savedTemp = 0;
RTC_DATA_ATTR int savedHumid = 0;

// ---Date and time
char date_and_time[20] = "2026/02/28-18:58:13";

// ---Display
// 2.9inch e-Paper Display Module from Waveshare; 296x128 Resolution 3.3V/5V
GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display( 
  GxEPD2_290_BS(
    EPD_CS,   // CS=7
    EPD_DC,   // DC=1
    EPD_RST,  // RES=2
    EPD_BUSY) // BUSY=3
  );

RTC_DATA_ATTR const unsigned char *icon = nullptr;

// --- Battery level and LED; 
// I use pin "10" to drive LED when battery too low;
// also pin "0" is used to sense battery level;
const int ledPin = 10; // pin 10 drives red LED; but, it is also the one that drives the RGB LED of ESP board!
const int batPin = 0; // pin 0 from voltage divider on battery;
int battery_voltage = 0;
int low_battery = 0;

// --- RGB LED of ESP32 board;
// according to documentation of ESP32 board:
// https://www.waveshare.com/wiki/ESP32-C3-Zero#Document
// RGB LED is driven by Pin 10, which I happen to also 
// use to drive my own LED of the weatehr station to indicate
// low battery; I need to make sure the RGB LED is turned Off
// so that it does not consume power; an alternative to this 
// would be to just desolder the RGB LED from ESP32 board;
// I do not need it anyways;
// to turn Off RGB LED we use Adafruit_NeoPixel library;
#define NUMPIXELS  1
Adafruit_NeoPixel pixels_rgb(NUMPIXELS, ledPin, NEO_GRB + NEO_KHZ800); // ledPin is PIN 10


///////////////////////////////////////////////////////////////////////////////
//
// setup
//
///////////////////////////////////////////////////////////////////////////////

void setup() 
{
  delay(500);
  // turn Off RGB LED;
  pixels_rgb.begin();
  pixels_rgb.clear();
  pixels_rgb.show();

  // (1) initialize display;
  SPI.end();
  SPI.begin(EPD_CLK, -1, EPD_DIN, EPD_CS); 
  display.init(9600, true, 50, false); // 115200, true, 50, false
  display.setRotation(3);

  // (2) init serial to local PC; will print debug/status info 
  // to serial terminal;
  Serial.begin(9600);
  Serial.println("Initialising...");

  // (3) check battery level;
  pinMode(ledPin, OUTPUT); // LED pin 10;
  digitalWrite(ledPin, HIGH);
  delay(500);
  digitalWrite(ledPin, LOW);
  // next calculations are valid only when powered from the 3.7V rechargeable battery;
  low_battery = 0;
  battery_voltage = analogRead(batPin); 
  Serial.print("Battery voltage: ");
  Serial.println(battery_voltage);
  // if battery_voltage is below half of the battery 3.7, then, turn On LED;
  // 5V ---> 4095 (for 12 bit resolution)
  // (3.7V/2 = 1.85) / 2 = 0.925V ---> 758
  if (battery_voltage < 758) {
    digitalWrite(ledPin, HIGH);
    low_battery = 1;
    Serial.println("!!! Low battery level. Recharge it now !!!");
  }

  // (4) attempt to connect to WiFi network;
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  // try to connect up to 8 times; if unsuccessful, tell user that display
  // will not be updated;
  while ( WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
    wifi_timeout++;
    if (wifi_timeout > 8) {
      WiFi.disconnect();
      Serial.println("WiFi not connected! Display will not be updated!");
      break;
    }
  }

  // (5) if connected to wifi, then, pull weather data from the Internet from openweathermap.org
  if ( WiFi.status() == WL_CONNECTED) {
    connection_to_wifi_ok = true;
    Serial.println("WiFi connected successfully! Checking weather now.");
    
    // (a) -------------------------------------------- retrieve weather data
    get_weather();
      
    // (b) -------------------------------------------- retrieve time and date data
    init_and_set_time_zone("CST6CDT,M3.2.0,M11.1.0");
    get_date_and_time();
  } 
  // if connection to WiFi was not successful, then, display previously saved data, if any;
  else { 
    if (savedTemp == 0 && savedHumid == 0) { 
      // first time we have nothing saved;
      temperature = "N/A";
      humidity = "N/A";
      icon = epd_bitmap_clear_sky;
      Serial.println("No saved data. Display will not update.");
    } else { 
      // after a previous successful connection, we have saved temp and hum values;
      // so, just re-print those old values;
      String temperaturerecovery = String(savedTemp, 1) + "`F"; // use '`' because '°' is not supported in Adafruit_GFX library; 
      String humidityrecovery = String(savedHumid) + "%";
      temperature = temperaturerecovery;
      humidity = humidityrecovery;
      Serial.println("Data recovered from last refresh.");
    }
  }
  // NOTE: 
  // at this time, we should have temperature, humidity, and icon updated with new "values";
  // also, time and date data;

  // (6) update e-paper display;
  display.clearScreen();
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setTextSize(1);

  if (connection_to_wifi_ok == false) {
    display.drawBitmap(4, 3, epd_bitmap_warning, 16, 16, 0);
  }
  // personalized title on top of display;
  display.setFont(&FreeSans9pt7b);
  display.setCursor(48, 13);
  if (low_battery == 1) {
    display.println("Low battery. Recharge now!"); // if battery low (and esp w/o built-in protection);
  } else {
    display.print("Susan's Weather Station"); // default title;
  }
  display.setCursor(24, 125);
  // print at the bottom when we last time updated successfully;
  display.print("Updated: ");
  display.setCursor(104, 125);
  display.print(date_and_time);
  // draw a vertical line, separtes main weather icon on left from;
  // draw 2 horizontal lines too;
  // temp/hum icons + values on right are on the right side;
  display.drawLine(0, 20, 295, 20, 0); // horiz line 1;
  display.drawLine(0, 107, 295, 107, 0); // horiz line 2;
  display.drawLine(104, 21, 104, 106, 0); // vertical line;
  display.drawBitmap(124, 23, epd_bitmap_temperature, 32, 32, 0);
  display.drawBitmap(123, 73, epd_bitmap_humidity, 32, 32, 0);
  // update temp and hum values with a larger font size;
  display.setFont(&FreeSans18pt7b);
  display.setCursor(169, 51);
  display.print(temperature); // update temp;
  display.setCursor(169, 102);
  display.print(humidity); // update hum;
  // updated weather icon;
  display.drawBitmap(18, 32, icon, 64, 64, 0); 
  
  // update e-paper display with new information;
  display.display();

  // (7) go to sleep; will wake up in 30 minutes;
  Serial.println("E-Paper updated! Going to deep sleep mode now.");
  display.powerOff();
  //display.hibernate();  
  delay(500);
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

///////////////////////////////////////////////////////////////////////////////
//
// loop
//
///////////////////////////////////////////////////////////////////////////////

void loop() 
{
  // nothing done here;
  // things will be refreshed on startup only; 
}

///////////////////////////////////////////////////////////////////////////////
//
// utilities: weather, date and time
//
///////////////////////////////////////////////////////////////////////////////

void get_weather()
{
  HTTPClient http;
  http.begin(weatherURL);
  int httpCode = http.GET();
  // example:
  // {"coord":{"lon":-88.5426,"lat":44.0247},"weather":[{"id":804,"main":"Clouds","description":"overcast clouds","icon":"04n"}],"base":"stations","main":{"temp":-11.16,"feels_like":-16.34,"temp_min":-12.08,"temp_max":-10.56,"pressure":1014,"humidity":79,"sea_level":1014,"grnd_level":984},"visibility":10000,"wind":{"speed":2.57,"deg":260},"clouds":{"all":100},"dt":1767318036,"sys":{"type":1,"id":5237,"country":"US","sunrise":1767274138,"sunset":1767306375},"timezone":-21600,"id":5265838,"name":"Oshkosh","cod":200} 

  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println("Weather JSON:");
    Serial.println(payload);

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      float temperature_new = doc["main"]["temp"];
      int humidity_new = doc["main"]["humidity"];
      String icon_new = doc["weather"][0]["icon"];

      // new values for temp and humidity will be saved;
      savedTemp = temperature_new;
      savedHumid = humidity_new;
      Serial.println("Data saved!");

      String temperatureformatted = String(temperature_new, 1) + " F";
      String humidityformatted = String(humidity_new) + "%";

      if (icon_new == "01d" || icon_new == "01n") {
        icon = epd_bitmap_clear_sky;
      } else if (icon_new == "02d" || icon_new == "02n") {
        icon = epd_bitmap_few_clouds;
      } else if (icon_new == "03d" || icon_new == "03n") {
        icon = epd_bitmap_scattered_clouds;
      } else if (icon_new == "04d" || icon_new == "04n") {
        icon = epd_bitmap_broken_clouds;
      } else if (icon_new == "09d" || icon_new == "09n") {
        icon = epd_bitmap_shower_rain;
      } else if (icon_new == "10d" || icon_new == "10n") {
        icon = epd_bitmap_rain;
      } else if (icon_new == "11d" || icon_new == "11n") {
        icon = epd_bitmap_thunderstorm;
      } else if (icon_new == "13d" || icon_new == "13n") {
        icon = epd_bitmap_snow;
      } else if (icon_new == "50d" || icon_new == "50n") {
        icon = epd_bitmap_mist;
      } else {
        icon = epd_bitmap_clear_sky;
      }
      temperature = temperatureformatted;
      humidity = humidityformatted;
    } else {
      Serial.println("JSON parsing failed");
      temperature = "!JSON";
      humidity = "!JSON";
    }
  } else {
    Serial.print("HTTP GET failed, code: ");
    Serial.println(httpCode);
    temperature = "!" + httpCode;
    humidity = "!" + httpCode;
  }
  http.end();
  Serial.println("Temp: " + temperature);
  Serial.println("Humidity: " + humidity);
}

void init_and_set_time_zone(String timezone)
{
  // argument timezone will be something like: "CST6CDT,M3.2.0,M11.1.0"
  // for central standard time in the US;
  
  // (1) connect to Network Time Protocol (NTP) server and adjust timezone;
  struct tm timeinfo;
  Serial.println("Setting up time...");
  configTime(0, 0, "pool.ntp.org"); // first, connect to NTP server, with 0 TZ offset;
  if ( !getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time. Check the WiFi connection...");
  } 
  Serial.println("Got the time from NTP");
  
  // (2) now, we can set the real timezone;
  Serial.printf("Setting Timezone to %s...\n", timezone.c_str());
  // now, adjust the TZ; clock settings are adjusted to show the new local time;
  setenv("TZ", timezone.c_str(), 1);
  tzset();
}

void get_date_and_time()
{
  // get date and time; place into global variable date_and_time;
  struct tm timeinfo;
  if ( !getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time. Check the WiFi connection...");
  } 
  strftime( date_and_time, sizeof(date_and_time), "%Y/%m/%d-%H:%M:%S", &timeinfo);
  //Serial.println(date_and_time);
}
