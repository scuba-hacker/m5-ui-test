#include <Arduino.h>
#include <M5StickCPlus.h>

bool useGetDepthAsync = true;
bool recordSurveyHighlight = false, recordBreadCrumbTrail = false;
uint32_t recordHighlightExpireTime = 0;
float depth = 0.0, Lat = 0.0, Lng = 0.0;
float magnetic_heading = 0.0, journey_course = 90.0, journey_distance = 0.0;
float water_temperature = 0.0, humidity = 0.0;
uint32_t last_journey_commit_time = 0;
uint32_t journey_clear_period = 800; // 0.8 seconds
bool diveTimerRunning = false;
unsigned long whenToStopTimerDueToLackOfDepth = 0;
uint16_t minutesDurationDiving = 0;
bool gps_ok = true, gpsTargetTimedOut = false, internetUploadOk = true;


// Additional variables for target section
enum e_direction_metric {JOURNEY_COURSE, COMPASS_HEADING};
#define NAV_COURSE_DISPLAY 1
int display_to_show = NAV_COURSE_DISPLAY;
int directionMetric = COMPASS_HEADING;
const int GPS_NO_GPS_LIVE_IN_FLOAT = 0;
const int GPS_NO_FIX_FROM_FLOAT = 1;
const int GPS_FIX_FROM_FLOAT = 2;
int GPS_status = 0;
bool hasEverReceivedGPSFix = false;
float heading_to_target = 0.0;
float distance_to_target = 0.0;
int satellites = 0;

// Additional variables for compass section
float temperature = 0;
bool blackout_journey_no_movement = false;

class GPSClass {
  public:
    class HDOPClass {
      public:
        int h;
        int& hdop() { return h; }
    } hdop;
} gps;

// Additional variables for refresh direction graphic section
bool enableNavigationGraphics = true;
enum e_way_marker {BLACKOUT_MARKER, GO_ANTICLOCKWISE_MARKER, GO_AHEAD_MARKER, GO_CLOCKWISE_MARKER, GO_TURN_AROUND_MARKER, UNKNOWN_MARKER};

enum e_soundFX {SFX_PIANO_AHEAD='0', SFX_PIANO_BEHIND='1',SFX_PIANO_LEFT='2',SFX_PIANO_RIGHT='3',
                SFX_ORGAN_AHEAD='4', SFX_ORGAN_BEHIND='5',SFX_ORGAN_LEFT='6',SFX_ORGAN_RIGHT='7',
                SFX_PAD_AHEAD='8', SFX_PAD_BEHIND='9',SFX_PAD_LEFT=':',SFX_PAD_RIGHT=';',SFX_NONE='_'};
                
e_soundFX SFX_AHEAD = SFX_NONE;   // default to no sounds, originally set to SFX_PIANO_AHEAD
e_soundFX SFX_TURN_AROUND = SFX_NONE;
e_soundFX SFX_ANTICLOCKWISE = SFX_NONE;
e_soundFX SFX_CLOCKWISE = SFX_NONE;
e_soundFX SFX_UNKNOWN = SFX_NONE;

uint32_t lastWayMarkerChangeTimestamp = 0;
e_way_marker lastWayMarker = BLACKOUT_MARKER;
e_way_marker newWayMarker = BLACKOUT_MARKER;
char activity_indicator[] = "\\|/-";
uint8_t activity_count = 0;
void refreshDirectionGraphic(float directionOfTravel, float headingToTarget);
uint32_t durationBetweenGuidanceSounds = 50;

void goBlackout();
void goAhead();
void goClockwise();
void goUnknown();
void goAntiClockwise();
void goTurnAround();
void drawGoAhead(const bool show);
void drawGoBlackout(const bool show);
void drawGoClockwise(const bool show);
void drawGoTurnAround(const bool show);
void drawGoUnknown(const bool show);
void drawGoAntiClockwise(const bool show);

void publishToSilkyPlayAudioGuidance(enum e_soundFX sound) {}

// forward declarations
void drawSurveyDisplay();
void drawSurveyDisplaySmooth();
void testSurveyDisplay();
void drawTargetSection();
void drawTargetSectionSprite();
void drawTargetSection_smooth();
void drawCompassSection();
void drawCompassSectionSprite();
void dumpHeapUsage(const char* msg);

// Stubs
bool isGPSStreamOk() { return gps_ok; }
bool isGPSTargetShortTimedOut() { return gpsTargetTimedOut; }
bool isInternetUploadOk() { return internetUploadOk; }
void publishToOceanicPinPlaced(float Lat,float Lng,float magnetic_heading, float depth) {}

std::string getCardinal(float b, bool surveyScreen=false) 
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
void cycleSurveyDepthTempHumidity()
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

void cycleTargetDisplay()
{
  static int step = 0;
  
  float& head = heading_to_target;
  float& dist = distance_to_target;
  int& sats = satellites;
  int& hdop = gps.hdop.hdop();
  int& stat = GPS_status;
  bool& everFix = hasEverReceivedGPSFix;
  uint32_t& rec_tm = recordHighlightExpireTime;

  static bool testGPSStatus = false;
  static bool testPin = true;
  everFix = true;
  GPS_status = 2;

  if (testPin)
  {
    // do a single test of pin highlight - active for 5 seconds
    testPin = false;
    rec_tm = millis() + 5000;
  }
  
  switch (step)
  {
    case 0: hdop=0; dist = 1; GPS_status = 0; everFix = false; break;
    case 1: hdop=1; dist = 3; break;
    case 2: hdop=1; dist = 9; break;
    case 3: hdop=2; dist = 32; break;
    case 4: hdop=2; dist = 14; GPS_status = 1; everFix = false; break;
    case 5: hdop=6; dist = 100.0; break;
    case 6: hdop=6; dist = 22; break;
    case 7: hdop=11; dist = 1000.0; GPS_status = 2; everFix = true; break;
    case 8: hdop=11; dist = 99; break;
    case 9: hdop=0;  dist = 10010.0; break;
    case 10: hdop=1; dist = 8; GPS_status = 1; everFix = true; break;
    case 11: hdop=1; dist = 7; GPS_status = 1; everFix = true; break;
    case 12: hdop=2; dist = 6; GPS_status = 2; everFix = true; break;
    case 13: hdop=2; dist = 5; GPS_status = 2; everFix = true; break;
    case 14: hdop=6; break;
    case 15: hdop=6; break;
    case 16: hdop=11; break;
    case 17: hdop=11; break;
  }

  // if not testing gps status, force a fix
  if (!testGPSStatus)
  {
    everFix = true; GPS_status = 2;
  }

  if (step < 9)
  {
    head = (head >= 316 ? 0 : head + 45);
    satellites+=2;
  }
  else
  {
    head = (head <= 0 ? 315 : head - 45);
    satellites-=2;
  }
    
  step = (step + 1) % 18;
}

// Findings:
//   Enclosure temperature when < 10 degrees layout is unstable. Never been that low.d
//   Depth from 10.0m to 9.0m has trailing m.
void cycleCompassDisplay()
{
  static int step = 0;
  
  float& d = depth;
  float& h = humidity;
  float& t = temperature;
  float& mag = magnetic_heading;

  switch (step)
  {
    case 0: depth = 0.0; t = 3.0; h = 99.0;  break;
    case 1: depth = 1.1; t = 2.0; h = 8.0; break;
    case 2: depth = 2.2; t = 5.0; h = 10.0;break;
    case 3: depth = 10.3; t = 8.0; h = 20.0; break;
    case 4: depth = 11.0; t = 15.5; h = 30.0; break;
    case 5: depth = 22.1; t = 17.0; h = 1.0; break;
    case 6: depth = 33.2; t = 18.0; h = 20.0; break;
    case 7: depth = 44.5; t = 19.0; h = 50.0; break;
    case 8: depth = 31.1; t = 10.0; h = 55; break;
    case 9: depth = 11.1; t = 19.5; h = 51; break;
    case 10: depth =22.2; t = 23.1; h = 80; break;
    case 11: depth =22.3; t = 11.0; h = 90; break;
    case 12: depth =22.1; t = 20.0; h = 100; break;
    case 13: depth =2.9; t = 18.0; h = 80; break;
    case 14: depth =8.7; t = 10.0; h = 90; break;
    case 15: depth =10.1; t = 19.0; h = 82; break;
    case 16: depth =2.1; t = 17.0; h = 81; break;
    case 17: depth =20.1; t = 10.0; h = 0; break;
  }

  if (step < 9)
  {
    mag = (mag <= 0 ? 315 : mag - 45);
  }
  else
  {
    mag = (mag >= 316 ? 0 : mag + 45);
  }
    
  step = (step + 1) % 18;
}



void setup()
{
    M5.begin();
    M5.Lcd.fillScreen(TFT_BLACK);

    Serial.begin(115200);
}

void testSurveyDisplay()
{
  uint32_t wait = 100;
  drawSurveyDisplaySmooth();
  cycleSurveyDepthTempHumidity();
  delay(wait);
  cycleCourse();
  delay(wait);
}

void testNavDisplay()
{
  uint32_t wait = 500;
  drawTargetSection_smooth();
  drawCompassSectionSprite();
  cycleTargetDisplay();
  cycleCompassDisplay();
  delay(wait);
}

void loop()
{
  testNavDisplay();
}

void drawTargetSection()
{
  directionMetric = (display_to_show == NAV_COURSE_DISPLAY ? JOURNEY_COURSE : COMPASS_HEADING);

  // Target Course and distance is shown in Green
  // Journey course and distance over last 10 seconds shown in Red
  // HDOP quality shown in top right corner as square block. Blue best at <=1.
  // Sat count shown underneath HDOP. Red < 4, Orange < 6, Yellow < 10, Blue 10+

  M5.Lcd.setRotation(0);
  M5.Lcd.setTextSize(5);
  M5.Lcd.setCursor(0, 0);

  uint16_t x = 0, y = 0, degree_offset = 0, cardinal_offset = 0, hdop = 0, metre_offset = 0;

  if (GPS_status == GPS_NO_GPS_LIVE_IN_FLOAT && !hasEverReceivedGPSFix)
  {
    M5.Lcd.setCursor(5, 0);
    M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);

    M5.Lcd.print(" NO\n");
    M5.Lcd.setCursor(21, 41);
    M5.Lcd.print("GPS");

    M5.Lcd.setTextSize(2);
  }
  else if (GPS_status == GPS_NO_FIX_FROM_FLOAT && hasEverReceivedGPSFix)
  {
    M5.Lcd.setCursor(5, 0);
    M5.Lcd.setTextColor(TFT_ORANGE, TFT_BLACK);

    M5.Lcd.print(" NO\n");
    M5.Lcd.setCursor(21, 41);
    M5.Lcd.print("FIX");

    M5.Lcd.setTextSize(2);
  }
  else // GPS_FIX_FROM_FLOAT or post-first-fix states - use color coding
  {
    M5.Lcd.setCursor(5, 0);
    
    // Set color based on GPS message timeout
    if (!isGPSStreamOk())
      M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);        // Red after 10 seconds
    else if (isGPSTargetShortTimedOut())
      M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);     // Yellow after 2.5 seconds
    else
      M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);      // Green when GPS is current

    // Display heading to target at top with degrees sign suffix
    M5.Lcd.printf("%3.0f", heading_to_target);
    M5.Lcd.setTextSize(2);
    degree_offset = -2;
    x = M5.Lcd.getCursorX();
    y = M5.Lcd.getCursorY();
    M5.Lcd.setCursor(x, y + degree_offset);
    M5.Lcd.print("o ");

    // Display Cardinal underneath degrees sign
    cardinal_offset = 21;
    M5.Lcd.setCursor(x, y + cardinal_offset);
    M5.Lcd.printf("%s ", getCardinal(heading_to_target).c_str());

    // Display HDOP signal quality as small coloured dot
    hdop = gps.hdop.hdop();
    if (hdop > 10)
      M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
    else if (hdop > 5)
      M5.Lcd.setTextColor(TFT_ORANGE, TFT_BLACK);
    else if (hdop > 1)
      M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    else
      M5.Lcd.setTextColor(TFT_BLUE, TFT_BLACK);

    M5.Lcd.setTextSize(5);
    M5.Lcd.setCursor(x + 10, y - 25);
    M5.Lcd.print(".");

    // Display number of satellites
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(x + 8, y + 40);
    if (satellites < 4.0)
      M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
    else if (satellites < 6.0)
      M5.Lcd.setTextColor(TFT_ORANGE, TFT_BLACK);
    else if (satellites < 10.0)
      M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    else
      M5.Lcd.setTextColor(TFT_BLUE, TFT_BLACK);
      
    M5.Lcd.printf("%2lu", satellites);

    if (recordHighlightExpireTime != 0)
    {
      M5.Lcd.setTextSize(3);
      M5.Lcd.setTextColor(TFT_MAGENTA, TFT_BLACK);
      M5.Lcd.setCursor(x, y);
      M5.Lcd.print("\n");
      M5.Lcd.setTextSize(4);
  
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
    else
    {
      // Display distance to target in metres, with by 'm' suffix
      // Set color based on GPS message timeout
      if (!isGPSStreamOk())
        M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);        // Red after 10 seconds
      else if (isGPSTargetShortTimedOut())
        M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);     // Yellow after 2.5 seconds
      else
        M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);      // Green when GPS is current
      
      uint8_t distanceTextSize = 0;

      M5.Lcd.setCursor(x, y);

      if (distance_to_target >= 1000)
      {
        x = M5.Lcd.getCursorX();
        y = M5.Lcd.getCursorY();

        if (distance_to_target >= 10000)
        {
          distanceTextSize=4;
          M5.Lcd.setTextSize(distanceTextSize);
          M5.Lcd.setCursor(x, y + 15);
          M5.Lcd.printf("\n%4.0f", distance_to_target / 1000.0);
          metre_offset = 14;
        }
        else
        {
          distanceTextSize=5;
          M5.Lcd.setTextSize(distanceTextSize);
          M5.Lcd.println("");
    
          x = M5.Lcd.getCursorX();
          y = M5.Lcd.getCursorY();

          M5.Lcd.setCursor(x+5, y);
          M5.Lcd.printf("%2.1f", distance_to_target / 1000.0);
          metre_offset = 22;
        }

        M5.Lcd.setTextSize(2);

        x = M5.Lcd.getCursorX();
        y = M5.Lcd.getCursorY();

        M5.Lcd.setCursor(x, y + metre_offset);
        M5.Lcd.print("km");
        M5.Lcd.setCursor(x, y);
      }
      else  // < 1000m
      {
        distanceTextSize = 5;
        M5.Lcd.setTextSize(distanceTextSize);
        M5.Lcd.printf("\n%3.0f", distance_to_target);
        M5.Lcd.setTextSize(3);

        x = M5.Lcd.getCursorX();
        y = M5.Lcd.getCursorY();

        metre_offset = 14;
        M5.Lcd.setCursor(x, y + metre_offset);
        M5.Lcd.print("m");
        M5.Lcd.setCursor(x, y);
      }

      M5.Lcd.setTextSize(distanceTextSize);
      M5.Lcd.println("");
    }
  }
}


// RAM size of sprite: 10665 bytes
void drawTargetSectionSprite()
{
  M5.Lcd.setRotation(0);

  int16_t targetSpriteWidth = TFT_WIDTH;
  int16_t targetSpriteHeight = 79;
  int16_t targetSpriteY = 0;
  static TFT_eSprite targetSprite(&M5.Lcd);
  static bool inited = false;

  if (!inited)
  {
    if (!targetSprite.createSprite(targetSpriteWidth,targetSpriteHeight))
    {
      return;
    }
    inited = true;
  }

  // print to main display: depth
  targetSprite.fillSprite(TFT_BLACK);



  directionMetric = (display_to_show == NAV_COURSE_DISPLAY ? JOURNEY_COURSE : COMPASS_HEADING);

  // Target Course and distance is shown in Green
  // Journey course and distance over last 10 seconds shown in Red
  // HDOP quality shown in top right corner as square block. Blue best at <=1.
  // Sat count shown underneath HDOP. Red < 4, Orange < 6, Yellow < 10, Blue 10+

  targetSprite.setTextSize(5);
  targetSprite.setCursor(0, 0);

  uint16_t x = 0, y = 0, degree_offset = 0, cardinal_offset = 0, hdop = 0, metre_offset = 0;

  if (GPS_status == GPS_NO_GPS_LIVE_IN_FLOAT && !hasEverReceivedGPSFix)
  {
    targetSprite.setCursor(5, 0);
    targetSprite.setTextColor(TFT_RED, TFT_BLACK);

    targetSprite.print(" NO\n");
    targetSprite.setCursor(21, 41);
    targetSprite.print("GPS");

    targetSprite.setTextSize(2);
  }
  else if (GPS_status == GPS_NO_FIX_FROM_FLOAT && hasEverReceivedGPSFix)
  {
    targetSprite.setCursor(5, 0);
    targetSprite.setTextColor(TFT_ORANGE, TFT_BLACK);

    targetSprite.print(" NO\n");
    targetSprite.setCursor(21, 41);
    targetSprite.print("FIX");
    targetSprite.setTextSize(2);
  }
  else // GPS_FIX_FROM_FLOAT or post-first-fix states - use color coding
  {
    targetSprite.setCursor(5, 0);
    
    // Set color based on GPS message timeout
    if (!isGPSStreamOk())
      targetSprite.setTextColor(TFT_RED, TFT_BLACK);        // Red after 10 seconds
    else if (isGPSTargetShortTimedOut())
      targetSprite.setTextColor(TFT_YELLOW, TFT_BLACK);     // Yellow after 2.5 seconds
    else
      targetSprite.setTextColor(TFT_GREEN, TFT_BLACK);      // Green when GPS is current
    // Display heading to target at top with degrees sign suffix
    targetSprite.printf("%3.0f", heading_to_target);
    targetSprite.setTextSize(2);
    degree_offset = -2;
    x = targetSprite.getCursorX();
    y = targetSprite.getCursorY();
    targetSprite.setCursor(x, y + degree_offset);
    targetSprite.print("o ");

    // Display Cardinal underneath degrees sign
    cardinal_offset = 21;
    targetSprite.setCursor(x, y + cardinal_offset);
    targetSprite.printf("%s ", getCardinal(heading_to_target).c_str());

    // Display HDOP signal quality as small coloured dot
    hdop = gps.hdop.hdop();
    if (hdop > 10)
      targetSprite.setTextColor(TFT_RED, TFT_BLACK);
    else if (hdop > 5)
      targetSprite.setTextColor(TFT_ORANGE, TFT_BLACK);
    else if (hdop > 1)
      targetSprite.setTextColor(TFT_YELLOW, TFT_BLACK);
    else
      targetSprite.setTextColor(TFT_BLUE, TFT_BLACK);

    targetSprite.setTextSize(5);
    targetSprite.setCursor(x + 10, y - 25);
    targetSprite.print(".");

    // Display number of satellites
    targetSprite.setTextSize(2);
    targetSprite.setCursor(x + 8, y + 40);
    if (satellites < 4.0)
      targetSprite.setTextColor(TFT_RED, TFT_BLACK);
    else if (satellites < 6.0)
      targetSprite.setTextColor(TFT_ORANGE, TFT_BLACK);
    else if (satellites < 10.0)
      targetSprite.setTextColor(TFT_YELLOW, TFT_BLACK);
    else
      targetSprite.setTextColor(TFT_BLUE, TFT_BLACK);
      
    targetSprite.printf("%2lu", satellites);

    if (recordHighlightExpireTime != 0)
    {
      targetSprite.setTextSize(3);
      targetSprite.setTextColor(TFT_MAGENTA, TFT_BLACK);
      targetSprite.setCursor(x, y);
      targetSprite.print("\n");
      targetSprite.setTextSize(4);
  
      if (millis() < recordHighlightExpireTime)
      {
        targetSprite.printf("-PIN-\n");
        if (recordSurveyHighlight)
        { // flag gets reset in telemetry code after being logged first time
          // Also publish is rate-limited internally to max 1 call per 5 seconds
          publishToOceanicPinPlaced(Lat,Lng,magnetic_heading,depth);
        }
      }
      else
      {
        recordHighlightExpireTime = 0;
//        targetSprite.print("     \n");
      }
    }
    else
    {
      // Display distance to target in metres, with by 'm' suffix
      // Set color based on GPS message timeout
      if (!isGPSStreamOk())
        targetSprite.setTextColor(TFT_RED, TFT_BLACK);        // Red after 10 seconds
      else if (isGPSTargetShortTimedOut())
        targetSprite.setTextColor(TFT_YELLOW, TFT_BLACK);     // Yellow after 2.5 seconds
      else
        targetSprite.setTextColor(TFT_GREEN, TFT_BLACK);      // Green when GPS is current
      
      uint8_t distanceTextSize = 0;

      targetSprite.setCursor(x, y);
      if (distance_to_target >= 1000)
      {
        x = targetSprite.getCursorX();
        y = targetSprite.getCursorY();
        if (distance_to_target >= 10000)
        {
          distanceTextSize=4;
          targetSprite.setTextSize(distanceTextSize);
          targetSprite.setCursor(x, y + 15);
          targetSprite.printf("\n%4.0f", distance_to_target / 1000.0);
          metre_offset = 14;
        }
        else
        {
          distanceTextSize=5;
          targetSprite.setTextSize(distanceTextSize);
          targetSprite.println("");
    
          x = targetSprite.getCursorX();
          y = targetSprite.getCursorY();
          targetSprite.setCursor(x+5, y);
          targetSprite.printf("%2.1f", distance_to_target / 1000.0);
          metre_offset = 22;
        }

        targetSprite.setTextSize(2);
        x = targetSprite.getCursorX();
        y = targetSprite.getCursorY();

        targetSprite.setCursor(x, y + metre_offset);
        targetSprite.print("km");
        targetSprite.setCursor(x, y);
      }
      else  // < 1000m
      {
        distanceTextSize = 5;
        targetSprite.setTextSize(distanceTextSize);
        targetSprite.printf("\n%3.0f", distance_to_target);
        targetSprite.setTextSize(3);

        x = targetSprite.getCursorX();
        y = targetSprite.getCursorY();

        metre_offset = 14;
        targetSprite.setCursor(x, y + metre_offset);
        targetSprite.print("m");
        targetSprite.setCursor(x, y);
      }

      targetSprite.setTextSize(distanceTextSize);
      targetSprite.println("");
    }
  }
  targetSprite.pushSprite(0,0);
}

// RAM Usage of compass section sprite is 24,300 bytes
void drawCompassSectionSprite()
{
  // sprite size is TFT_WIDTH x 170 pixels
    M5.Lcd.setRotation(0);

    char label[16];

    int16_t compassSpriteWidth = TFT_WIDTH;
    int16_t compassSpriteHeight = 90;
    int16_t compassSpriteY = 79;
    static TFT_eSprite compassSprite(&M5.Lcd);
    static bool inited = false;

    if (!inited)
    {
      if (!compassSprite.createSprite(compassSpriteWidth,compassSpriteHeight))
      {
        return;
      }
      inited = true;
    }

    // print to main display: depth
    compassSprite.fillSprite(TFT_BLACK);

    float directionOfTravel = magnetic_heading;

    // Display Journey Course with degrees suffix
    // this is the direction travelled in last x seconds
    // Black out the Journey Course if no recent movement
    compassSprite.setFreeFont(&FreeSansBold24pt7b);
    compassSprite.setTextWrap(false);
    compassSprite.setTextSize(1);

    compassSprite.setTextDatum(TR_DATUM);
    compassSprite.setTextColor(TFT_YELLOW, TFT_BLACK);

    // compass:  x=0 y=79
    snprintf(label, sizeof(label), "%3.0f", directionOfTravel);
    compassSprite.drawString(label, 85, 0);

    compassSprite.setTextDatum(TL_DATUM);
    compassSprite.setTextFont(1);

    uint16_t x = 0, y = 0, degree_offset = 0;

    compassSprite.setCursor(92,0);

    x = compassSprite.getCursorX();
    y = compassSprite.getCursorY();

    compassSprite.setTextSize(2);
    compassSprite.setCursor(x, y + degree_offset);

    // Display degrees
    compassSprite.printf("o ");

    // Display Cardinal underneath degrees sign
    const uint16_t cardinal_offset = 21;
    compassSprite.setCursor(x, y + cardinal_offset);
    // cardinal:  x=90 y=100
    compassSprite.printf("%s ", getCardinal(directionOfTravel).c_str());

    // Display temp and humidity
    compassSprite.setCursor(x, y);
    compassSprite.setTextSize(2);
    // temp:  x=90 y=79
    compassSprite.printf("\n\n\n%2.1f", temperature);

    x = compassSprite.getCursorX();
    y = compassSprite.getCursorY();

    const uint16_t temp_degrees_offset = -2;
    compassSprite.setCursor(x + 3, y + temp_degrees_offset);
    compassSprite.setTextSize(0);
    // temp deg:  x=51 y=125
    compassSprite.printf("o ", temperature);
    compassSprite.setTextSize(2);
    // C AND HUMIDITY:  x=63 y=125
    if (temperature < 10.0)
      compassSprite.printf("C   %2.0f%%", humidity);
    else
      compassSprite.printf("C %3.0f%%", humidity);

    // direction graphic:  y=170
    if (GPS_status == GPS_FIX_FROM_FLOAT)
      refreshDirectionGraphic(directionOfTravel, heading_to_target);

    compassSprite.setTextSize(3);
    compassSprite.setTextFont(0);
    compassSprite.setTextColor(TFT_CYAN, TFT_BLACK);

    compassSprite.setTextDatum(TC_DATUM);
    snprintf(label, sizeof(label), "%3.1fm", depth);
    compassSprite.drawString(label, compassSpriteWidth / 2, 67);

    compassSprite.pushSprite(0,compassSpriteY);

    blackout_journey_no_movement = false;
}

void drawCompassSection()
{
    float directionOfTravel = magnetic_heading;

    // Display Journey Course with degrees suffix
    // this is the direction travelled in last x seconds
    // Black out the Journey Course if no recent movement
    M5.Lcd.setTextSize(5);

    M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);

    M5.Lcd.printf("%3.0f", directionOfTravel);

    uint16_t x = 0, y = 0, degree_offset = 0;

    x = M5.Lcd.getCursorX();
    y = M5.Lcd.getCursorY();

    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(x, y + degree_offset);

    // Display degrees
    M5.Lcd.printf("o ");

    // Display Cardinal underneath degrees sign
    const uint16_t cardinal_offset = 21;
    M5.Lcd.setCursor(x, y + cardinal_offset);
    M5.Lcd.printf("%s ", getCardinal(directionOfTravel).c_str());

    // Display temp and humidity
    M5.Lcd.setCursor(x, y);
    M5.Lcd.setTextSize(2);
    M5.Lcd.printf("\n\n\n%2.1f", temperature);

    x = M5.Lcd.getCursorX();
    y = M5.Lcd.getCursorY();

    const uint16_t temp_degrees_offset = -2;
    M5.Lcd.setCursor(x + 3, y + temp_degrees_offset);
    M5.Lcd.setTextSize(0);
    M5.Lcd.printf("o ", temperature);
    M5.Lcd.setTextSize(2);
    M5.Lcd.printf("C %3.0f%%", humidity);

    if (GPS_status == GPS_FIX_FROM_FLOAT)
      refreshDirectionGraphic(directionOfTravel, heading_to_target);

    M5.Lcd.setCursor(40, 146);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextFont(0);
    M5.Lcd.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Lcd.printf("%3.1fm", depth);

    blackout_journey_no_movement = false;
}

void drawCourseSection()
{
    float directionOfTravel = journey_course;

    // Display Journey Course with degrees suffix
    // this is the direction travelled in last x seconds
    // Black out the Journey Course if no recent movement
    M5.Lcd.setTextSize(5);

    M5.Lcd.setTextColor((blackout_journey_no_movement ? TFT_BLACK : TFT_RED), TFT_BLACK);

    M5.Lcd.printf("%3.0f", directionOfTravel);

    uint16_t x = 0, y = 0, degree_offset = 0, cardinal_offset = 0, metre_offset = 0;
    
    x = M5.Lcd.getCursorX();
    y = M5.Lcd.getCursorY();

    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(x, y + degree_offset);

    if (GPS_status == GPS_FIX_FROM_FLOAT)
    {
      // Display small rotating line character to indicate a new journey datapoint has been recorded
      M5.Lcd.printf("o %c", activity_indicator[activity_count]);

      // Display Cardinal underneath degrees sign
      cardinal_offset = 21;
      M5.Lcd.setCursor(x, y + cardinal_offset);
      M5.Lcd.printf("%s ", getCardinal(directionOfTravel).c_str());

      // Display distance travelled during last journey course measurement with 'm' suffix
      M5.Lcd.setCursor(x, y);
      M5.Lcd.setTextSize(5);
      M5.Lcd.printf("\n%3.0f", journey_distance);

      M5.Lcd.setTextSize(3);

      x = M5.Lcd.getCursorX();
      y = M5.Lcd.getCursorY();

      M5.Lcd.setCursor(x, y + metre_offset);
      M5.Lcd.print("m");
      M5.Lcd.setTextSize(5);
      refreshDirectionGraphic(directionOfTravel, heading_to_target);
    }
    else
    {
      // do nothing
    } 
}


void drawTargetSection_smooth()
{  
  M5.Lcd.setRotation(0);

  char label[16];

  int16_t targetSpriteWidth = TFT_WIDTH;
  int16_t targetSpriteHeight = 79;
  int16_t targetSpriteY = 0;
  static TFT_eSprite targetSprite(&M5.Lcd);
  static bool inited = false;

  if (!inited)
  {
    if (!targetSprite.createSprite(targetSpriteWidth,targetSpriteHeight))
    {
      return;
    }
    inited = true;
  }

  // print to main display: depth
  targetSprite.fillSprite(TFT_BLACK);

  directionMetric = (display_to_show == NAV_COURSE_DISPLAY ? JOURNEY_COURSE : COMPASS_HEADING);

  // Target Course and distance is shown in Green
  // Journey course and distance over last 10 seconds shown in Red
  // HDOP quality shown in top right corner as square block. Blue best at <=1.
  // Sat count shown underneath HDOP. Red < 4, Orange < 6, Yellow < 10, Blue 10+

  targetSprite.setTextSize(5);
  targetSprite.setCursor(0, 0);

  uint16_t x = 0, y = 0, degree_offset = 0, cardinal_offset = 0, hdop = 0, metre_offset = 0;

  if (GPS_status == GPS_NO_GPS_LIVE_IN_FLOAT && !hasEverReceivedGPSFix)
  {
    targetSprite.setFreeFont(&FreeSansBold24pt7b);
    targetSprite.setTextWrap(false);
    targetSprite.setTextSize(1);
    targetSprite.setTextDatum(TC_DATUM);
    targetSprite.setTextColor(TFT_RED, TFT_BLACK);

    targetSprite.drawString("NO", targetSprite.width()/2, 0);
    targetSprite.drawString("GPS", targetSprite.width()/2, 42);
  }
  else if (GPS_status == GPS_NO_FIX_FROM_FLOAT && hasEverReceivedGPSFix)
  {
    targetSprite.setFreeFont(&FreeSansBold24pt7b);
    targetSprite.setTextWrap(false);
    targetSprite.setTextSize(1);
    targetSprite.setTextDatum(TC_DATUM);
    targetSprite.setTextColor(TFT_ORANGE, TFT_BLACK);

    targetSprite.drawString("NO", targetSprite.width()/2, 0);
    targetSprite.drawString("FIX", targetSprite.width()/2, 42);
  }
  else // GPS_FIX_FROM_FLOAT or post-first-fix states - use color coding
  {
    targetSprite.setFreeFont(&FreeSansBold24pt7b);
    targetSprite.setTextWrap(false);
    targetSprite.setTextSize(1);

    targetSprite.setTextDatum(TR_DATUM);
    
    // Set color based on GPS message timeout
    if (!isGPSStreamOk())
      targetSprite.setTextColor(TFT_RED, TFT_BLACK);        // Red after 10 seconds
    else if (isGPSTargetShortTimedOut())
      targetSprite.setTextColor(TFT_YELLOW, TFT_BLACK);     // Yellow after 2.5 seconds
    else
      targetSprite.setTextColor(TFT_GREEN, TFT_BLACK);      // Green when GPS is current

    snprintf(label, sizeof(label), "%3.0f", heading_to_target);
    targetSprite.drawString(label, 85, 0);

    targetSprite.setTextDatum(TL_DATUM);
    targetSprite.setTextFont(1);

    uint16_t x = 0, y = 0, degree_offset = 0;

    targetSprite.setCursor(92,0);

    x = targetSprite.getCursorX();
    y = targetSprite.getCursorY();

    targetSprite.setTextSize(2);
    targetSprite.setCursor(x, y);

    // Display degrees
    targetSprite.printf("o ");

    // Display Cardinal underneath degrees sign
    const uint16_t cardinal_offset = 21;
    targetSprite.setCursor(x, y + cardinal_offset);
    // cardinal:  x=90 y=100
    targetSprite.printf("%s ", getCardinal(heading_to_target).c_str());
    
    // Display HDOP signal quality as small coloured dot
    hdop = gps.hdop.hdop();
    if (hdop > 10)
      targetSprite.setTextColor(TFT_RED, TFT_BLACK);
    else if (hdop > 5)
      targetSprite.setTextColor(TFT_ORANGE, TFT_BLACK);
    else if (hdop > 1)
      targetSprite.setTextColor(TFT_YELLOW, TFT_BLACK);
    else
      targetSprite.setTextColor(TFT_BLUE, TFT_BLACK);

    targetSprite.setTextSize(5);
    targetSprite.setCursor(x + 10, y - 25);
    targetSprite.print(".");

    // Display number of satellites
    targetSprite.setTextSize(2);
    targetSprite.setCursor(x + 8, y + 40);
    if (satellites < 4.0)
      targetSprite.setTextColor(TFT_RED, TFT_BLACK);
    else if (satellites < 6.0)
      targetSprite.setTextColor(TFT_ORANGE, TFT_BLACK);
    else if (satellites < 10.0)
      targetSprite.setTextColor(TFT_YELLOW, TFT_BLACK);
    else
      targetSprite.setTextColor(TFT_BLUE, TFT_BLACK);
    
    // PIN overwrites satellite count so hide whilst pin in progress
    if (recordHighlightExpireTime == 0)
      targetSprite.printf("%2lu", satellites);

    if (recordHighlightExpireTime != 0)
    {
      targetSprite.setFreeFont(&FreeSansBold24pt7b);
      targetSprite.setTextDatum(TC_DATUM);
      targetSprite.setTextSize(1);
      targetSprite.setTextColor(TFT_MAGENTA, TFT_BLACK, false);
  
      if (millis() < recordHighlightExpireTime)
      {
        char pinLabel[] = "--PIN--";
        int secondsLeft = (recordHighlightExpireTime - millis()) / 1000;

        if (secondsLeft < 3)
          snprintf(label, sizeof(label), "-- %d --", secondsLeft);
        else
          snprintf(label, sizeof(label), "%s", pinLabel);

        targetSprite.drawString(label,targetSprite.width()/2,40);

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
    else
    {
      // Display distance to target in metres, with by 'm' suffix
      // Set color based on GPS message timeout
      if (!isGPSStreamOk())
        targetSprite.setTextColor(TFT_RED, TFT_BLACK);        // Red after 10 seconds
      else if (isGPSTargetShortTimedOut())
        targetSprite.setTextColor(TFT_YELLOW, TFT_BLACK);     // Yellow after 2.5 seconds
      else
        targetSprite.setTextColor(TFT_GREEN, TFT_BLACK);      // Green when GPS is current
      
      uint8_t distanceTextSize = 0;

      targetSprite.setCursor(x, y);

      targetSprite.setTextSize(1);
      targetSprite.setTextDatum(TR_DATUM);

      char metres[] = "m";
      char kilometres[] = "km";
      char* units = nullptr;

      if (distance_to_target >= 1000)  // draw km distance
      {
        x = targetSprite.getCursorX();
        y = targetSprite.getCursorY();
        if (distance_to_target >= 10000)  // use whole km for >= 10km
        {
          targetSprite.setFreeFont(&FreeSansBold18pt7b);
          snprintf(label, sizeof(label), "%4.0f", distance_to_target / 1000.0);
          targetSprite.drawString(label, 85, 50);
        }
        else    // use 1 decimal place for < 10km
        {
          targetSprite.setFreeFont(&FreeSansBold24pt7b);
          snprintf(label, sizeof(label), "%2.1f", distance_to_target  / 1000.0);
          targetSprite.drawString(label, 85, 40);
        }
        units = kilometres;
      }
      else  // < 1000m - draw metres distance
      {
        targetSprite.setFreeFont(&FreeSansBold24pt7b);
        snprintf(label, sizeof(label), "%3.0f", distance_to_target);
        targetSprite.drawString(label, 85, 40);
        units = metres;
      }

      // Draw distance units
      targetSprite.setTextDatum(TL_DATUM);
      targetSprite.setFreeFont(&FreeSansBold12pt7b);
      targetSprite.drawString(units, 90, 58);
    }
  }
  targetSprite.pushSprite(0,0);
}

void refreshDirectionGraphic( float directionOfTravel,  float headingToTarget)
{
  if (!enableNavigationGraphics)
    return;

  // Calculate whether the diver needs to continue straight ahead,
  // rotate clockwise or rotate anticlockwise and u pdate graphic.
  // Blacks out if no journey recorded.
  const int16_t edgeBound = 25;    // If journey course within +- 25 degrees of target heading then go ahead

  int16_t normaliser = (int16_t)(directionOfTravel);

  int16_t d = (int16_t)directionOfTravel - normaliser;  // directionofTravel normalised to zero
  int16_t t = (int16_t)headingToTarget - normaliser;    // headingToTarget normalised.
  if (t > 180)                  // normalise to range -179 to +180 degrees
    t -= 360;
  else if (t <= -180)
    t += 360;

  int16_t e1 = t - edgeBound;   // left-most edge to target
  if (e1 > 180)                 // normalise to range -179 to +180 degrees
    e1 -= 360;
  else if (e1 <= -180)
    e1 += 360;

  int16_t e2 = t + edgeBound;   // right-most edge to target
  if (e2 > 180)                 // normalise to range -179 to +180 degrees
    e2 -= 360;
  else if (e2 <= -180)
    e2 += 360;

  int16_t o = t + 180;          // opposite heading to target
  if (o > 180)                  // normalise to range -179 to +180 degrees
    o -= 360;
  else if (o <= -180)
    o += 360;


  if (blackout_journey_no_movement)
  {
    goBlackout();
    lastWayMarker = BLACKOUT_MARKER;
  }
  else
  {
    if (millis() - lastWayMarkerChangeTimestamp > durationBetweenGuidanceSounds)
    {
      lastWayMarkerChangeTimestamp = millis();

      if (e1 <= d && d <= e2)     // scenario 1
      {
        newWayMarker = GO_AHEAD_MARKER;

        if (lastWayMarker != newWayMarker)
        {
          goAhead();
          lastWayMarker = newWayMarker;
        }
        publishToSilkyPlayAudioGuidance(SFX_AHEAD);
      }
      
      else if (e1 > e2)           // scenario 4
      {
        newWayMarker = GO_TURN_AROUND_MARKER;

        if (lastWayMarker != newWayMarker)
        {
          goTurnAround();
          lastWayMarker = newWayMarker;
        }
        publishToSilkyPlayAudioGuidance(SFX_TURN_AROUND);
      }
      else if (o <= d && d <= e1) // scenario 2
      {
        newWayMarker = GO_CLOCKWISE_MARKER;

        if (lastWayMarker != newWayMarker)
        {
          goClockwise();
          lastWayMarker = newWayMarker;
        }
        publishToSilkyPlayAudioGuidance(SFX_CLOCKWISE);
      }
      else if (e2 <= d && d <= o) // scenario 3
      {
        newWayMarker = GO_ANTICLOCKWISE_MARKER;

        if (lastWayMarker != newWayMarker)
        {
          goAntiClockwise();
          lastWayMarker = newWayMarker;
        }
        publishToSilkyPlayAudioGuidance(SFX_ANTICLOCKWISE);
      }
      else if (o <= d && d <= e1) // scenario 5
      {
        newWayMarker = GO_CLOCKWISE_MARKER;

        if (lastWayMarker != newWayMarker)
        {
          goClockwise();
          lastWayMarker = newWayMarker;
        }
        publishToSilkyPlayAudioGuidance(SFX_CLOCKWISE);
      }
      else if (e2 <= d && d <= o) // scenario 6
      {
        newWayMarker = GO_ANTICLOCKWISE_MARKER;

        if (lastWayMarker != newWayMarker)
        {
          goAntiClockwise();
          lastWayMarker = newWayMarker;
        }
        publishToSilkyPlayAudioGuidance(SFX_ANTICLOCKWISE);
      }
      else
      {
        newWayMarker = UNKNOWN_MARKER;

        if (lastWayMarker != newWayMarker)
        {
          goUnknown();
          lastWayMarker = newWayMarker;
        }
        publishToSilkyPlayAudioGuidance(SFX_UNKNOWN);
      }
    }
  }
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
    char label[16];
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
        char pinLabel[] = "--PIN--";
        int secondsLeft = (recordHighlightExpireTime - millis()) / 1000;

        if (secondsLeft < 3)
          snprintf(label, sizeof(label), "-- %d --", secondsLeft);
        else
          snprintf(label, sizeof(label), "%s", pinLabel);

        pinSprite.drawString(label,  pinSprite.width() / 2, 0);  // Draw string using specified font number

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


void goBlackout()
{
  drawGoUnknown(false);
  drawGoClockwise(false);
  drawGoAntiClockwise(false);
  drawGoAhead(false);
  drawGoTurnAround(false);
}

void goAhead()
{
  drawGoUnknown(false);
  drawGoClockwise(false);
  drawGoAntiClockwise(false);
  drawGoTurnAround(false);
  drawGoAhead(true);
}

void goTurnAround()
{
  drawGoUnknown(false);
  drawGoClockwise(false);
  drawGoAntiClockwise(false);
  drawGoAhead(false);
  drawGoTurnAround(true);
}

void goClockwise()
{
  drawGoUnknown(false);
  drawGoAhead(false);
  drawGoAntiClockwise(false);
  drawGoTurnAround(false);
  drawGoClockwise(true);
}

void goUnknown()
{
  drawGoAhead(false);
  drawGoTurnAround(false);
  drawGoClockwise(false);
  drawGoAntiClockwise(false);
  drawGoUnknown(true);
}

void goAntiClockwise()
{
  drawGoUnknown(false);
  drawGoAhead(false);
  drawGoTurnAround(false);
  drawGoClockwise(false);
  drawGoAntiClockwise(true);
}

void drawGoAhead(const bool show)
{
  uint32_t colour = (show ? TFT_GREEN : TFT_BLACK);
  const int screenWidth = M5.Lcd.width();
  const int screenHeight = M5.Lcd.height();

  M5.Lcd.fillTriangle(0, screenHeight,
                      screenWidth, screenHeight,
                      screenWidth / 2, screenHeight - 70,
                      colour);

  if (show)
  {
    M5.Lcd.setCursor(40, 220);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_BLACK, TFT_GREEN);
    M5.Lcd.print("Ahead");
  }
}

void drawGoTurnAround(const bool show)
{
  uint32_t colour = (show ? TFT_CYAN : TFT_BLACK);
  const int screenWidth = M5.Lcd.width();
  const int screenHeight = M5.Lcd.height();

  M5.Lcd.fillTriangle(0, screenHeight - 70,
                      screenWidth, screenHeight - 70,
                      screenWidth / 2, screenHeight,
                      colour);

  if (show)
  {
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_BLACK, TFT_CYAN);
    M5.Lcd.setCursor(40, 180);
    M5.Lcd.print("About");
    M5.Lcd.setCursor(45, 200);
    M5.Lcd.print("Turn");
  }
}

void drawGoAntiClockwise(const bool show)
{
  uint32_t colour = (show ? TFT_RED : TFT_BLACK);
  const int screenWidth = M5.Lcd.width();
  const int screenHeight = M5.Lcd.height();

  M5.Lcd.fillTriangle(0, screenHeight,
                      0, screenHeight - 70,
                      screenWidth / 2, screenHeight,
                      colour);

  if (show)
  {
    M5.Lcd.setCursor(5, 220);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
    M5.Lcd.print("Anti");
  }
}

void drawGoClockwise(const bool show)
{
  uint32_t colour = (show ? TFT_BLUE : TFT_BLACK);
  const int screenWidth = M5.Lcd.width();
  const int screenHeight = M5.Lcd.height();

  M5.Lcd.fillTriangle(screenWidth, screenHeight,
                      screenWidth, screenHeight - 70,
                      screenWidth / 2, screenHeight,
                      colour);
  if (show)
  {
    M5.Lcd.setCursor(96, 220);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLUE);
    M5.Lcd.print("Clk");
  }
}

void drawGoUnknown(const bool show)
{
  uint32_t colour = (show ? TFT_MAGENTA : TFT_BLACK);
  const int screenWidth = M5.Lcd.width();
  const int screenHeight = M5.Lcd.height();

  M5.Lcd.setCursor(screenWidth / 2, 190);
  M5.Lcd.setTextSize(5);
  M5.Lcd.setTextColor(colour, TFT_BLACK);
  M5.Lcd.print("!");
}
