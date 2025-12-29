#include <config.h>
#include <color_tools.h>
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <WiFi.h>
#include <SpotifyEsp32.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <time.h>
#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <Adafruit_NeoPixel.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <map>
#include <algorithm>
#include <cmath>

#define countof(x) (sizeof(x) / sizeof(x[0]))

// variables
MatrixPanel_I2S_DMA *display;
String currentAlbumArtUrl = "";
String previousAlbumArtUrl = " ";
uint16_t leastPredominantColor = 0;
uint16_t mostPredominantColor = 0;
bool isSpotifyPlaying = false;
bool spotifyInitialized = false;
bool spotifyAuthenticated = false;
const char *weekDays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

// objects
Spotify sp(CLIENT_ID, CLIENT_SECRET, REFRESH_TOKEN);
JPEGDEC jpeg;
Adafruit_NeoPixel pixels(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Custom allocator for PSRAM
template <typename T>
struct PSRAMAllocator
{
    typedef T value_type;

    PSRAMAllocator() = default;
    template <class U>
    constexpr PSRAMAllocator(const PSRAMAllocator<U> &) noexcept {}

    T *allocate(std::size_t n)
    {
        if (n > std::size_t(-1) / sizeof(T))
            throw std::bad_alloc();
        if (auto p = static_cast<T *>(heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM)))
            return p;
        throw std::bad_alloc();
    }

    void deallocate(T *p, std::size_t) noexcept { heap_caps_free(p); }
};

template <class T, class U>
bool operator==(const PSRAMAllocator<T> &, const PSRAMAllocator<U> &) { return true; }
template <class T, class U>
bool operator!=(const PSRAMAllocator<T> &, const PSRAMAllocator<U> &) { return false; }

std::map<uint16_t, int, std::less<uint16_t>, PSRAMAllocator<std::pair<const uint16_t, int>>> colorCounts;

uint16_t extractMostVibrantColor(const std::map<uint16_t, int, std::less<uint16_t>, PSRAMAllocator<std::pair<const uint16_t, int>>> &colorCounts)
{
    uint16_t mostVibrantColor = 0;
    float maxVibrancy = -1.0f;

    for (std::map<uint16_t, int, std::less<uint16_t>, PSRAMAllocator<std::pair<const uint16_t, int>>>::const_iterator it = colorCounts.begin(); it != colorCounts.end(); ++it)
    {
        uint16_t color = it->first;
        float vibrancy = calculateVibrancy(color);

        if (vibrancy > maxVibrancy)
        {
            maxVibrancy = vibrancy;
            mostVibrantColor = color;
        }
    }

    return mostVibrantColor;
}

int downloadImage(String imageUrl)
{
    Serial.println("Downloading image... " + imageUrl);
    HTTPClient http;

    File f = LittleFS.open("/cover.jpg", "w");

    if (!f)
    {
        Serial.println("Error opening file");
        return -1;
    }

    http.begin(imageUrl);

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK)
    {
        Serial.println("[HTTP] GET... failed, error: " + http.errorToString(httpCode) + " : " + String(httpCode));
        Serial.println("Response: " + http.getString());
        f.close();
        http.end();
        return -1;
    }

    int fileCode = http.writeToStream(&f);

    if (fileCode < 0)
    {
        Serial.println(F("Error writing to file"));
        f.close();
        http.end();
        return -1;
    }

    Serial.println("File Downloaded");

    f.close();
    http.end();
    return 0;
}

int drawMCU(JPEGDRAW *pDraw)
{

    uint16_t *pPixel = (uint16_t *)pDraw->pPixels;
    for (int y = 0; y < pDraw->iHeight; y++)
    {
        for (int x = 0; x < pDraw->iWidth; x++)
        {
            display->drawPixel(x + pDraw->x, y + pDraw->y, pPixel[y * pDraw->iWidth + x]);

            // Count the occurrences of each color
            colorCounts[pPixel[y * pDraw->iWidth + x]]++;
        }
    }

    return 1; // Continue decoding
}

void drawJPEG(const char *filename, int xpos, int ypos)
{

    colorCounts.clear();

    File file = LittleFS.open(filename, "r");
    if (!file)
    {
        Serial.println("Failed to open file for reading");
        return;
    }

    int fileSize = file.size();
    if (fileSize <= 0)
    {
        Serial.println("Empty image file");
        file.close();
        return;
    }

    uint8_t *buffer = (uint8_t *)malloc((size_t)fileSize);
    if (!buffer)
    {
        Serial.println("Not enough memory to load image");
        file.close();
        return;
    }

    size_t readBytes = file.read(buffer, fileSize);
    if ((int)readBytes != fileSize)
    {
        Serial.println("Warning: read fewer bytes than file size");
    }
    file.close();

    if (jpeg.openRAM(buffer, fileSize, drawMCU))
    {
        jpeg.decode(xpos, ypos, 0); // 0 = full size
        jpeg.close();
    }

    free(buffer);

    Serial.println("Color counts:" + String(colorCounts.size()));

    // Find the least and most predominant colors
    std::pair<decltype(colorCounts)::iterator, decltype(colorCounts)::iterator> minmax;

    minmax = std::minmax_element(
        colorCounts.begin(), colorCounts.end(),
        [](const decltype(colorCounts)::value_type &a, const decltype(colorCounts)::value_type &b)
        {
            return a.second < b.second;
        });

    mostPredominantColor = minmax.second->first;
    leastPredominantColor = minmax.first->first;

    uint8_t r, g, b;

    display->color565to888(mostPredominantColor, r, g, b);

    pixels.setPixelColor(0, pixels.Color(r, g, b));
    pixels.show();

    // if mostPredominantColor is similar to leastPredominantColor the second least predominat font

    if (areColorsSimilar(mostPredominantColor, leastPredominantColor, COLOR_SIMILARITY_THRESHOLD))
    {
        Serial.println("Most predominant color is similar to least predominant color");

        uint16_t vibrantColor = extractMostVibrantColor(colorCounts);

        leastPredominantColor = vibrantColor;

        if (areColorsSimilar(mostPredominantColor, leastPredominantColor, COLOR_SIMILARITY_THRESHOLD))
        {
            Serial.println("Invert color");

            leastPredominantColor = invertColor(mostPredominantColor);
        }
    }
}

void drawWeekDay(int day, int hour)
{

    // Only show between configured hours
    if (hour >= SHOW_HOUR_START && hour < SHOW_HOUR_END)
    {
        // Get the color based on the time
        uint16_t baseColor = getClockDigitColor(hour, 0);

        // Extract RGB components
        uint8_t r = (baseColor >> 11) & 0x1F;
        uint8_t g = (baseColor >> 5) & 0x3F;
        uint8_t b = baseColor & 0x1F;

        // Adjust brightness (reduce to 1/2)
        r = r / 2;
        g = g / 2;
        b = b / 2;

        // Combine back into 16-bit color
        uint16_t adjustedColor = (r << 11) | (g << 5) | b;

        // Increase text size to fill the screen
        display->setFont(&FreeSansBold12pt7b);
        display->setTextSize(1);
        display->setTextWrap(false);

        int xOffset = 3;
        int yOffset = 60;

        // Draw the day

        // Draw the text with an outline
        for (int8_t x = -1; x <= 1; x++)
        {
            for (int8_t y = -1; y <= 1; y++)
            {
                if (x == 0 && y == 0)
                    continue; // Skip the center
                display->setCursor(xOffset + x, yOffset + y);

                display->setTextColor(0);

                display->printf(weekDays[day]);
            }
        }

        display->setTextColor(adjustedColor);
        display->setCursor(xOffset, yOffset);
        display->print(weekDays[day]);
    }
}

void drawMonthDay(int day, int hour)
{
    // Only show between configured hours
    if (hour >= SHOW_HOUR_START && hour < SHOW_HOUR_END)
    {

        // Get the color based on the time
        uint16_t baseColor = getClockDigitColor(hour, 0);

        // Extract RGB components
        uint8_t r = (baseColor >> 11) & 0x1F;
        uint8_t g = (baseColor >> 5) & 0x3F;
        uint8_t b = baseColor & 0x1F;

        // Adjust brightness (reduce to 1/4)
        r = r / 4;
        g = g / 4;
        b = b / 4;

        // Combine back into 16-bit color
        uint16_t adjustedColor = (r << 11) | (g << 5) | b;

        // Increase text size to fill the screen
        display->setFont(&FreeSansBold18pt7b);
        display->setTextSize(2);
        display->setTextWrap(false);

        // Format the day as a two-digit string
        char dayText[3];
        snprintf(dayText, sizeof(dayText), "%02d", day);

        // Calculate the size of the text
        int16_t x1, y1;
        uint16_t w, h;
        display->getTextBounds(dayText, 0, 0, &x1, &y1, &w, &h);

        // Calculate position to center the text
        int yOffset = 25 + h / 2; // 32 is the center of a 64-pixel high display

        // Draw the main text
        display->setTextColor(adjustedColor);

        if (day < 10)
        {
            display->setCursor(13, yOffset);
            display->print(dayText[1]);
        }
        else
        {
            display->setCursor(0, yOffset);
            display->print(dayText[0]);

            for (int8_t x = -1; x <= 1; x++)
            {
                for (int8_t y = -1; y <= 1; y++)
                {
                    if (x == 0 && y == 0)
                        continue; // Skip the center
                    display->setCursor(26 + x, yOffset + y);

                    display->setTextColor(0);

                    display->print(dayText[1]);
                }
            }
            display->setTextColor(adjustedColor);

            display->setCursor(26, yOffset);
            display->print(dayText[1]);
        }
    }
}

void drawClock(String clockText, uint16_t bodyColor, uint16_t counterColor, bool center)
{
    display->setFont(&FreeSans12pt7b);

    int yOffset = center ? 40 : 30;
    int xOffset = 3;

    display->setTextSize(1);
    display->setTextWrap(false);

    // Draw the text with an outline
    for (int8_t x = -1; x <= 1; x++)
    {
        for (int8_t y = -1; y <= 1; y++)
        {
            if (x == 0 && y == 0)
                continue; // Skip the center
            display->setCursor(xOffset + x, yOffset + y);

            display->setTextColor(counterColor);

            display->printf(clockText.c_str());
        }
    }

    display->setCursor(xOffset, yOffset);
    display->setTextColor(bodyColor);
    display->printf(clockText.c_str());
}

bool hasInternetConnectivity()
{
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(3000);

    if (!http.begin("http://clients3.google.com/generate_204"))
    {
        Serial.println(F("Connectivity check begin failed"));
        return false;
    }

    int code = http.GET();
    http.end();
    bool ok = code == 204;
    if (ok)
    {
        Serial.println(F("Internet reachable"));
    }
    else
    {
        Serial.println("No internet, code: " + String(code));
    }
    return ok;
}

void ensureSpotifyReady()
{
    if (spotifyAuthenticated)
        return;

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println(F("WiFi lost, cannot init Spotify"));
        spotifyInitialized = false;
        return;
    }

    if (!hasInternetConnectivity())
    {
        Serial.println(F("No internet, deferring Spotify auth"));
        return;
    }

    if (!spotifyInitialized)
    {
        Serial.print(F("Spotify begin: "));
        sp.begin();
        spotifyInitialized = true;
        Serial.println(F("started"));
    }

    if (!sp.is_auth())
    {
        Serial.print(F("Authenticating Spotify (timeout 10s): "));
        unsigned long start = millis();
        while (!sp.is_auth() && millis() - start < 10000)
        {
            sp.handle_client();
            delay(10);
        }
        if (sp.is_auth())
        {
            spotifyAuthenticated = true;
            Serial.printf("Authenticated! Refresh token: %s\n", sp.get_user_tokens().refresh_token);
        }
        else
        {
            Serial.println(F("Auth not completed, will retry later"));
        }
    }
    else
    {
        spotifyAuthenticated = true;
        Serial.println(F("Spotify already authenticated"));
    }
}

void setup()
{
    // Initialize USBSerial port
    Serial.begin(115200);
    Serial.println("\nStart!");

    // Start led matrix
    Serial.println(F("Led Matrix begin"));
    HUB75_I2S_CFG mxconfig(
        64,
        64,
        1,
        MATRIX_PINS);

    mxconfig.driver = HUB75_I2S_CFG::ICN2038S;
    mxconfig.clkphase = false;
    mxconfig.latch_blanking = 4;
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_10M;
    mxconfig.double_buff = true;

    // Display Setup
    display = new MatrixPanel_I2S_DMA(mxconfig);
    display->begin();
    display->setBrightness8(DISPLAY_BRIGHTNESS);
    display->clearScreen();
    display->flipDMABuffer();

    // Initialize LittleFS
    Serial.print(F("LittleFS begin: "));
    if (!LittleFS.begin(true))
    {
        Serial.println(F("failed"));
    }
    else
    {
        Serial.println(F("ok"));
    }

    // Initialize Wifi
    Serial.print(F("WiFi begin: "));

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println(F("\nWiFi failed! Restarting..."));
        // ESP.restart();
    }
    else
    {

        Serial.println((String) "RSSI : " + WiFi.RSSI() + " dB");
        Serial.println("\nIP:" + WiFi.localIP().toString());

        // Set primary DNS server
        IPAddress primaryDNS(8, 8, 8, 8); // Google's DNS server

        // Set secondary DNS server (optional)
        IPAddress secondaryDNS(8, 8, 4, 4); // Google's secondary DNS server

        // Apply DNS settings
        if (WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), primaryDNS, secondaryDNS))
        {
            Serial.println("DNS Server configuration successful");
        }
        else
        {
            Serial.println("DNS Server configuration failed");
        }

        // Print the DNS server to verify
        Serial.print("DNS Server: ");
        Serial.println(WiFi.dnsIP());
    }

    // Initialize mDNS
    Serial.print(F("mDNS begin: "));
    if (!MDNS.begin(PROJECTNAME))
    {
        Serial.println(F("failed"));
        // ESP.restart();
    }
    else
    {
        // Set the hostname to "$PROJECTNAME.local"
        Serial.println(F("ok"));
    }

    // Initialize NTP
    setenv("TZ", TZ_STRING, 1);                                                              // Set timezone
    configTime(NTP_GMT_OFFSET_SECONDS, NTP_DAYLIGHT_OFFSET_SECONDS, ntpServer1, ntpServer2); // Init and get the time

    struct tm timeinfo;

    if (!getLocalTime(&timeinfo))
    {
        Serial.println(F("Failed to obtain time"));
        return;
    }
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

    // Initialize Spotify (lazy, internet-checked)
    ensureSpotifyReady(); // Will defer if no internet

    pinMode(PIN_LED, OUTPUT);
    pinMode(PIN_LIGHT_SENSOR, INPUT);

    if (!getLocalTime(&timeinfo))
    {
        Serial.println(F("Failed to obtain time"));
    }

    char datestring[6];
    snprintf_P(datestring,
               countof(datestring),
               PSTR("%02u:%02u"),
               timeinfo.tm_hour,
               timeinfo.tm_min);

    drawMonthDay(timeinfo.tm_mday, timeinfo.tm_hour);
    drawWeekDay(timeinfo.tm_wday, timeinfo.tm_hour);
    drawClock(datestring, getClockDigitColor(timeinfo.tm_hour, timeinfo.tm_min), 0, false);

    display->flipDMABuffer();

    pixels.begin(); // Initialize NeoPixel strip
    pixels.setBrightness(NEOPIXEL_BRIGHTNESS);
}

void loop()
{
    // Reconnect if wifi is down
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Reconnecting to WiFi...");
        WiFi.disconnect();
        WiFi.reconnect();
    }

    ensureSpotifyReady();

    if (!spotifyAuthenticated)
    {
        Serial.println(F("Spotify not ready, showing clock only"));

        display->clearScreen();

        struct tm timeinfo;
        if (!getLocalTime(&timeinfo))
        {
            Serial.println(F("Failed to obtain time"));
        }

        char datestring[6];
        snprintf_P(datestring,
                   countof(datestring),
                   PSTR("%02u:%02u"),
                   timeinfo.tm_hour,
                   timeinfo.tm_min);

        drawMonthDay(timeinfo.tm_mday, timeinfo.tm_hour);
        drawWeekDay(timeinfo.tm_wday, timeinfo.tm_hour);
        drawClock(datestring, getClockDigitColor(timeinfo.tm_hour, timeinfo.tm_min), 0, timeinfo.tm_hour <= SHOW_HOUR_START || timeinfo.tm_hour >= SHOW_HOUR_END);
        display->flipDMABuffer();
        delay(2000);
        return;
    }

    // Get the current uptime
    Serial.println("Uptime in minutes: " + String((millis() / 60000)));

    display->clearScreen();

    Serial.println(F("Checking Spotify state"));

    response currentState = sp.currently_playing();

    /*
    State
      200 Information about playback
      204 Playback not available or active
      401 Bad or expired token. This can happen if the user revoked a token or the access token has expired. You should re-authenticate the user.
      403 Bad OAuth request (wrong consumer key, bad nonce, expired timestamp...). Unfortunately, re-authenticating the user won't help here.
      429 The app has exceeded its rate limits.
    */

    if (currentState.status_code != 200)
    {
        Serial.println("Error, code: " + String(currentState.status_code));

        if (currentState.status_code == 201)
        {
            Serial.println(F("Spotify on inactive"));

            isSpotifyPlaying = false;
        }

        if (currentState.status_code == 401)
        {
            Serial.println(F("The access token expired"));

            sp.get_access_token();
            currentState = sp.currently_playing();
        }

        if (currentState.status_code == 403)
        {
            Serial.println(F("Bad OAuth request"));
        }

        if (currentState.status_code == 429)
        {
            Serial.println(F("The app has exceeded its rate limits."));
        }

        if (currentState.reply["message"].as<String>().equals("Timeout receiving headers"))
        {
            Serial.println(F("Timeout receiving headers"));
            currentState = sp.currently_playing();
        }
    }

    // check if is play is null
    if (!currentState.reply["is_playing"].isNull())
    {
        isSpotifyPlaying = currentState.reply["is_playing"].as<bool>();
    }

    if (isSpotifyPlaying)
    {
        Serial.println(F("Spotify is playing"));
        currentAlbumArtUrl = currentState.reply["item"]["album"]["images"][2]["url"].as<String>();

        if (!currentState.reply["item"]["album"]["images"][2]["url"].isNull())
        {

            if (!currentAlbumArtUrl.equals(previousAlbumArtUrl))
            {
                previousAlbumArtUrl = currentAlbumArtUrl;
                int downloadResult = downloadImage(currentAlbumArtUrl);

                Serial.println("Download result: " + String(downloadResult));
            }
        }

        drawJPEG("/cover.jpg", 0, 0);

        struct tm timeinfo;

        if (!getLocalTime(&timeinfo))
        {
            Serial.println(F("Failed to obtain time"));
        }

        char datestring[6];
        snprintf_P(datestring,
                   countof(datestring),
                   PSTR("%02u:%02u"),
                   timeinfo.tm_hour,
                   timeinfo.tm_min);

        drawClock(datestring, mostPredominantColor, leastPredominantColor, true);
    }
    else
    {
        Serial.println(F("Spotify is not playing, drawing clock"));

        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
        pixels.show();

        struct tm timeinfo;

        if (!getLocalTime(&timeinfo))
        {
            Serial.println(F("Failed to obtain time"));
        }

        char datestring[6];
        snprintf_P(datestring,
                   countof(datestring),
                   PSTR("%02u:%02u"),
                   timeinfo.tm_hour,
                   timeinfo.tm_min);

        drawMonthDay(timeinfo.tm_mday, timeinfo.tm_hour);

        drawWeekDay(timeinfo.tm_wday, timeinfo.tm_hour);

        drawClock(datestring, getClockDigitColor(timeinfo.tm_hour, timeinfo.tm_min), 0, timeinfo.tm_hour <= SHOW_HOUR_START || timeinfo.tm_hour >= SHOW_HOUR_END);

        currentAlbumArtUrl = "";
        previousAlbumArtUrl = " ";
    }

    display->flipDMABuffer();
    delay(1000);
}
