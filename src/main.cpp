#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <TFT_eSPI.h>
#include <AnimatedGIF.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <time.h>
#include <Preferences.h>

// CYD touch is on a separate SPI bus (HSPI), not shared with the display
#define TOUCH_CS   33
#define TOUCH_CLK  25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39

SPIClass touchSPI(HSPI);
XPT2046_Touchscreen ts(TOUCH_CS);
Preferences prefs;

char ssid[33]     = "YOURWIFICREDENTIALS";
char password[65] = "YOURWIFICREDNETIALS";
char callsign[16] = "INPUTYOURCALLSIGN";

// Edit buffers for settings screen (applied only on DONE)
char editCallsign[16];
char editSSID[33];
char editPassword[65];
char* editBufs[]      = { editCallsign, editSSID, editPassword };
const int fieldMaxLen[] = { 15, 32, 64 };

const char* ntpServer = "pool.ntp.org";
const char* imageHost = "www.hamqsl.com";
const char* imagePath = "/solarbc.php";

TFT_eSPI tft = TFT_eSPI(320, 240);  // force _init_width=320, _init_height=240 at runtime
AnimatedGIF gif;

#define MAX_IMAGE_SIZE 30000
uint8_t imageBuffer[MAX_IMAGE_SIZE];
int imageSize = 0;

// --- Screen state ---
enum AppScreen { SCREEN_MAIN, SCREEN_SETTINGS, SCREEN_WIFI_SCAN };
AppScreen currentScreen = SCREEN_MAIN;
int activeField = 0;  // 0=Callsign, 1=SSID, 2=Password

unsigned long lastImageFetch = 0;
bool wifiConnected = false;

// --- Main screen layout zones (rotation(1) = 240 wide x 320 tall) ---
#define CALLSIGN_Y      0
#define CALLSIGN_H      40
#define UTC_Y           40
#define UTC_H           60
#define DATE_Y          100
#define DATE_H          30
#define IMAGE_Y         130
#define IMAGE_H         130
#define BUTTON_Y        280
#define BUTTON_H         40

// --- WiFi scan state ---
#define MAX_NETWORKS  8
#define SCAN_ITEM_H   36
int  networkCount = 0;
char networkSSIDs[MAX_NETWORKS][33];
int  networkRSSI[MAX_NETWORKS];

// --- Settings screen layout ---
#define SET_HEADER_Y    0
#define SET_HEADER_H    30
#define SET_FIELD_Y     30
#define SET_FIELD_H     21
#define KB_TOP          95
#define KB_ROW_H        45
#define KB_KEY_W        24   // 240 / 10

const char* KB_ROWS_UPPER[] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL.",
    "ZXCVBNM-_@"
};
const char* KB_ROWS_LOWER[] = {
    "!@#$%^&*()",
    "qwertyuiop",
    "asdfghjkl.",
    "zxcvbnm-_@"
};
bool capsLock = true;

// GIF draw callback - scales image to fit 240px wide
void gifDraw(GIFDRAW *pDraw) {
    int y = IMAGE_Y + pDraw->y;

    uint16_t lineBuffer[320];
    uint8_t *s = pDraw->pPixels;
    uint16_t *pal = pDraw->pPalette;

    int srcWidth = pDraw->iWidth;
    int dstWidth = 240;

    for (int x = 0; x < dstWidth; x++) {
        int srcX = (x * srcWidth) / dstWidth;
        lineBuffer[x] = pal[s[srcX]];
    }

    tft.pushImage(0, y, dstWidth, 1, lineBuffer);
}

bool fetchImage() {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15);

    if (!client.connect(imageHost, 443)) {
        Serial.println("Connection failed");
        return false;
    }

    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                  imagePath, imageHost);

    // Skip HTTP headers
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }

    // Read chunked data
    imageSize = 0;
    while (client.connected() || client.available()) {
        if (client.available()) {
            String chunkSizeStr = client.readStringUntil('\n');
            chunkSizeStr.trim();
            int chunkSize = strtol(chunkSizeStr.c_str(), NULL, 16);

            if (chunkSize == 0) break;

            int bytesRead = 0;
            while (bytesRead < chunkSize && imageSize < MAX_IMAGE_SIZE) {
                if (client.available()) {
                    imageBuffer[imageSize++] = client.read();
                    bytesRead++;
                }
            }
            client.readStringUntil('\n');
        }
    }

    client.stop();
    Serial.printf("Image bytes received: %d\n", imageSize);
    return imageSize > 0;
}

void drawCallsign() {
    tft.fillRect(0, CALLSIGN_Y, 240, CALLSIGN_H, TFT_DARKCYAN);
    tft.setTextColor(TFT_RED, TFT_DARKCYAN);
    tft.setTextFont(4);
    int textWidth = tft.textWidth(callsign);
    int x = (240 - textWidth) / 2;
    int y = CALLSIGN_Y + (CALLSIGN_H - tft.fontHeight()) / 2;
    tft.setCursor(x, y);
    tft.print(callsign);
}

void drawDate() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;

    char dateStr[20];
    strftime(dateStr, sizeof(dateStr), "%d %b %Y", &timeinfo);

    tft.fillRect(0, DATE_Y, 240, DATE_H, TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextFont(4);
    int textWidth = tft.textWidth(dateStr);
    int x = (240 - textWidth) / 2;
    int y = DATE_Y + (DATE_H - tft.fontHeight()) / 2;
    tft.setCursor(x, y);
    tft.print(dateStr);
}

void displayUTC() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to get time");
        return;
    }

    char timeStr[6];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);

    tft.fillRect(0, UTC_Y, 240, UTC_H, TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString(timeStr, 120, UTC_Y, 6);
    tft.setTextFont(1);
    tft.drawCentreString("UTC", 120, UTC_Y + 52, 1);
}

void drawSettingsButton() {
    tft.fillRect(0, BUTTON_Y, 240, BUTTON_H, TFT_NAVY);
    tft.drawRect(0, BUTTON_Y, 240, BUTTON_H, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.drawCentreString("Settings", 120, BUTTON_Y + 12, 2);
}

// --- Settings screen drawing ---

void drawFieldRow(int i) {
    const char* labels[] = { "CS:", "SSID:", "PW:" };
    int y = SET_FIELD_Y + i * SET_FIELD_H;
    bool active = (i == activeField);
    uint16_t bg = active ? TFT_DARKGREY : TFT_BLACK;
    tft.fillRect(0, y, 240, SET_FIELD_H, bg);
    tft.drawRect(0, y, 240, SET_FIELD_H, TFT_WHITE);
    tft.setTextFont(1);
    tft.setTextColor(TFT_YELLOW, bg);
    tft.setCursor(3, y + 7);
    tft.print(labels[i]);
    tft.print(" ");
    tft.setTextColor(active ? TFT_GREEN : TFT_WHITE, bg);
    if (i == 2) {  // Password — mask with asterisks
        int len = strlen(editBufs[i]);
        for (int c = 0; c < len; c++) tft.print('*');
    } else {
        tft.print(editBufs[i]);
    }
    if (active) tft.print("|");
}

void drawFieldRows() {
    for (int i = 0; i < 3; i++) drawFieldRow(i);
}

void drawKeyboard() {
    const char** rows = capsLock ? KB_ROWS_UPPER : KB_ROWS_LOWER;
    for (int row = 0; row < 4; row++) {
        int y = KB_TOP + row * KB_ROW_H;
        const char* keys = rows[row];
        for (int col = 0; col < 10; col++) {
            int x = col * KB_KEY_W;
            tft.fillRect(x, y, KB_KEY_W - 1, KB_ROW_H - 1, TFT_NAVY);
            tft.drawRect(x, y, KB_KEY_W - 1, KB_ROW_H - 1, TFT_WHITE);
            tft.setTextColor(TFT_WHITE, TFT_NAVY);
            char buf[2] = { keys[col], 0 };
            tft.drawCentreString(buf, x + KB_KEY_W / 2, y + 15, 2);
        }
    }
    // Bottom row: CAPS(60) | SPACE(60) | BKSP(60) | DONE(60)
    int y = KB_TOP + 4 * KB_ROW_H;
    const char* bottomLabels[] = { "CAPS", "SPACE", "BKSP", "DONE" };
    for (int i = 0; i < 4; i++) {
        int x = i * 60;
        uint16_t bg = (i == 0 && capsLock) ? TFT_DARKGREEN : TFT_NAVY;
        tft.fillRect(x, y, 59, KB_ROW_H - 1, bg);
        tft.drawRect(x, y, 59, KB_ROW_H - 1, TFT_WHITE);
        tft.setTextColor(TFT_WHITE, bg);
        tft.drawCentreString(bottomLabels[i], x + 30, y + 15, 2);
    }
}

void drawSettingsScreen() {
    tft.fillScreen(TFT_BLACK);

    // Header
    tft.fillRect(0, SET_HEADER_Y, 240, SET_HEADER_H, TFT_DARKGREY);
    tft.fillRect(0, SET_HEADER_Y, 50, SET_HEADER_H, TFT_NAVY);
    tft.drawRect(0, SET_HEADER_Y, 50, SET_HEADER_H, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.drawCentreString("< Back", 25, SET_HEADER_Y + 9, 1);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.drawCentreString("Settings", 145, SET_HEADER_Y + 9, 2);

    drawFieldRows();
    drawKeyboard();
}

// --- WiFi scan screen ---

void doWifiScan() {
    // Show scanning message while scan runs
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, 240, 30, TFT_DARKGREY);
    tft.fillRect(0, 0, 50, 30, TFT_NAVY);
    tft.drawRect(0, 0, 50, 30, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.drawCentreString("< Back", 25, 9, 1);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.drawCentreString("Select Network", 145, 9, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("Scanning...", 120, 160, 2);

    int n = WiFi.scanNetworks();
    networkCount = 0;
    for (int i = 0; i < n && networkCount < MAX_NETWORKS; i++) {
        if (WiFi.channel(i) <= 13) {  // 2.4GHz only
            strncpy(networkSSIDs[networkCount], WiFi.SSID(i).c_str(), 32);
            networkSSIDs[networkCount][32] = '\0';
            networkRSSI[networkCount] = WiFi.RSSI(i);
            networkCount++;
        }
    }
    WiFi.scanDelete();
    Serial.printf("Scan found %d networks\n", networkCount);
}

void drawScanScreen() {
    tft.fillScreen(TFT_BLACK);

    // Header
    tft.fillRect(0, 0, 240, 30, TFT_DARKGREY);
    tft.fillRect(0, 0, 50, 30, TFT_NAVY);
    tft.drawRect(0, 0, 50, 30, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.drawCentreString("< Back", 25, 9, 1);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.drawCentreString("Select Network", 145, 9, 2);

    if (networkCount == 0) {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawCentreString("No networks found", 120, 160, 2);
        return;
    }

    for (int i = 0; i < networkCount; i++) {
        int y = 30 + i * SCAN_ITEM_H;
        tft.fillRect(0, y, 240, SCAN_ITEM_H - 1, TFT_BLACK);
        tft.drawRect(0, y, 240, SCAN_ITEM_H - 1, TFT_DARKGREY);

        // SSID name
        tft.setTextFont(2);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(4, y + 10);
        tft.print(networkSSIDs[i]);

        // Signal bars (4 bars, right-aligned)
        int rssi = networkRSSI[i];
        int bars = (rssi >= -60) ? 4 : (rssi >= -70) ? 3 : (rssi >= -80) ? 2 : 1;
        for (int b = 0; b < 4; b++) {
            int bx = 216 + b * 5;
            int bh = (b + 1) * 6;
            int by = y + SCAN_ITEM_H - 5 - bh;
            tft.fillRect(bx, by, 4, bh, (b < bars) ? TFT_GREEN : TFT_DARKGREY);
        }
    }
}

void handleScanTouch(int sx, int sy) {
    // Back — return to settings without changing SSID
    if (sy < 30 && sx < 50) {
        currentScreen = SCREEN_SETTINGS;
        drawSettingsScreen();
        return;
    }

    // Network row tapped
    if (sy >= 30) {
        int idx = (sy - 30) / SCAN_ITEM_H;
        if (idx >= 0 && idx < networkCount) {
            strncpy(editSSID, networkSSIDs[idx], sizeof(editSSID) - 1);
            editSSID[sizeof(editSSID) - 1] = '\0';
            Serial.printf("Network selected: \"%s\"\n", editSSID);
            currentScreen = SCREEN_SETTINGS;
            drawSettingsScreen();
        }
    }
}

void handleSettingsTouch(int sx, int sy) {
    // Back button
    if (sy < SET_HEADER_H && sx < 50) {
        Serial.println("Settings: Back pressed");
        currentScreen = SCREEN_MAIN;
        lastImageFetch = 0;  // force immediate GIF reload
        tft.fillScreen(TFT_BLACK);
        drawCallsign();
        displayUTC();
        drawDate();
        drawSettingsButton();
        return;
    }

    // Field row selection
    if (sy >= SET_FIELD_Y && sy < KB_TOP) {
        int field = (sy - SET_FIELD_Y) / SET_FIELD_H;
        if (field >= 0 && field < 3) {
            if (field == 1) {  // SSID — launch WiFi scan picker
                activeField = 1;
                currentScreen = SCREEN_WIFI_SCAN;
                doWifiScan();
                drawScanScreen();
            } else if (field != activeField) {
                activeField = field;
                drawFieldRows();
                Serial.printf("Settings: Field selected = %d\n", activeField);
            }
        }
        return;
    }

    // Keyboard rows 0–3
    if (sy >= KB_TOP && sy < KB_TOP + 4 * KB_ROW_H) {
        int row = (sy - KB_TOP) / KB_ROW_H;
        int col = constrain(sx / KB_KEY_W, 0, 9);
        char key = (capsLock ? KB_ROWS_UPPER : KB_ROWS_LOWER)[row][col];
        int len = strlen(editBufs[activeField]);
        if (len < fieldMaxLen[activeField]) {
            editBufs[activeField][len]     = key;
            editBufs[activeField][len + 1] = '\0';
            drawFieldRow(activeField);
        }
        Serial.printf("Settings: '%c' -> field %d: \"%s\"\n", key, activeField, editBufs[activeField]);
        return;
    }

    // Bottom row: CAPS(0-59) | SPACE(60-119) | BKSP(120-179) | DONE(180-239)
    if (sy >= KB_TOP + 4 * KB_ROW_H) {
        int btn = constrain(sx / 60, 0, 3);
        if (btn == 0) {  // CAPS
            capsLock = !capsLock;
            drawKeyboard();
            Serial.printf("Settings: CAPS -> %s\n", capsLock ? "UPPER" : "lower");
        } else if (btn == 1) {  // SPACE
            int len = strlen(editBufs[activeField]);
            if (len < fieldMaxLen[activeField]) {
                editBufs[activeField][len]     = ' ';
                editBufs[activeField][len + 1] = '\0';
                drawFieldRow(activeField);
            }
            Serial.printf("Settings: SPACE -> field %d: \"%s\"\n", activeField, editBufs[activeField]);
        } else if (btn == 2) {  // BKSP
            int len = strlen(editBufs[activeField]);
            if (len > 0) {
                editBufs[activeField][len - 1] = '\0';
                drawFieldRow(activeField);
            }
            Serial.printf("Settings: BKSP -> field %d: \"%s\"\n", activeField, editBufs[activeField]);
        } else {  // DONE — apply edits and save to NVS
            strcpy(callsign, editCallsign);
            strcpy(ssid,     editSSID);
            strcpy(password, editPassword);
            prefs.begin("shackboard", false);  // read-write
            prefs.putString("ssid",     ssid);
            prefs.putString("password", password);
            prefs.putString("callsign", callsign);
            prefs.end();
            Serial.printf("Settings saved: CS=\"%s\" SSID=\"%s\"\n", callsign, ssid);
            if (!wifiConnected) {
                // Reboot so setup() reconnects with new credentials
                Serial.println("Rebooting to apply WiFi settings...");
                delay(200);
                ESP.restart();
            }
            currentScreen  = SCREEN_MAIN;
            lastImageFetch = 0;
            tft.fillScreen(TFT_BLACK);
            drawCallsign();
            displayUTC();
            drawDate();
            drawSettingsButton();
        }
        return;
    }
}

void setup() {
    Serial.begin(115200);

    // Load saved settings from NVS (falls back to compiled defaults if not set)
    prefs.begin("shackboard", true);  // read-only
    prefs.getString("ssid",     ssid,     sizeof(ssid));
    prefs.getString("password", password, sizeof(password));
    prefs.getString("callsign", callsign, sizeof(callsign));
    prefs.end();
    Serial.printf("Loaded: CS=\"%s\" SSID=\"%s\"\n", callsign, ssid);

    touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    ts.begin(touchSPI);
    ts.setRotation(1);

    tft.init();
    tft.setSwapBytes(true);
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    // Backlight on
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);

    // Draw callsign bar immediately
    drawCallsign();

    // Show connecting message in UTC zone
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextFont(2);
    tft.setCursor(10, UTC_Y + 10);
    tft.print("Connecting WiFi...");

    WiFi.begin(ssid, password);
    Serial.print("Connecting WiFi");

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi failed!");
        tft.setCursor(10, UTC_Y + 30);
        tft.print("WiFi Failed!");
        drawSettingsButton();
        return;
    }

    wifiConnected = true;
    Serial.println("WiFi connected!");

    tft.fillRect(0, UTC_Y, 240, UTC_H, TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextFont(2);
    tft.setCursor(10, UTC_Y + 10);
    tft.print("Syncing time...");

    configTime(0, 0, ntpServer);
    struct tm timeinfo;
    int ntpAttempts = 0;
    while (!getLocalTime(&timeinfo) && ntpAttempts < 20) {
        delay(500);
        ntpAttempts++;
    }

    Serial.println("Time synced!");
    tft.fillScreen(TFT_BLACK);
    drawCallsign();
    displayUTC();
    drawDate();
    drawSettingsButton();
}

void loop() {
    static unsigned long lastTimeTick  = 0;

    if (currentScreen == SCREEN_MAIN) {
        // Refresh image every 5 minutes
        if (millis() - lastImageFetch > 300000 || lastImageFetch == 0) {
            Serial.println("Fetching image...");
            if (fetchImage()) {
                Serial.println("Decoding GIF...");
                tft.fillRect(0, IMAGE_Y, 240, IMAGE_H, TFT_BLACK);
                if (gif.open(imageBuffer, imageSize, gifDraw)) {
                    gif.playFrame(true, NULL);
                    gif.close();
                    Serial.println("GIF displayed!");
                } else {
                    Serial.println("GIF open failed");
                }
            }
            lastImageFetch = millis();
        }

        // Update time every second
        if (millis() - lastTimeTick > 1000) {
            displayUTC();
            drawDate();
            lastTimeTick = millis();
        }
    }

    // Touch detection — fire once per press (edge trigger)
    // Calibration measured on this CYD: top-left=(3751,434), bottom-right=(320,3615)
    static bool wasTouched = false;
    bool touched = ts.touched();
    if (touched && !wasTouched) {
        TS_Point p = ts.getPoint();
        int sx = constrain(map(p.y,  434, 3615, 0, 239), 0, 239);
        int sy = constrain(map(p.x, 3751,  320, 0, 319), 0, 319);
        Serial.printf("Touch screen: x=%d y=%d\n", sx, sy);

        if (currentScreen == SCREEN_MAIN) {
            if (sy >= BUTTON_Y) {
                Serial.println("Settings button pressed");
                currentScreen = SCREEN_SETTINGS;
                activeField = 0;
                strcpy(editCallsign, callsign);
                strcpy(editSSID,     ssid);
                strcpy(editPassword, password);
                drawSettingsScreen();
            }
        } else if (currentScreen == SCREEN_SETTINGS) {
            handleSettingsTouch(sx, sy);
        } else if (currentScreen == SCREEN_WIFI_SCAN) {
            handleScanTouch(sx, sy);
        }
    }
    wasTouched = touched;
}
