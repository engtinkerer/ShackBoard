#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <TFT_eSPI.h>
#include <AnimatedGIF.h>
#include <time.h>

const char* ssid     = "TYAD 2.4G Legacy";
const char* password = "";
const char* callsign = "KD3CMD";

const char* ntpServer = "pool.ntp.org";
const char* imageHost = "www.hamqsl.com";
const char* imagePath = "/solarbc.php";

TFT_eSPI tft = TFT_eSPI(320, 240);  // force _init_width=320, _init_height=240 at runtime
AnimatedGIF gif;

#define MAX_IMAGE_SIZE 30000
uint8_t imageBuffer[MAX_IMAGE_SIZE];
int imageSize = 0;

// Layout zones (rotation(1) on 320x240 panel = 240 wide x 320 tall)
#define CALLSIGN_Y      0
#define CALLSIGN_H      40
#define UTC_Y           40
#define UTC_H           60
#define DATE_Y          100
#define DATE_H          30
#define IMAGE_Y         130
#define IMAGE_H         130
//#define ATTRIBUTION_Y   260
//#define ATTRIBUTION_H   20
#define BUTTON_Y        280   // reserved for future touch buttons



//void drawAttribution() {
    //tft.fillRect(0, ATTRIBUTION_Y, 240, 20, TFT_BLACK);
    //tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    //tft.drawCentreString("http://www.n0nbh.com", 120, ATTRIBUTION_Y + 1, 1);
    //tft.drawCentreString("Copyright Paul L Herrman 2021", 120, ATTRIBUTION_Y + 11, 1);
//}

// GIF draw callback - scales image to fit 240px wide
void gifDraw(GIFDRAW *pDraw) {
    int y = IMAGE_Y + pDraw->y;

    uint16_t lineBuffer[320];
    uint8_t *s = pDraw->pPixels;
    uint16_t *pal = pDraw->pPalette;

    int srcWidth = pDraw->iWidth;
    int dstWidth = 240;

    // Scale horizontally to fit 240px wide
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
    // Yellow background bar
    tft.fillRect(0, CALLSIGN_Y, 240, CALLSIGN_H, TFT_DARKCYAN);
    tft.setTextColor(TFT_RED, TFT_DARKCYAN);
    tft.setTextFont(4);
    // Center the callsign text
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
    //Serial.printf("Time: %s UTC\n", timeStr);

    tft.fillRect(0, UTC_Y, 240, UTC_H, TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString(timeStr, 120, UTC_Y, 6);
    tft.setTextFont(1);
    tft.drawCentreString("UTC", 120, UTC_Y + 52, 1);
}

void setup() {
    Serial.begin(115200);
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
        return;
    }

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
    //drawAttribution();
  }

void loop() {
    static unsigned long lastImageFetch = 0;
    static unsigned long lastTimeTick  = 0;

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
            //drawAttribution();
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