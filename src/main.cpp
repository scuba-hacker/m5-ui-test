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

void incCourse()
{
  journey_course += 45;
  if (journey_course >= 360.0)
    journey_course -= 360.0;
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
  uint32_t now = millis();
  uint32_t dur = 800;
  uint32_t exp = now + dur;

  switch (step)
  {
    case 0: depth = 0.0; t = 0.0; h = 99.0; mins = 0; rec_tm = exp; break;
    case 1: depth = 1.1; t = 1.0; h = 8.0; mins++;break;
    case 2: depth = 2.2; t = 2.0; h = 10.0;mins++; break;
    case 3: depth = 3.3; t = 5.0; h = 20.0;mins++; i = false; g = false; break;
    case 4: depth = 10.0; t = 5.5; h = 30.0;mins++; as = false; break;
    case 5: depth = 20.0; t = 7.0; h = 10.0;mins++;rec_tm = exp; break;
    case 6: depth = 30.0; t = 8.0; h = 20.0;mins=10;break;
    case 7: depth = 40.0; t = 9.0; h = 50.0;mins=11; recordBreadCrumbTrail = true; break;
    case 8: depth = 30.0; t = 10.0; h = 55;mins++; recordBreadCrumbTrail = false; break;
    case 9: depth = 10.0; t = 9.5; h = 51; mins=12; i = true; as = true; break;
    case 10: depth = 6.0; t = 23.1; h = 80; mins=13; rec_tm = exp; break;
    case 11: depth = 2.0; t = 1.0; h = 90; mins=98; g = true; break;
    case 12: depth = 0.1; t = 18.0; h = 100; mins=99; break;
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
    drawSurveyDisplay();
    cycleDepthTempHumidity();
    delay(500);
    cycleCourse();
    delay(500);
}

void drawSurveyDisplay()
{
    int charWidth=0, offset=0;
    M5.Lcd.setRotation(0);
    M5.Lcd.setCursor(15, 0);

    bool filledBackground = true;
    if (useGetDepthAsync)
      M5.Lcd.setTextColor(TFT_CYAN, TFT_BLACK,filledBackground);
    else
      M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK,filledBackground);

    M5.Lcd.setTextSize(6);
    if (depth < 10.0)
      M5.Lcd.printf("%.1f\n", depth);    
    else
      M5.Lcd.printf("%.0fm\n", depth);

    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(TFT_MAGENTA, TFT_BLACK,filledBackground);
    M5.Lcd.print("\n");
    M5.Lcd.setTextSize(4);

    if (recordHighlightExpireTime != 0)
    {
      if (millis() < recordHighlightExpireTime)
      {
        M5.Lcd.fillRect(0, M5.Lcd.getCursorY(), TFT_WIDTH, 30, TFT_BLACK);
        M5.Lcd.setCursor(10, M5.Lcd.getCursorY());        
        M5.Lcd.printf("-PIN-\n");

        if (recordSurveyHighlight)
        { // flag gets reset in telemetry code after being logged first time
          // Also publish is rate-limited internally to max 1 call per 5 seconds
          publishToOceanicPinPlaced(Lat,Lng,magnetic_heading,depth);
        }
      }
      else
      {
        recordHighlightExpireTime = 0;
        M5.Lcd.fillRect(0, M5.Lcd.getCursorY(), TFT_WIDTH, 30, TFT_BLACK);
        M5.Lcd.print("\n");
      }
    }
    else if (recordBreadCrumbTrail)
    {
        M5.Lcd.fillRect(0, M5.Lcd.getCursorY(), TFT_WIDTH, 30, TFT_BLACK);
        M5.Lcd.setCursor(10, M5.Lcd.getCursorY());        
        M5.Lcd.print("-REC-\n");
    }
    else
    {
      if  (millis() - journey_clear_period > last_journey_commit_time || journey_distance == 0)
      {
        M5.Lcd.fillRect(0, M5.Lcd.getCursorY(), TFT_WIDTH, 30, TFT_BLACK);
        M5.Lcd.print("\n");
      }
      else
      {
        M5.Lcd.fillRect(0, M5.Lcd.getCursorY(), TFT_WIDTH, 30, TFT_BLACK);

        const bool surveyScreen = true;
        std::string cardinal = getCardinal(journey_course,surveyScreen).c_str();
        charWidth = 23;
        offset = (TFT_WIDTH - cardinal.length() * charWidth) / 2;
        M5.Lcd.setCursor(offset, M5.Lcd.getCursorY());
        M5.Lcd.printf("%s\n", cardinal.c_str());
      }
    }
        
    M5.Lcd.setTextSize(1);
    M5.Lcd.println("\n");

    M5.Lcd.setTextSize(7);

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

    M5.Lcd.fillRect(0, M5.Lcd.getCursorY(), 45, 50, TFT_BLACK);
    offset = (minutesDurationDiving < 10) ? 50 : 8;
    M5.Lcd.setCursor(offset, M5.Lcd.getCursorY());
    M5.Lcd.printf("%hu'", minutesDurationDiving);

    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK, filledBackground);

    M5.Lcd.print("\n\n\n\n");

    M5.Lcd.fillRect(M5.Lcd.getCursorX(), M5.Lcd.getCursorY()-5, TFT_WIDTH, 20, TFT_BLACK);

    charWidth = 12;
    int len = (water_temperature < 9.99 ? 3 : 4);
    M5.Lcd.setCursor((TFT_WIDTH-len*charWidth)/2, M5.Lcd.getCursorY());
    M5.Lcd.printf("%.1f", water_temperature);

    // print degrees sign
    int16_t x = M5.Lcd.getCursorX(), y = M5.Lcd.getCursorY();
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(x+2, y-4);
    M5.Lcd.print("o  ");
    M5.Lcd.setCursor(x, y);

    M5.Lcd.setTextSize(2);
    M5.Lcd.printf("\n\n");

    M5.Lcd.setTextSize(3);

    // Set GPS 'G' character background color based on fix message timeout
    if (!isGPSStreamOk())
      M5.Lcd.setTextColor(TFT_WHITE, TFT_RED, filledBackground);        // Red after 10 seconds no fix
    else if (isGPSTargetShortTimedOut())
      M5.Lcd.setTextColor(TFT_BLACK, TFT_YELLOW, filledBackground);     // Yellow after 3 seconds no fix  
    else
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK, filledBackground);      // Black when GPS fixes current

    M5.Lcd.print("G");

    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK, filledBackground);
    
    if (isInternetUploadOk())
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK, filledBackground);
    else
      M5.Lcd.setTextColor(TFT_WHITE, TFT_RED, filledBackground);
    
    M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK, filledBackground);
    M5.Lcd.fillRect(20, M5.Lcd.getCursorY(), TFT_WIDTH-40, 30, TFT_BLACK);

    charWidth = 16;
    len = (humidity < 9.99) ? 2 : ((humidity < 99.99) ? 3 : 4);
    M5.Lcd.setCursor((TFT_WIDTH-len*charWidth)/2, M5.Lcd.getCursorY());
    M5.Lcd.printf("%.0f%%", humidity);

    M5.Lcd.setCursor(115, M5.Lcd.getCursorY());
    if (isInternetUploadOk())
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK, filledBackground);
    else
      M5.Lcd.setTextColor(TFT_WHITE, TFT_RED, filledBackground);
    
    M5.Lcd.printf("Q");
}
