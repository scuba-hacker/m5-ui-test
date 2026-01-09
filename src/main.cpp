#include <Arduino.h>
#include <M5StickCPlus.h>


bool useGetDepthAsync = true;
bool recordSurveyHighlight = false, recordBreadCrumbTrail = false;
unsigned long recordHighlightExpireTime = 0;
float depth = 0.0, Lat = 0.0, Lng = 0.0;
float magnetic_heading = 0.0, journey_course = 0.0, journey_distance = 0.0;
float water_temperature = 0.0, humidity = 0.0;
unsigned long last_journey_commit_time = 0;
const unsigned long journey_clear_period = 5UL * 1000UL; // 5 seconds
bool diveTimerRunning = false;
unsigned long whenToStopTimerDueToLackOfDepth = 0;
uint16_t minutesDurationDiving = 0;

bool gps_ok = true, gpsTargetTimedOut = false, internetUploadOk = true;
std::string cardinal = "N";

// forward declarations
void drawSurveyDisplay();

// Stubs
bool isGPSStreamOk() { return gps_ok; }
bool isGPSTargetShortTimedOut() { return gpsTargetTimedOut; }
bool isInternetUploadOk() { return internetUploadOk; }
std::string getCardinal(float b, bool surveyScreen = false) { return cardinal; }

void publishToOceanicPinPlaced(float Lat,float Lng,float magnetic_heading, float depth) {}

void setup()
{
    M5.begin();
    M5.Lcd.fillScreen(TFT_BLACK);

    Serial.begin(115200);
}

void loop()
{
    drawSurveyDisplay();

    delay(500);
}

void drawSurveyDisplay()
{
    M5.Lcd.setRotation(0);
    M5.Lcd.setCursor(15, 0);

    if (useGetDepthAsync)
      M5.Lcd.setTextColor(TFT_CYAN, TFT_BLACK);
    else
      M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);

    M5.Lcd.setTextSize(6);
    if (depth < 10.0)
      M5.Lcd.printf("%.1f\n", depth);    
    else
      M5.Lcd.printf("%.0fm\n", depth);

    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(TFT_MAGENTA, TFT_BLACK);
    M5.Lcd.print("\n");
    M5.Lcd.setTextSize(4);

    if (recordHighlightExpireTime != 0)
    {
      if (millis() < recordHighlightExpireTime)
      {
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
        M5.Lcd.print("     \n");
      }
    }
    else if (recordBreadCrumbTrail)
    {
        M5.Lcd.print("-REC-\n");
    }
    else
    {
      if  (millis() - journey_clear_period > last_journey_commit_time || journey_distance == 0)
      {
        M5.Lcd.print("     \n");
      }
      else
      {
        const bool surveyScreen = true;
        M5.Lcd.printf("%s\n", getCardinal(journey_course,surveyScreen).c_str());
      }
    }
        
    M5.Lcd.setTextSize(1);
    M5.Lcd.println("\n");

    M5.Lcd.setTextSize(7);

    if (diveTimerRunning == false && minutesDurationDiving == 0)    
      M5.Lcd.setTextColor(TFT_BLUE, TFT_BLACK);     // dive not started yet
    else if (diveTimerRunning == false && minutesDurationDiving > 0)
      M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);      // dive finished
    else if (diveTimerRunning == true && whenToStopTimerDueToLackOfDepth == 0)
      M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);    // dive in progress
    else if (diveTimerRunning == true && whenToStopTimerDueToLackOfDepth > 0)
      M5.Lcd.setTextColor(TFT_ORANGE, TFT_BLACK);   // dive in progress but not at minimum depth
    else
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);  // shouldn't get here.
      
    M5.Lcd.printf("%2hu'", minutesDurationDiving);

    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Lcd.printf("\n\n\n\n   %.1f", water_temperature);

    // print degrees sign
    int16_t x = M5.Lcd.getCursorX(), y = M5.Lcd.getCursorY();
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(x+2, y-4);
    M5.Lcd.print("o");
    M5.Lcd.setCursor(x, y);
    M5.Lcd.setTextSize(2);
    M5.Lcd.printf("\n\n");

    M5.Lcd.setTextSize(3);

    // Set GPS 'G' character background color based on fix message timeout
    if (!isGPSStreamOk())
      M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);        // Red after 10 seconds no fix
    else if (isGPSTargetShortTimedOut())
      M5.Lcd.setTextColor(TFT_BLACK, TFT_YELLOW);     // Yellow after 3 seconds no fix  
    else
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);      // Black when GPS fixes current

    M5.Lcd.print("G");

    M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);

    M5.Lcd.printf(" %.0f%% ", humidity);

    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    
    if (isInternetUploadOk())
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    else
      M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
    
    M5.Lcd.printf("Q");
}
