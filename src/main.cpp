#include <Arduino.h>
#include <M5StickCPlus.h>

bool useGetDepthAsync = true;
bool recordSurveyHighlight = false, recordBreadCrumbTrail = false;
uint32_t recordHighlightExpireTime = 0;
float depth = 0.0, Lat = 0.0, Lng = 0.0;
float magnetic_heading = 0.0, journey_course = 90.0, journey_distance = 0.0;
float water_temperature = 0.0, humidity = 0.0;
uint32_t last_journey_commit_time = 0;
const uint32_t journey_clear_period = 800; // 0.8 seconds
bool diveTimerRunning = false;
unsigned long whenToStopTimerDueToLackOfDepth = 0;
uint16_t minutesDurationDiving = 0;
bool gps_ok = true, gpsTargetTimedOut = false, internetUploadOk = true;

// forward declarations
void drawSurveyDisplay();
void drawSurveyDisplaySmooth();
void dumpHeapUsage(const char* msg);

// Stubs
bool isGPSStreamOk() { return gps_ok; }
bool isGPSTargetShortTimedOut() { return gpsTargetTimedOut; }
bool isInternetUploadOk() { return internetUploadOk; }
void publishToOceanicPinPlaced(float Lat,float Lng,float magnetic_heading, float depth) {}

std::string getCardinal(float b, bool surveyScreen) 
{
  std::string result = "---";

  if      (b > 337.5 || b <= 22.5) result = (surveyScreen ? "North" : "N  ");  // 0
  else if (b > 22.5 && b <= 67.5) result = (surveyScreen ? "NE" : "NE ");  // 45
  else if (b > 67.5 && b <= 112.5) result = (surveyScreen ? "East" : "E  ");  // 90
  else if (b > 112.5 && b <= 157.5) result = (surveyScreen ? "SE" : "SE "); // 135
  else if (b > 157.5 && b <= 202.5) result = (surveyScreen ? "South" : "S  "); // 180
  else if (b > 202.5 && b <= 247.5) result = (surveyScreen ? "SW" : "SW "); // 225
  else if (b > 247.5 && b <= 292.5) result = (surveyScreen ? "West" : "W  "); // 270
  else if (b > 292.5 && b <= 337.5) result = (surveyScreen ? "NW" : "NW "); // 315

  return result;
}

void cycleCourse()
{
  static int step = 0;
  float& jd = journey_distance;
  jd = 1;
  last_journey_commit_time = millis();

  switch (step)
  {
    case 0: journey_course = 0.0; break;
    case 1: journey_course = 45.0; break;
    case 2: journey_course = 90.0; break;
    case 3: journey_course = 135.0; break;
    case 4: journey_course = 180.0; break;
    case 5: journey_course = 225.0; break;
    case 6: journey_course = 270.0; break;
    case 7: journey_course = 315.0; break;
  }

  step = (step + 1) % 8;
}

/*
Limitations:
Depth range: 0.0 to 99.9 meters
Dive time range: 0 to 99 minutes
Humidity range: 10 to 100%
temp range: 0.0 to 30.0 degrees C
*/
void cycleDepthTempHumidity()
{
  static int step = 0;
  float& t = water_temperature, &h = humidity;
  bool& i = internetUploadOk, &g = gps_ok, &as = useGetDepthAsync;
  uint16_t& mins=minutesDurationDiving;
  uint32_t& rec_tm = recordHighlightExpireTime;
  bool& rec_bdt = recordBreadCrumbTrail;

  uint32_t now = millis();
  uint32_t dur = 600;
  uint32_t exp = now + dur;

  switch (step)
  {
    case 0: depth = 0.0; t = 0.0; h = 99.0; mins = 0; rec_tm = exp; break;
    case 1: depth = 1.1; t = 1.0; h = 8.0; mins++;break;
    case 2: depth = 2.2; t = 2.0; h = 10.0;mins++; break;
    case 3: depth = 10.3; t = 5.0; h = 20.0;mins++; i = false; g = false; break;
    case 4: depth = 11.0; t = 5.5; h = 30.0;mins++; as = false; break;
    case 5: depth = 22.1; t = 7.0; h = 10.0;mins++;rec_tm = exp; break;
    case 6: depth = 33.2; t = 8.0; h = 20.0;mins=10;break;
    case 7: depth = 44.5; t = 9.0; h = 50.0;mins=11; rec_bdt = true; break;
    case 8: depth = 31.1; t = 10.0; h = 55;mins++;  break;
    case 9: depth = 11.1; t = 9.5; h = 51; mins=12; i = true; rec_bdt = false; as = true; break;
    case 10: depth =22.2; t = 23.1; h = 80; mins=13; rec_tm = exp; break;
    case 11: depth =22.3; t = 1.0; h = 90; mins=98; g = true; break;
    case 12: depth =22.1; t = 18.0; h = 100; mins=99; break;
  }
  step = (step + 1) % 13;
}

void setup()
{
    M5.begin();
    M5.Lcd.fillScreen(TFT_BLACK);

    Serial.begin(115200);
}

void loop()
{
  uint32_t wait = 100;
    drawSurveyDisplaySmooth();
    cycleDepthTempHumidity();
    delay(wait);
    cycleCourse();
    delay(wait);
}

// total sprite RAM usage: 58652 bytes
void drawSurveyDisplaySmooth()
{
    int16_t humiditySpriteWidth = TFT_WIDTH, temperatureSpriteWidth = TFT_WIDTH, pinSpriteWidth = TFT_WIDTH, depthSpriteWidth = TFT_WIDTH, timerSpriteWidth = TFT_WIDTH;
    int16_t humiditySpriteHeight = 30, temperatureSpriteHeight = 18, pinSpriteHeight = 30, depthSpriteHeight = 72, timerSpriteHeight = 80 /* was 64 */;
    int16_t humiditySpriteY = 210, temperatureSpriteY = 184, pinSpriteY = 69 /*(was 72)*/, depthSpriteY = 0, timerSpriteY = 105 /* was 120 */;

    // sprites must not be deleted after use - affects the parent TFT_eSPI object for screen
    static TFT_eSprite humiditySprite(&M5.Lcd);
    static TFT_eSprite temperatureSprite(&M5.Lcd);
    static TFT_eSprite pinSprite(&M5.Lcd);
    static TFT_eSprite depthSprite(&M5.Lcd);
    static TFT_eSprite timerSprite(&M5.Lcd);
    static bool inited = false;

    if (!inited)
    {
      if (!humiditySprite.createSprite(humiditySpriteWidth,humiditySpriteHeight) ||
          !temperatureSprite.createSprite(temperatureSpriteWidth,temperatureSpriteHeight) || 
          !pinSprite.createSprite(pinSpriteWidth,pinSpriteHeight) ||
          !depthSprite.createSprite(depthSpriteWidth,depthSpriteHeight) ||
          !timerSprite.createSprite(timerSpriteWidth,timerSpriteHeight)
        )
      {
        return;
      }
      inited = true;
    }

    int charWidth=0, offset=0;
    M5.Lcd.setRotation(0);

    // print to main display: depth
    depthSprite.fillSprite(TFT_BLACK);

    bool filledBackground = true;
    if (useGetDepthAsync)
      depthSprite.setTextColor(TFT_CYAN, TFT_BLACK,filledBackground);
    else
      depthSprite.setTextColor(TFT_GREEN, TFT_BLACK,filledBackground);

    depthSprite.setCursor(15, 0);
    depthSprite.setFreeFont(&FreeSansBold18pt7b);
    depthSprite.setTextSize(2);
    depthSprite.setTextDatum(BL_DATUM);
    depthSprite.setTextWrap(false);
    if (depth < 10.0)
    {
      depthSprite.setCursor(0, 55);
      depthSprite.printf("%.1f", depth);
    }
    else
    {
      depthSprite.setCursor(10, 55);
      depthSprite.printf("%.0f", depth);
    }

    depthSprite.setFreeFont(&FreeSansBold12pt7b);
    depthSprite.print("m");

    depthSprite.pushSprite(0, depthSpriteY);

    // print to pin sprite: -PIN-, -REC-, journey course or blank
    pinSprite.fillSprite(TFT_BLACK);
    pinSprite.setTextColor(TFT_MAGENTA, TFT_BLACK,filledBackground);
    pinSprite.setFreeFont(&FreeSansBold18pt7b);

    pinSprite.setTextDatum(TC_DATUM);
    if (recordHighlightExpireTime != 0)
    {
      if (millis() < recordHighlightExpireTime)
      {
        pinSprite.drawString("--PIN--",  pinSprite.width() / 2, 0);  // Draw string using specified font number

        if (recordSurveyHighlight)
        { // flag gets reset in telemetry code after being logged first time
          // Also publish is rate-limited internally to max 1 call per 5 seconds
          publishToOceanicPinPlaced(Lat,Lng,magnetic_heading,depth);
        }
      }
      else
      {
        recordHighlightExpireTime = 0;
      }
    }
    else if (recordBreadCrumbTrail)
    {
        pinSprite.drawString("--REC--",  pinSprite.width() / 2, 0);  // Draw string using specified font number
    }
    else
    {
      if  (millis() - journey_clear_period > last_journey_commit_time || journey_distance == 0)
      {
        // print nothing
      }
      else
      {
        const bool surveyScreen = true;
        std::string cardinal = getCardinal(journey_course,surveyScreen).c_str();
        pinSprite.drawString(cardinal.c_str(),  pinSprite.width() / 2, 0);  // Draw string using specified font number
      }
    }
    pinSprite.pushSprite(0, pinSpriteY);

    timerSprite.fillSprite(TFT_BLACK);

    // Print dive timer directly to display
    if (diveTimerRunning == false && minutesDurationDiving == 0)    
      timerSprite.setTextColor(TFT_BLUE, TFT_BLACK, filledBackground);     // dive not started yet
    else if (diveTimerRunning == false && minutesDurationDiving > 0)
      timerSprite.setTextColor(TFT_RED, TFT_BLACK, filledBackground);      // dive finished
    else if (diveTimerRunning == true && whenToStopTimerDueToLackOfDepth == 0)
      timerSprite.setTextColor(TFT_GREEN, TFT_BLACK, filledBackground);    // dive in progress
    else if (diveTimerRunning == true && whenToStopTimerDueToLackOfDepth > 0)
      timerSprite.setTextColor(TFT_ORANGE, TFT_BLACK, filledBackground);   // dive in progress but not at minimum depth
    else
      timerSprite.setTextColor(TFT_WHITE, TFT_BLACK, filledBackground);  // shouldn't get here.

    timerSprite.setFreeFont(&FreeSansBold24pt7b);
    timerSprite.setTextSize(2);
    offset = (minutesDurationDiving < 10) ? 40 : 5;
    timerSprite.setCursor(offset, 68);
    timerSprite.printf("%hu'", minutesDurationDiving);
    timerSprite.pushSprite(0, timerSpriteY);

    // print to sprite water temperature and degrees sign
    temperatureSprite.fillSprite(TFT_BLACK);
    temperatureSprite.setTextSize(2);
    temperatureSprite.setTextColor(TFT_GREEN, TFT_BLACK, filledBackground);
    charWidth = 12;
    const int degreeSignXOffset = 2, degreeSignYOffset = 4;
    int len = (water_temperature < 9.99 ? 3 : 4);
    temperatureSprite.setCursor((TFT_WIDTH-len*charWidth)/2, degreeSignYOffset);
    temperatureSprite.printf("%.1f", water_temperature);
    temperatureSprite.setTextSize(1);
    temperatureSprite.setCursor(temperatureSprite.getCursorX()+degreeSignXOffset, 0);
    temperatureSprite.print("o");
    temperatureSprite.pushSprite(0, temperatureSpriteY);










    // print to humidity sprite: gps status, humidity, internet status
    humiditySprite.fillSprite(TFT_BLACK);
    humiditySprite.setFreeFont(&FreeMonoBold18pt7b);
    humiditySprite.setTextDatum(TL_DATUM);

    // Set GPS 'G' character background color based on fix message timeout
    if (!isGPSStreamOk())
      humiditySprite.setTextColor(TFT_WHITE, TFT_RED, filledBackground);        // Red after 10 seconds no fix
    else if (isGPSTargetShortTimedOut())
      humiditySprite.setTextColor(TFT_BLACK, TFT_YELLOW, filledBackground);     // Yellow after 3 seconds no fix  
    else
      humiditySprite.setTextColor(TFT_WHITE, TFT_BLACK, filledBackground);      // Black when GPS fixes current

    humiditySprite.drawString("G",0,0);

    humiditySprite.setFreeFont(&FreeMonoBold18pt7b); // FreeSansBold18pt7b
    humiditySprite.setTextDatum(TC_DATUM);
    // print humidity value
    humiditySprite.setTextColor(TFT_YELLOW, TFT_BLACK, filledBackground);

    char humidityBuffer[5];
    snprintf(humidityBuffer, sizeof(humidityBuffer), "%.0f%%", humidity);
    humiditySprite.drawString(humidityBuffer,pinSprite.width() / 2,0);

    humiditySprite.setFreeFont(&FreeMonoBold18pt7b);
    humiditySprite.setTextDatum(TR_DATUM);
    // Set Internet upload 'Q' character background color based on internet upload status
    if (isInternetUploadOk())
      humiditySprite.setTextColor(TFT_WHITE, TFT_BLACK, filledBackground);
    else
      humiditySprite.setTextColor(TFT_WHITE, TFT_RED, filledBackground);
    
    humiditySprite.drawString("Q",pinSprite.width(),0);

    humiditySprite.pushSprite(0, humiditySpriteY);
}

// total sprite RAM usage: 21892 bytes
void drawSurveyDisplay()
{
    int16_t humiditySpriteWidth = TFT_WIDTH, temperatureSpriteWidth = TFT_WIDTH, pinSpriteWidth = TFT_WIDTH;
    int16_t humiditySpriteHeight = 30, temperatureSpriteHeight = 20, pinSpriteHeight = 30;
    int16_t humiditySpriteY = 216, temperatureSpriteY = 184, pinSpriteY = 72;
    int16_t timerScreenY = 120;

    // sprites must not be deleted after use - affects the parent TFT_eSPI object for screen
    static TFT_eSprite humiditySprite(&M5.Lcd);
    static TFT_eSprite temperatureSprite(&M5.Lcd);
    static TFT_eSprite pinSprite(&M5.Lcd);
    static bool inited = false;

    if (!inited)
    {
      if (!humiditySprite.createSprite(humiditySpriteWidth,humiditySpriteHeight) ||
          !temperatureSprite.createSprite(temperatureSpriteWidth,temperatureSpriteHeight) || 
          !pinSprite.createSprite(pinSpriteWidth,pinSpriteHeight))
      {
        return;
      }
      inited = true;
    }

    int charWidth=0, offset=0;
    M5.Lcd.setRotation(0);

    // print to main display: depth
    bool filledBackground = true;
    if (useGetDepthAsync)
      M5.Lcd.setTextColor(TFT_CYAN, TFT_BLACK,filledBackground);
    else
      M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK,filledBackground);

    M5.Lcd.setCursor(15, 0);
    M5.Lcd.setTextSize(6);
    if (depth < 10.0)
      M5.Lcd.printf("%.1f\n", depth);    
    else
      M5.Lcd.printf("%.0fm\n", depth);

    // print to pin sprite: -PIN-, -REC-, journey course or blank
    pinSprite.fillSprite(TFT_BLACK);
    pinSprite.setTextColor(TFT_MAGENTA, TFT_BLACK,filledBackground);
    pinSprite.setTextSize(4);

    if (recordHighlightExpireTime != 0)
    {
      if (millis() < recordHighlightExpireTime)
      {
        pinSprite.setCursor(10, 0);        
        pinSprite.print("-PIN-");

        if (recordSurveyHighlight)
        { // flag gets reset in telemetry code after being logged first time
          // Also publish is rate-limited internally to max 1 call per 5 seconds
          publishToOceanicPinPlaced(Lat,Lng,magnetic_heading,depth);
        }
      }
      else
      {
        recordHighlightExpireTime = 0;
      }
    }
    else if (recordBreadCrumbTrail)
    {
        pinSprite.setCursor(10, 0);        
        pinSprite.print("-REC-");
    }
    else
    {
      if  (millis() - journey_clear_period > last_journey_commit_time || journey_distance == 0)
      {
        // print nothing
      }
      else
      {
        const bool surveyScreen = true;
        std::string cardinal = getCardinal(journey_course,surveyScreen).c_str();
        charWidth = 23;
        offset = (TFT_WIDTH - cardinal.length() * charWidth) / 2;
        pinSprite.setCursor(offset, 0);
        pinSprite.printf("%s", cardinal.c_str());
      }
    }
    pinSprite.pushSprite(0, pinSpriteY);

    // Print dive timer directly to display
    if (diveTimerRunning == false && minutesDurationDiving == 0)    
      M5.Lcd.setTextColor(TFT_BLUE, TFT_BLACK, filledBackground);     // dive not started yet
    else if (diveTimerRunning == false && minutesDurationDiving > 0)
      M5.Lcd.setTextColor(TFT_RED, TFT_BLACK, filledBackground);      // dive finished
    else if (diveTimerRunning == true && whenToStopTimerDueToLackOfDepth == 0)
      M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK, filledBackground);    // dive in progress
    else if (diveTimerRunning == true && whenToStopTimerDueToLackOfDepth > 0)
      M5.Lcd.setTextColor(TFT_ORANGE, TFT_BLACK, filledBackground);   // dive in progress but not at minimum depth
    else
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK, filledBackground);  // shouldn't get here.

    M5.Lcd.setTextSize(7);
    M5.Lcd.setCursor(8, timerScreenY);
    M5.Lcd.print(" ");
    offset = (minutesDurationDiving < 10) ? 50 : 8;
    M5.Lcd.setCursor(offset, timerScreenY);
    M5.Lcd.printf("%hu'", minutesDurationDiving);

    // print to sprite water temperature and degrees sign
    temperatureSprite.fillSprite(TFT_BLACK);
    temperatureSprite.setTextSize(2);
    temperatureSprite.setTextColor(TFT_GREEN, TFT_BLACK, filledBackground);
    charWidth = 12;
    const int degreeSignXOffset = 2, degreeSignYOffset = 4;
    int len = (water_temperature < 9.99 ? 3 : 4);
    temperatureSprite.setCursor((TFT_WIDTH-len*charWidth)/2, degreeSignYOffset);
    temperatureSprite.printf("%.1f", water_temperature);
    temperatureSprite.setTextSize(1);
    temperatureSprite.setCursor(temperatureSprite.getCursorX()+degreeSignXOffset, 0);
    temperatureSprite.print("o");
    temperatureSprite.pushSprite(0, temperatureSpriteY);

    // print to humidity sprite: gps status, humidity, internet status
    humiditySprite.fillSprite(TFT_BLACK);
    humiditySprite.setTextSize(3);
    humiditySprite.setCursor(0,0);
    charWidth = 16;

    // Set GPS 'G' character background color based on fix message timeout
    if (!isGPSStreamOk())
      humiditySprite.setTextColor(TFT_WHITE, TFT_RED, filledBackground);        // Red after 10 seconds no fix
    else if (isGPSTargetShortTimedOut())
      humiditySprite.setTextColor(TFT_BLACK, TFT_YELLOW, filledBackground);     // Yellow after 3 seconds no fix  
    else
      humiditySprite.setTextColor(TFT_WHITE, TFT_BLACK, filledBackground);      // Black when GPS fixes current

    humiditySprite.print("G");
    // print humidity value
    humiditySprite.setTextColor(TFT_YELLOW, TFT_BLACK, filledBackground);
    len = (humidity < 9.99) ? 2 : ((humidity < 99.99) ? 3 : 4);
    humiditySprite.setCursor((TFT_WIDTH-len*charWidth)/2, 0);
    humiditySprite.printf("%.0f%%", humidity);

    // Set Internet upload 'Q' character background color based on internet upload status
    humiditySprite.setCursor(115, humiditySprite.getCursorY()); 
    if (isInternetUploadOk())
      humiditySprite.setTextColor(TFT_WHITE, TFT_BLACK, filledBackground);
    else
      humiditySprite.setTextColor(TFT_WHITE, TFT_RED, filledBackground);
    
    humiditySprite.print("Q");

    humiditySprite.pushSprite(0, humiditySpriteY);
}

void dumpHeapUsage(const char* msg)
{  
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); // internal RAM, memory capable to store data or to create new task
    Serial.printf("\n%s : free heap bytes: %i  largest free heap block: %i min free ever: %i\n",  msg, info.total_free_bytes, info.largest_free_block, info.minimum_free_bytes);
}