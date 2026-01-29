#include <U8g2lib.h>
#include <AiEsp32RotaryEncoder.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <FirebaseESP32.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

// LED(+)-->5 , SDA-->21 , SCL-->22 , vcc-->Vin , clk-->18 , dt-->19 , sw-->23
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

byte current_menu = 0; // 0=hours , 1=minutes , 2=seconds
byte hours1 = 0; // First digit of hour
byte hours2 = 0; // Second digit of hour
byte minutes1 = 0; // First digit of minute
byte minutes2 = 0; // Second digit of minute
byte seconds1 = 0; // First digit of second
byte seconds2 = 0; // Second digit of second

byte first_digit = 0; // first digit in each selection
byte second_digit = 0; // second digit in each selection
byte current_pos = 0;

bool text = true; // determine whether text showing

// Conditions for menu in timer
bool timer_menu = true; // true for timer false for menu
bool pause_selected = false; // true if pause selected false play selected
const int outline_y[] = {0, 22, 46}; // y position of outline

// rotary encoder parameters
#define clk 18
#define dt 19
#define sw 23

// Led output
#define p_out 5

// Reset variables
#define RESET_BUTTON_PIN 27
bool RESET_FLAG = false;

//Plug ID
#define plugID "plug002"

// Firebase details
#define FIREBASE_HOST "smarttimerplug-default-rtdb.firebaseio.com"
#define FIREBASE_API_KEY ""

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Captive portal variables
String firebaseEmail;
String firebasePassword;
String plugName;
String userID;

// Preferences
Preferences preferences;

// Connection variables
bool wifiConnected = false;
bool firebaseInitialized = false;

unsigned long lastFirebaseCheck = 0;
const unsigned long firebaseCheckInterval = 2000;
unsigned long lastSeenCheck = 0;
const unsigned long lastSeenInterval = 5000;

int CONNECTION_PIN = 26;

// ESP32 mode
bool onlineMode = false;
bool modeSelected = false;
bool onlineButOffline = false;
bool wifiInitialized = false;

// States
enum UIState {
    SET_TIME,
    START_CONFIRM,
    COUNTDOWN_ACTIVE,
    MENU_ACTIVE,
    RESET
};

enum TimeState {
    SET_HOURS,
    SET_MINUTES,
    SET_SECONDS
};

TimeState current_time = SET_HOURS;

UIState current_state;

// Firebase Parameters
bool isDeleted = false;
bool isOn = false;
bool isPaused = false;
String lastUpdatedBy;
long durationSeconds;
long lastSeen;
long startTime;

// Countdown parameters
struct CountdownTimer {
    unsigned long lastTick = 0;
    unsigned long pauseTime = 0;
    int remainingSeconds = 0;
    bool countdownRunning = false;
    bool countdownPaused = false;

    void startCountdown(int totalSeconds) {
        remainingSeconds = totalSeconds;
        countdownRunning = true;
        countdownPaused = false;
        lastTick = millis();
        digitalWrite(p_out, HIGH);
    }

    void tick() {
        if (countdownPaused || !countdownRunning) return;

        if (millis() - lastTick >= 1000) {
            remainingSeconds --;
            lastTick += 1000;

            Serial.print("Time left: ");
            Serial.println(remainingSeconds);

            if (remainingSeconds <= 0) {
                countdownRunning = false;
                digitalWrite(p_out, LOW);
            }
        }
    }

    int pause() {
        countdownPaused = true;
        digitalWrite(p_out, LOW);
        pauseTime = millis();
        Serial.print("Paused at: ");
        Serial.println(remainingSeconds);
        return remainingSeconds;
    }

    int resume() {
        unsigned long pausedDuration = millis() - pauseTime;
        lastTick += pausedDuration;
        countdownPaused = false;
        digitalWrite(p_out, HIGH);
        Serial.print("Resumed at: ");
        Serial.println(remainingSeconds);
        return remainingSeconds;
    }

    void stop() {
        countdownRunning = false;
        digitalWrite(p_out, LOW);
    }

};

CountdownTimer countdown;

// Other parameters (Original)
int counter = 0;
int lastEncoderValue;
byte sw_state = 0;

AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(clk, dt, sw);

void IRAM_ATTR readEncoderISR() {
  rotaryEncoder.readEncoder_ISR();
}

unsigned long totalSeconds;

// parameters for switch handling
bool lastButtonState = false;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;  // ms

// Non-blocking text display timing
unsigned long textStartTime = 0;
const unsigned long textDisplayDuration = 1000; // 1 seconds

void loadSavedSettings() {
  preferences.begin("myplug", false);
  firebaseEmail = preferences.getString("email", "");
  firebasePassword = preferences.getString("fpass", "");
  plugName = preferences.getString("pname", "");
  preferences.end();
}

void saveSettings() {
  preferences.begin("myplug", false);
  preferences.putString("email", firebaseEmail);
  preferences.putString("fpass", firebasePassword);
  preferences.putString("pname", plugName);
  preferences.end();
}



void drawWiFiIcon() {
  const int baseX = 128 - 9; // leave room for 4 bars (each 2px wide)
  const int baseY = 0;

  // Bar widths and spacing
  const int barWidth = 1;
  const int barSpacing = 2;

  // Heights of signal bars
  u8g2.drawBox(baseX + 0 * barSpacing, baseY + 7, barWidth, 2); // Shortest bar
  u8g2.drawBox(baseX + 1 * barSpacing, baseY + 5, barWidth, 4);
  u8g2.drawBox(baseX + 2 * barSpacing, baseY + 3, barWidth, 6);
  u8g2.drawBox(baseX + 3 * barSpacing, baseY + 1, barWidth, 8); // Tallest bar
}

void drawFirebaseIcon() {
  const int signalX = 128 - 9;  // signal icon starts here
  const int signalWidth = 8;   // ~8px wide
  const int padding = 2;

  const int fX = signalX - 6 - padding;  // room for letter 'F' (6px wide)
  const int fY = 1;                      // small margin from top

  u8g2.setFont(u8g2_font_6x10_tr);       // small readable font
  u8g2.drawStr(fX, fY + 8, "F");         // draw 'F' aligned with signal bars
}

void drawSelectMode(byte selectedItem) {
    const char* menuItems[3] = {"Run Online", "Run Offline", "Factory Reset"};

    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_7x14_tr);
        for (int i = 0; i < 3; i++) {
            int yPos = 16 + i * 22;
            int xPos = 23;

            u8g2.drawStr(xPos, yPos, menuItems[i]);

            if (i == selectedItem) {
                int textWidth = u8g2.getStrWidth(menuItems[i]);
                int rectX = xPos - 4;
                int rectY = yPos - u8g2.getAscent();
                int rectW = textWidth + 8;
                int rectH = u8g2.getAscent() - u8g2.getDescent();

                u8g2.drawFrame(rectX, rectY, rectW, rectH);
            }
        }
    } while (u8g2.nextPage());
}

void drawCaptivePortal() {
    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_7x14_tr);
        u8g2.drawStr(40, 20, "Started");

        u8g2.setFont(u8g2_font_7x14_tr);
        u8g2.drawStr(15, 45, "Captive Portal");
    } while (u8g2.nextPage());
}

void drawConnecting() {
    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_7x14_tr);
        u8g2.drawStr(0, 40, "Connecting........");
    } while (u8g2.nextPage());
}

void drawTimeDigits(byte h1, byte h2, byte m1, byte m2, byte s1, byte s2) {
  char timeStr[9];
  snprintf(timeStr, sizeof(timeStr), "%d%d:%d%d:%d%d", h1, h2, m1, m2, s1, s2);

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_fub20_tr);
    int baselineY = 40;
    int xStart = 10;

    u8g2.drawStr(xStart, baselineY, timeStr);

    // Get widths for precise rectangle positions
    int digitWidth = u8g2.getStrWidth("0"); // width per digit approx
    int colonWidth = u8g2.getStrWidth(":");

    // Positions of each digit pair group
    int posHours = xStart;
    int posMinutes = posHours + 2 * digitWidth + colonWidth;
    int posSeconds = posMinutes + 2 * digitWidth + colonWidth;

    // Vertical rectangle parameters
    int rectHeight = u8g2.getAscent() - u8g2.getDescent() + 6; // +6 for padding
    int rectY = baselineY - u8g2.getAscent() - 3; // rectangle top slightly above baseline

    if (current_state == SET_TIME) {
        // Draw outline around selected digit
        int rectX = 0;
        if (current_time == SET_HOURS) {
        rectX = current_pos == 0 ? posHours : posHours + digitWidth - 1;
        } else if (current_time == SET_MINUTES) {
        rectX = current_pos == 0 ? posMinutes - 4 : posMinutes + digitWidth - 4;
        } else if (current_time == SET_SECONDS) {
        rectX = current_pos == 0 ? posSeconds - 9 : posSeconds + digitWidth - 9;
        }
        rectX -= 2;
        u8g2.drawFrame(rectX, rectY, digitWidth + 2, rectHeight);
    }

    // H M S labels
    u8g2.setFont(u8g2_font_6x10_tf);
    int labelBaselineY = rectY + rectHeight + u8g2.getFontAscent() + 5;

    int hLabelWidth = u8g2.getStrWidth("H");
    int mLabelWidth = u8g2.getStrWidth("M");
    int sLabelWidth = u8g2.getStrWidth("S");

    int hLabelX = posHours + digitWidth - (hLabelWidth / 2);
    int mLabelX = posMinutes + digitWidth - (mLabelWidth / 2) - 5;
    int sLabelX = posSeconds + digitWidth - (sLabelWidth / 2) - 10;

    // Draw the labels
    u8g2.drawStr(hLabelX, labelBaselineY, "H");
    u8g2.drawStr(mLabelX, labelBaselineY, "M");
    u8g2.drawStr(sLabelX, labelBaselineY, "S");

    if (wifiConnected) {
        drawWiFiIcon();
        if (firebaseInitialized) {
            drawFirebaseIcon();
        }
    }

  } while (u8g2.nextPage());
}

void drawCountdownComplete() {
    u8g2.firstPage();
  	do {
        u8g2.setFont(u8g2_font_fub20_tr); // Large bold font (approx 20px tall)
  		u8g2.drawStr(0, 25, "Countdown");   

  		u8g2.setFont(u8g2_font_fub20_tr); // Use same large font
  		u8g2.drawStr(0, 55, "Complete!"); 
        if (wifiConnected) {
            drawWiFiIcon();
            if (firebaseInitialized) {
                drawFirebaseIcon();
            }
        }
    }while (u8g2.nextPage());
}

String basePathBuilder() {
    String basePath = "/users/";
    basePath += userID;
    basePath += "/plugs/";
    basePath += plugID;
    return basePath;
}

void startCaptivePortal() {
    WiFiManager wm;

  //Custom parameters
  WiFiManagerParameter custom_email("email", "Firebase Email", "", 40);
  WiFiManagerParameter custom_pass("pass", "Firebase Password", "", 40);
  WiFiManagerParameter custom_name("name", "Plug Name", "", 20);

  wm.addParameter(&custom_email);
  wm.addParameter(&custom_pass);
  wm.addParameter(&custom_name);
  //wm.setConfigPortalBlocking(false);

  wm.setTitle("Smart Plug Config");

  if (!wm.autoConnect("SmartPlug_Config")) {
    Serial.println("Failed to connect.");
    delay(3000);
    ESP.restart();
  }

  //Saving values
  firebaseEmail = custom_email.getValue();
  firebasePassword = custom_pass.getValue();
  plugName = custom_name.getValue();

  saveSettings();

  Serial.println("WiFi connected.");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  wifiInitialized = true;
  Serial.print("Email ");
  Serial.println(firebaseEmail);
  Serial.print("Plug Name: ");
  Serial.println(plugName);
}

void connectToWifi() {
    Serial.println("Attempting to connect to WiFi...");
    //WiFiManager wm;
    //wm.autoConnect();
    int attempts = 0;
    const int MAX_CONNECT_ATTEMPTS = 20;

    WiFi.begin();
    drawConnecting();
    while (WiFi.status() != WL_CONNECTED && attempts < MAX_CONNECT_ATTEMPTS) {
        delay(500);
        Serial.print(".");
        attempts ++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConnected through method.");
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        wifiInitialized = true;
    } else {
        Serial.println("Couldn't connect through method.");
    }
}

void resetConnectionDetails() {
    WiFi.disconnect(true, true);
    WiFiManager wm;
    wm.resetSettings();
    delay(1000);
    Serial.println("WiFi credentials erased.");
    ESP.restart();
}

void initFirebase() {
  config.database_url = FIREBASE_HOST;
  config.api_key = FIREBASE_API_KEY;
  loadSavedSettings();
  auth.user.email = firebaseEmail;
  auth.user.password = firebasePassword;
  Serial.println(firebaseEmail);
  Serial.println(firebasePassword);

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  unsigned long timeout = millis();
  while (!auth.token.uid.length() && millis() - timeout < 10000) {
    delay(100);
  }

  if (auth.token.uid.length()) {
    userID = auth.token.uid.c_str();
    Serial.print("UID: ");
    Serial.println(userID);
  } else {
    Serial.println("Failed to get user UID.");
  }
}

void wifiAndFirebase(bool onlineMode) {
    if (onlineMode) {
        if(!wifiConnected) {
            WiFi.mode(WIFI_STA);
            connectToWifi();
            delay(100);
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("\nSuccessfully connected to WiFi.");
                wifiConnected = true;
            } else {
                if (!wifiInitialized) {
                    String ssid = WiFi.SSID();
                    Serial.print("SSID: ");
                    Serial.println(ssid);
                    bool hasCredentials = ssid.length();
                    Serial.println("Failed to connect.");
                    onlineButOffline = true;
                    if (hasCredentials) {
                        Serial.println("Failed to connect with stored credentials.");
                    } else {
                        Serial.println("No credentials found. Opening captive portal.");
                        drawCaptivePortal();
                        startCaptivePortal();
                    }
                } else {
                    onlineButOffline = true;
                }
            }
        } else {
            if (!firebaseInitialized) {
                initFirebase();
                if (Firebase.ready()) {
                    firebaseInitialized = true;
                } else {
                    return;
                }
            } else {
                //applyFirebaseControl();
            }
        }
    } else {
        return;
    }
}

void formatSecondsAndDraw(int totalSeconds) {
    int hoursTemp = totalSeconds / 3600;
    int minutesTemp = (totalSeconds % 3600) / 60;
    int secondsTemp = totalSeconds % 60;
    
    byte h1 = hoursTemp / 10;
    byte h2 = hoursTemp % 10;
    byte m1 = minutesTemp / 10;
    byte m2 = minutesTemp % 10;
    byte s1 = secondsTemp / 10;
    byte s2 = secondsTemp % 10;

    drawTimeDigits(h1, h2, m1, m2, s1, s2);
}

void onCountdownComplete() {
    drawCountdownComplete();
    current_state = RESET;
}

void modeSelector() {
    if (counter > 2) counter = 0;
        else if (counter < 0) counter = 2;
        if (sw_state == 1) {
            switch (counter) {
                case 0:
                    onlineMode = true;
                    modeSelected = true;
                    Serial.println("Online mode selected.");
                    return;
                case 1:
                    onlineMode = false;
                    modeSelected = true;
                    Serial.println("Offline mode selected.");
                    return;
                case 2:
                    RESET_FLAG = true;
            }
            sw_state = 0;
            counter = 0;
        }
        drawSelectMode(counter);
}

void displayCountdownScreen() {
    formatSecondsAndDraw(countdown.remainingSeconds);
    if (sw_state == 1) {
        current_state = MENU_ACTIVE;
        sw_state = 0;
    }
}

void displayTimeSetter() {

    if(current_time == SET_HOURS) {
        if (current_pos == 0) {
            if (sw_state == 1) {
                current_pos = 1;
                hours1 = first_digit;
                counter = 0;
            }
            if (counter > 2) counter = 0;
            else if (counter < 0) counter = 2;
            first_digit = counter;
            second_digit = hours2;
        } else {
            if (sw_state == 1) {
                current_time = SET_MINUTES;
                hours2 = second_digit;
                counter = 0;
                current_pos = 0;
            }
            if (hours1 == 2) {
                if (counter > 3) counter = 0;
                else if (counter < 0) counter = 3;
            } else {
                if (counter > 9) counter = 0;
                else if (counter < 0) counter = 9;
            }
            second_digit = counter;
            first_digit = hours1;
        }
        drawTimeDigits(first_digit, second_digit, minutes1, minutes2, seconds1, seconds2);
    } else if (current_time == SET_MINUTES) {
        if (current_pos == 0) {
        if (sw_state == 1) {
          current_pos = 1;
          minutes1 = first_digit;
          counter = 0;
        }
        if (counter > 5) counter = 0;
        else if (counter < 0) counter = 5;
        first_digit = counter;
        second_digit = minutes2;
      } else {
        if (sw_state == 1) {
          current_time = SET_SECONDS;
          minutes2 = second_digit;
          counter = 0;
          current_pos = 0;
          text = true;
        }
        if (counter > 9) counter = 0;
        else if (counter < 0) counter = 9;
        second_digit = counter;
        first_digit = minutes1;
      }
      drawTimeDigits(hours1, hours2, first_digit, second_digit, seconds1, seconds2);
    
    } else if(current_time == SET_SECONDS) {
        if (current_pos == 0) {
            if (sw_state == 1) {
                current_pos = 1;
                seconds1 = first_digit;
                counter = 0;
            }
            if (counter > 5) counter = 0;
            else if (counter < 0) counter = 5;
            first_digit = counter;
            second_digit = seconds2;
        } else {
            if (sw_state == 1) {
                current_state = START_CONFIRM;
                seconds2 = second_digit;
                counter = 0;
                current_pos = 0;
                text = true;

                // Calculate total seconds
                totalSeconds = (hours1 * 10 + hours2) * 3600UL + (minutes1 * 10 + minutes2) * 60UL + (seconds1 * 10 + seconds2);
                return;
            }
            if (counter > 9) counter = 0;
            else if (counter < 0) counter = 9;
            second_digit = counter;
            first_digit = seconds1;
        }
      drawTimeDigits(hours1, hours2, minutes1, minutes2, first_digit, second_digit);
    }
}

void displayStartConfirm() {
    if (sw_state == 0) {
        u8g2.firstPage();
  		do {
    		u8g2.setFont(u8g2_font_fub20_tr); // Large bold font (approx 20px tall)
  			u8g2.drawStr(25, 25, "Start");   // Draw "Start" at (x=30, y=25)

  			u8g2.setFont(u8g2_font_fub20_tr); // Use same large font
  			u8g2.drawStr(25, 55, "Timer"); // Draw "Timer" at (x=10, y=55)
  		}while (u8g2.nextPage());
    } else if (sw_state == 1) {
        sw_state = 0;
        current_state = COUNTDOWN_ACTIVE;
        countdown.startCountdown(totalSeconds);
        if (onlineMode) {
            String basePath = basePathBuilder();
            FirebaseJson json;

            json.set("durationSeconds", totalSeconds);
            json.set("isPaused", false);
            json.set("isOn", true);
            json.set("lastUpdatedBy", "esp");
            time_t now = time(nullptr);
            do {
                now = time(nullptr);
                delay(100);
            } while (now < 100000);
            json.set("startTime", time(nullptr));
            
            if (Firebase.updateNode(fbdo, basePath, json)) {
                Serial.println("Batch update successful.");
            } else {
                Serial.println("Update failed.");
                Serial.println(fbdo.errorReason());
            }
        }
    }
}

void displayTimerMenu(byte selectedItem) {
  const char* menuItems[3] = {"Return", "Reset", isPaused ? "Play" : "Pause"};
  
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_7x14_tr);
    for (int i = 0; i < 3; i++) {
      int yPos = 16 + i * 22; // y positions with some padding
      int xPos = 23;

      u8g2.drawStr(xPos, yPos, menuItems[i]);

      if (i == selectedItem) {
        int textWidth = u8g2.getStrWidth(menuItems[i]);
        int rectX = xPos - 4;  // padding left
        int rectY = yPos - u8g2.getAscent(); // top of text line
        int rectW = textWidth + 8;  // padding both sides
        int rectH = u8g2.getAscent() - u8g2.getDescent(); // height of font
        
        u8g2.drawFrame(rectX, rectY, rectW, rectH);
      }
    }
    if (wifiConnected) {
        drawWiFiIcon();
        if (firebaseInitialized) {
            drawFirebaseIcon();
        }
    }
  } while (u8g2.nextPage());
}

void timerMenu() {
    if (counter > 2) counter = 0;
    else if (counter < 0) counter = 2;
    byte selectedItem = counter;

    displayTimerMenu(selectedItem);
    if (sw_state == 1) {
        if (selectedItem == 0) {
            current_state = COUNTDOWN_ACTIVE;
            sw_state = 0;
        } else if (selectedItem == 1) {
            current_state = RESET;
            sw_state = 0;
        } else if (selectedItem == 2) {
            if (isPaused) {
                countdown.countdownPaused = false;
                isPaused = false;
                int remainingSeconds = countdown.resume();
                sw_state = 0;
                if (onlineMode) {
                    String basePath = basePathBuilder();
                    FirebaseJson json;

                    json.set("durationSeconds", remainingSeconds);
                    json.set("isPaused", false);
                    json.set("isOn", true);
                    json.set("lastUpdatedBy", "esp");
                    //time_t now = time(nullptr);
                    //do {
                    //    now = time(nullptr);
                    //    delay(100);
                    //} while (now < 100000);
                    //json.set("startTime", now);
                    if (Firebase.updateNode(fbdo, basePath, json)) {
                        Serial.println("Batch update successful.");
                    } else {
                        Serial.println("Update failed.");
                        Serial.println(fbdo.errorReason());
                    }
                }
                
            } else {
                countdown.countdownPaused = true;
                isPaused = true;
                int remainingSeconds = countdown.pause();
                sw_state = 0;
                if (onlineMode) {
                    String basePath = basePathBuilder();
                    FirebaseJson json;

                    json.set("durationSeconds", remainingSeconds);
                    json.set("isPaused", true);
                    json.set("isOn", true);
                    json.set("lastUpdatedBy", "esp");
                    // time_t now = time(nullptr);
                    // do {
                    //     now = time(nullptr);
                    //     delay(100);
                    // } while (now < 100000);
                    // json.set("startTime", now);
                    if (Firebase.updateNode(fbdo, basePath, json)) {
                        Serial.println("Batch update successful.");
                    } else {
                        Serial.println("Update failed.");
                        Serial.println(fbdo.errorReason());
                    }

                }
                
            }
        }
    }
}

void lastSeenUpdater() {
    String basePath = basePathBuilder();
    String path = basePath; path += "/lastSeen";
    time_t now = time(nullptr);
    do {
        now = time(nullptr);
        delay(100);
    } while (now < 100000);
    Firebase.setInt(fbdo, path, now);
}

void resetTimer(){
	current_state = SET_TIME;
    current_time = SET_HOURS;
	counter=0;
	current_pos=0;
	isPaused=false;
    sw_state=0;
    hours1 = 0;
    hours2 = 0;
    minutes1 = 0;
    minutes2 = 0;
    seconds1 = 0;
    seconds2 = 0;
    totalSeconds = 0;
    digitalWrite(p_out,LOW);
    if (onlineMode) {
        String basePath = basePathBuilder();
        FirebaseJson json;

        json.set("durationSeconds", totalSeconds);
        json.set("isPaused", false);
        json.set("isOn", false);
        json.set("lastUpdatedBy", "esp");
        // time_t now = time(nullptr);
        // do {
        //     now = time(nullptr);
        //     delay(100);
        // } while (now < 100000);
        // json.set("startTime", now);
        if (Firebase.updateNode(fbdo, basePath, json)) {
            Serial.println("Batch update successful.");
        } else {
            Serial.println("Update failed.");
            Serial.println(fbdo.errorReason());
        }
        
    }
}



void createInitialPlugEntry() {
    String basePath = basePathBuilder();
    FirebaseJson json;

    json.set("durationSeconds", 0);
    json.set("isPaused", false);
    json.set("isOn", false);
    json.set("lastUpdatedBy", "esp");
    json.set("isDeleted", false);
    json.set("name", plugName);
    time_t now = time(nullptr);
    do {
        now = time(nullptr);
        delay(100);
    } while (now < 100000);
    json.set("startTime", now);
    json.set("lastSeen", now);
    if (Firebase.updateNode(fbdo, basePath, json)) {
        Serial.println("New plug entry created.");
    } else {
        Serial.println("Update failed.");
        Serial.println(fbdo.errorReason());
    }
    preferences.putBool("isInitialized", true);
}

void firebaseListener(bool onlineMode) {
    if (onlineMode) {
        if (firebaseInitialized) {
            String basePath = basePathBuilder();

            if (Firebase.getJSON(fbdo, basePath)) {
                FirebaseJson json = fbdo.jsonObject();
                FirebaseJsonData data;

                json.get(data, "isDeleted");
                isDeleted = data.to<bool>();
                if (isDeleted) {
                    String path = basePath; path += "/isDeleted";
                    Firebase.setBool(fbdo, path, false);
                    resetConnectionDetails();
                }
                String path = basePath; path += "/name";
                Firebase.setString(fbdo, path, plugName);
                json.get(data, "lastUpdatedBy");
                lastUpdatedBy = data.to<String>();
                json.get(data, "isOn");
                isOn = data.to<bool>();
                if (lastUpdatedBy == "app") {
                    json.get(data, "seenByEsp");
                    bool seenByEsp = data.to<bool>();
                    if (!seenByEsp) {
                        if (isOn) {
                            current_state = COUNTDOWN_ACTIVE;
                            json.get(data, "isPaused");
                            isPaused = data.to<bool>();
                            json.get(data, "durationSeconds");
                            durationSeconds = data.to<int>();
                            if (isPaused) {
                                countdown.pause();
                            } else {
                                json.get(data, "startTime");
                                startTime = data.to<int>();
                                countdown.startCountdown(durationSeconds);
                            }
                        } else {
                            current_state = RESET;
                        }
                        String path = basePath; path += "/seenByEsp";
                        Firebase.setBool(fbdo, path, true);
                    }
                }
            } else {
                createInitialPlugEntry();
            }
        }
    }    
}

void setup() {
    Serial.begin(115200);

    pinMode(p_out, OUTPUT);
    digitalWrite(p_out, LOW);
    pinMode(CONNECTION_PIN, OUTPUT);
    digitalWrite(CONNECTION_PIN, LOW);

    pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

    u8g2.setColorIndex(1);
    u8g2.begin();
    u8g2.setBitmapMode(1);

    rotaryEncoder.begin();
    rotaryEncoder.setup(readEncoderISR);
    rotaryEncoder.setBoundaries(-1000, 1000, false);
    rotaryEncoder.setAcceleration(0);

    preferences.begin("myplug", true);
    Serial.println("Debug Load:");
    Serial.println(preferences.getString("email", "[empty]"));
    Serial.println(preferences.getString("fpass", "[empty]"));
    Serial.println(preferences.getString("pname", "[empty]"));
    preferences.end();

}

void loop() {
    static unsigned long lastEncoderCheck = 0;
    static const unsigned long encoderDebounceDelay = 100;

    int rawValue = rotaryEncoder.readEncoder();
    int currentEncoderValue = rawValue / 4;
    unsigned long currentMillis = millis();

    

    // Reset check
    //if (digitalRead(RESET_BUTTON_PIN) == LOW || isDeleted == true) {
    if (RESET_FLAG || isDeleted == true) {
        resetConnectionDetails();
    }

    if (currentMillis - lastEncoderCheck >= encoderDebounceDelay) {
        if (currentEncoderValue > lastEncoderValue) {
        counter--;
        lastEncoderValue = currentEncoderValue;
        } else if (currentEncoderValue < lastEncoderValue) {
        counter++;
        lastEncoderValue = currentEncoderValue;
        }
        lastEncoderCheck = currentMillis;
    }

    bool currentButtonState = rotaryEncoder.isEncoderButtonDown();

    if (currentButtonState && !lastButtonState && (currentMillis - lastDebounceTime > debounceDelay)) {
        sw_state = 1;
        lastDebounceTime = currentMillis;
    } else {
        sw_state = 0;
    }

    lastButtonState = currentButtonState;

    // Mode menu
    if (!modeSelected) {  
        modeSelector();
        
    } else {  
        if (!onlineMode) {
            switch (current_state) {
                case SET_TIME:
                    displayTimeSetter();
                    break;
                case START_CONFIRM:
                    displayStartConfirm();
                    break;
                case COUNTDOWN_ACTIVE:
                    countdown.tick();
                    displayCountdownScreen();
                    if (!countdown.countdownRunning) {
                        onCountdownComplete();
                    }
                    break;
                case MENU_ACTIVE:
                    timerMenu();
                    break;
                case RESET:
                    resetTimer();
                    break;
            }
        } else {
            wifiAndFirebase(onlineMode);
            // WiFi check
            if (WiFi.status() != WL_CONNECTED){
                wifiConnected = false;
                onlineMode = false;
            }
            if (!Firebase.ready()) {
                firebaseInitialized = false;
            }
            if (millis() - lastFirebaseCheck >= firebaseCheckInterval) {
                firebaseListener(onlineMode);
                lastFirebaseCheck = millis();
            }
            if (millis() - lastSeenCheck > lastSeenInterval) {
                lastSeenUpdater();
                lastSeenCheck = millis();
            }
            switch (current_state) {
                case SET_TIME:
                    displayTimeSetter();
                    break;
                case START_CONFIRM:
                    displayStartConfirm();
                    break;
                case COUNTDOWN_ACTIVE:
                    countdown.tick();
                    displayCountdownScreen();
                    if (!countdown.countdownRunning) {
                        onCountdownComplete();
                    }
                    break;
                case MENU_ACTIVE:
                    timerMenu();
                    break;
                case RESET:
                    resetTimer();
                    break;
            }
        }    
    }
}