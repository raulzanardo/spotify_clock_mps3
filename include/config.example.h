// config

// WIFI CREDENTIALS
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"

// SPOTIFY
#define CLIENT_ID "YOUR_CLIENT_ID"
#define CLIENT_SECRET "YOUR_CLIENT_SECRET"
#define REFRESH_TOKEN "YOUR_REFRESH_TOKEN"

#define PROJECTNAME "spotify_clock_mps3"

// WIFI

// Spotify credentials

#define DISABLE_AUDIOBOOKS
#define DISABLE_CATEGORIES
#define DISABLE_CHAPTERS
#define DISABLE_EPISODES
#define DISABLE_GENRES
#define DISABLE_MARKETS
#define DISABLE_PLAYLISTS
#define DISABLE_SEARCH
#define DISABLE_SHOWS

// ntp

#define ntpServer1 "pool.ntp.org"
#define ntpServer2 "time.nist.gov"

// Time settings
// TZ string used by setenv("TZ", ...)
#define TZ_STRING "BRT3"
// Offsets in seconds for configTime(gmtOffset_sec, daylightOffset_sec,...)
#define NTP_GMT_OFFSET_SECONDS (-10800)
#define NTP_DAYLIGHT_OFFSET_SECONDS (0)

// RGB pins
#define PIN_R1 42 
#define PIN_G1 41
#define PIN_B1 40
#define PIN_R2 38
#define PIN_G2 39
#define PIN_B2 37

// Address pins
#define PIN_ADDR_A 45
#define PIN_ADDR_B 36
#define PIN_ADDR_C 48
#define PIN_ADDR_D 35
#define PIN_ADDR_E 21

// Control pins
#define PIN_LAT 47 // Latch pin
#define PIN_OE 14  // Output Enable pin
#define PIN_CLK 2  // Clock pin

// Define the full pin array for easy reference
#define MATRIX_PINS {PIN_R1, PIN_G1, PIN_B1, PIN_R2, PIN_G2, PIN_B2,             \
                     PIN_ADDR_A, PIN_ADDR_B, PIN_ADDR_C, PIN_ADDR_D, PIN_ADDR_E, \
                     PIN_LAT, PIN_OE, PIN_CLK}

// LED pins
#define PIN_LED 13

// other
#define PIN_LIGHT_SENSOR 5

#define NEOPIXEL_PIN 4

// Display and behavior configuration
#define DISPLAY_BRIGHTNESS 30   // 0-255 for display->setBrightness8
#define NEOPIXEL_BRIGHTNESS 255 // 0-255 for NeoPixel
#define COLOR_SIMILARITY_THRESHOLD 10

// ===== COLOR TEMPERATURE SETTINGS =====
// Night time hour range (0-23 format)
#define NIGHT_START_HOUR 22  // 10 PM
#define NIGHT_END_HOUR 6     // 6 AM

// Color temperature settings for clock digits (in Kelvin)
#define NIGHT_TEMP 2000.0f   // Warm light for night
#define MIN_TEMP 2500.0f     // Slightly warmer minimum for São Paulo's climate
#define MAX_TEMP 7000.0f     // Slightly cooler maximum for brighter days

// Nighttime brightness dimming factor (0.0 to 1.0)
#define NIGHT_DIM_FACTOR 0.3f
