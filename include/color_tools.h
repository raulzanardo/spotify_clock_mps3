#include <Arduino.h>
#include <map>
#include <algorithm>
#include "config.h"
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static inline uint16_t getClockDigitColor(int hour, int minute)
{
    // Calculate the time as a float from 0 to 24
    float timeOfDay = hour + minute / 60.0f;

    // Calculate color temperature
    float temp;
    if (timeOfDay < NIGHT_END_HOUR || timeOfDay >= NIGHT_START_HOUR)
    {
        // Night time (10 PM to 6 AM)
        temp = NIGHT_TEMP;
    }
    else if (timeOfDay < 12)
    {
        // Morning: temperature increases
        temp = MIN_TEMP + (MAX_TEMP - MIN_TEMP) * ((timeOfDay - 6) / 6.0f);
    }
    else if (timeOfDay < 18)
    {
        // Afternoon: temperature decreases
        temp = MAX_TEMP - (MAX_TEMP - MIN_TEMP) * ((timeOfDay - 12) / 6.0f);
    }
    else
    {
        // Evening: temperature decreases to night temp
        temp = MIN_TEMP - (MIN_TEMP - NIGHT_TEMP) * ((timeOfDay - 18) / 4.0f);
    }

    // Convert temperature to RGB
    float red, green, blue;

    // Approximation of RGB values from color temperature
    // Based on a simplified version of the algorithm by Tanner Helland
    temp = temp / 100;

    if (temp <= 66)
    {
        red = 255;
        green = 99.4708025861f * std::log(temp) - 161.1195681661f;
        if (temp <= 19)
        {
            blue = 0;
        }
        else
        {
            blue = 138.5177312231f * std::log(temp - 10) - 305.0447927307f;
        }
    }
    else
    {
        red = 329.698727446f * std::pow(temp - 60, -0.1332047592f);
        green = 288.1221695283f * std::pow(temp - 60, -0.0755148492f);
        blue = 255;
    }

    // Clamp RGB values to 0-255 range
    red = std::min(255.0f, std::max(0.0f, red));
    green = std::min(255.0f, std::max(0.0f, green));
    blue = std::min(255.0f, std::max(0.0f, blue));

    // Dim the color at night
    if (timeOfDay < NIGHT_END_HOUR || timeOfDay >= NIGHT_START_HOUR)
    {
        float dimFactor = NIGHT_DIM_FACTOR; // Adjust this value to change nighttime brightness
        red *= dimFactor;
        green *= dimFactor;
        blue *= dimFactor;
    }

    // Convert to RGB565
    uint16_t r = static_cast<uint16_t>(red * 31 / 255);
    uint16_t g = static_cast<uint16_t>(green * 63 / 255);
    uint16_t b = static_cast<uint16_t>(blue * 31 / 255);

    return (r << 11) | (g << 5) | b;
}

// Function to calculate color similarity
bool areColorsSimilar(uint16_t color1, uint16_t color2, uint16_t threshold)
{
  uint8_t r1 = (color1 >> 11) & 0x1F;
  uint8_t g1 = (color1 >> 5) & 0x3F;
  uint8_t b1 = color1 & 0x1F;

  uint8_t r2 = (color2 >> 11) & 0x1F;
  uint8_t g2 = (color2 >> 5) & 0x3F;
  uint8_t b2 = color2 & 0x1F;

  uint16_t difference = std::abs(r1 - r2) + std::abs(g1 - g2) + std::abs(b1 - b2);
  return difference <= threshold;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Local helper: convert RGB565 -> 8-bit RGB
static inline void rgb565ToRgb8(uint16_t color, uint8_t &r, uint8_t &g, uint8_t &b)
{
  uint8_t r5 = (color >> 11) & 0x1F;
  uint8_t g6 = (color >> 5) & 0x3F;
  uint8_t b5 = color & 0x1F;

  r = static_cast<uint8_t>((r5 * 255 + 15) / 31);
  g = static_cast<uint8_t>((g6 * 255 + 31) / 63);
  b = static_cast<uint8_t>((b5 * 255 + 15) / 31);
}

// Local helper: convert 8-bit RGB -> RGB565
static inline uint16_t rgb8ToRgb565(uint8_t r, uint8_t g, uint8_t b)
{
  uint16_t r5 = static_cast<uint16_t>((r * 31 + 127) / 255);
  uint16_t g6 = static_cast<uint16_t>((g * 63 + 127) / 255);
  uint16_t b5 = static_cast<uint16_t>((b * 31 + 127) / 255);
  return (r5 << 11) | (g6 << 5) | b5;
}

uint16_t invertColor(uint16_t color)
{
  uint8_t r, g, b;
  rgb565ToRgb8(color, r, g, b);

  r = 255 - r;
  g = 255 - g;
  b = 255 - b;

  return rgb8ToRgb565(r, g, b);
}

// Function to calculate vibrancy (combination of saturation and brightness)
float calculateVibrancy(uint16_t color)
{
  uint8_t r, g, b;
  rgb565ToRgb8(color, r, g, b);

  float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
  float maxv = std::max({rf, gf, bf});
  float minv = std::min({rf, gf, bf});

  float saturation = (maxv == 0.0f) ? 0.0f : (maxv - minv) / maxv;
  float brightness = (rf + gf + bf) / 3.0f;

  // Combine saturation and brightness for vibrancy
  return saturation * brightness;
}