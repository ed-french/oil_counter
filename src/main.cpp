#include <Arduino.h>

#include <WiFi.h>
#include <HTTPClient.h>
#include <soc/rtc.h>
#include "credentials.h"
#include "SPI.h"
#include "TFT_eSPI.h"
#include <Preferences.h>





#define INTERVAL_S 17*60 // Every 17 mins - preserves battery life and also avoids repeated collisions

#define PIN_HALF_BAT_ADC 34

#define PIN_FIVE_VOLTS 32

#define FIVE_VOLT_THRESHOLD 3.9

#define WIFI_CONNECT_TIMEOUT 20000

// Use hardware SPI
TFT_eSPI tft = TFT_eSPI();

Preferences preferences;

bool last_successfully_sent_burning;

const char* hostname = "oilflowsensor";


String serverName = "http://192.168.1.125/oil_sensor";

bool burning=false;
bool old_burning=false;

bool connect_to_wifi()
{
  // returns true if successful
  tft.fillScreen(TFT_DARKGREEN);
    tft.setCursor(0,0);
  tft.setTextColor(TFT_WHITE);
  tft.println("Connecting:");
  WiFi.setHostname(hostname);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  delay(500);
  WiFi.begin(wifi_ssid, wifi_password);
  delay(1000);
  Serial.printf("Connecting to: %s\n",wifi_ssid);
  Serial.println("");

  uint32_t wifi_timeout=millis()+WIFI_CONNECT_TIMEOUT;


  // Wait for connection
  while (true)
  {
    tft.print(".");
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("\nWifi now connected");
      break;
    }
    if (millis()>wifi_timeout)
    {
      Serial.println("Pointless message saying we are restarting to have another go at connecting");
      return false;
    }

    delay(500);
    Serial.print(".");
  }
  tft.fillScreen(TFT_BLACK);
    tft.setCursor(0,0);
  tft.println("Connected:");
  tft.println(WiFi.localIP());
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(wifi_ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  delay(1000);
  return true;

}

void setup() {
    setCpuFrequencyMhz(80);
    Serial.begin(115200);
    delay(500);
    tft.begin();

    tft.setRotation(1);
    tft.setTextColor(TFT_WHITE);

    tft.setTextSize(2);

    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0,0);
    tft.println("booting...");
    Serial.println("Running...");
    delay(5000);
    pinMode(PIN_FIVE_VOLTS,INPUT);
    if (!connect_to_wifi())
    {
      ESP.restart();
    }



    



  // At boot we have woken from sleep, so previously we weren't burning

  preferences.begin("oilflow");

  old_burning=preferences.getBool("lastburn",false);
  last_successfully_sent_burning=old_burning;

}

int16_t get_battery_reading(uint8_t tries=8)
{
  int32_t accum=0;
  for (uint8_t i=0;i<tries;i++)
  {
    accum+=analogRead(PIN_HALF_BAT_ADC);//4096=3.3V
  }
  return (int16_t)(accum/tries);
}

float running_minutes()
{
  uint32_t millisrunning=millis();
  return millisrunning/60000.0;
}

void show_message(const char * mess,uint16_t delay_ms=2000)
{
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0,0);
  tft.print(mess);
  delay(delay_ms);
}


void send_status(uint16_t bat_reading,float voltage,bool burning,bool scheduled,float bus_volts)
{

  // check wifi still connected, otherwise save last sent and reboot

  if (WiFi.status()!=WL_CONNECTED)
  {
    Serial.println("WIFI DOWN!!!!");
    show_message("WiFi Down",3000);
    preferences.putBool("lastburn",last_successfully_sent_burning);
    ESP.restart();
  }


  // Make request

    HTTPClient http;

    String serverPath = serverName + "?volts="+String(voltage)+ \
                        "&reading="+String(bat_reading)+ \
                        "&burning="+String(burning?"yes":"no")+ \
                        "&scheduled="+String(scheduled?"yes":"no")+ \
                        "&vbus="+String(bus_volts);
    uint8_t tries_left=4;


    while (tries_left>0)
    {
      char mess_buff[255];
      http.begin(serverPath.c_str());

      // Send HTTP GET request
      int httpResponseCode = http.GET();

      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);      

      sprintf(mess_buff,"HTTP RESPONSE:\n%d",httpResponseCode);
      show_message(mess_buff);

      if (httpResponseCode>0)
      {
        String payload = http.getString();
        Serial.println(payload); 

        if (httpResponseCode==200)
        {
          Serial.println("Done.");
          last_successfully_sent_burning=burning;
          break;
        }
        Serial.print("Error code: ");
        Serial.println(httpResponseCode);

        Serial.println("Trying again...");
        delay(100);

      }

      http.end();      
      


      tries_left--;
    }



}

void loop()
{
  // put your main code here, to run repeatedly:

  uint32_t next_scheduled_send=millis()+1000*INTERVAL_S;

  bool reached_schedule=false;
  int16_t bat_reading;
  float voltage;
  float vbus;
  int16_t vbus_raw;

  while(true)
  {
 
    bat_reading=get_battery_reading();//4096=3.3V
    voltage=bat_reading/4096.0*3.3*2.0;

    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0,0);
    tft.printf("Mins:\n%.4f\nBat: %f",running_minutes(),voltage);
    
    

    //burning=(voltage>4.3);

    vbus=analogRead(PIN_FIVE_VOLTS)/4096.0*3.3*2.0;
    Serial.printf("Vbus=%f\n",vbus);


    burning=vbus>FIVE_VOLT_THRESHOLD;

    Serial.printf("Bat reading: %d\tVoltage: %f\n",bat_reading,voltage);

    if (millis()>next_scheduled_send)
    {
      reached_schedule=true;
      break; //Send now
    }
    reached_schedule=false;

    if (burning!=old_burning)
    {
      Serial.println("Burn state changed, send immediately");
      break;
    }
    delay(1000);

  } // End of wait for change or timeout bit

  old_burning=burning;


  delay(100); // Let Vcc stablise after power 
  // Now update the battery level measurement
  bat_reading=get_battery_reading();//4096=3.3V
  voltage=bat_reading/4096.0*3.3*2.0;
  
  send_status(bat_reading,voltage,burning,reached_schedule,vbus);
}    
    

    












    
