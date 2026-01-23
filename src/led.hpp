#pragma once

#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>

namespace alc
{
  struct RgbColour {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
  };

  // Common colors
  namespace LedColours {
    constexpr RgbColour OFF = { 0, 0, 0 };
    constexpr RgbColour AMBER = { 100, 50, 0 };
    constexpr RgbColour ORANGE = { 100, 30, 0 };
    constexpr RgbColour RED = { 100, 0, 0 };
    constexpr RgbColour GREEN = { 0, 100, 0 };
    constexpr RgbColour BLUE = { 0, 0, 100 };
  }

  class Led 
  {
    public:
      Led();
      
      bool Init();
      void SetColour(const RgbColour& colour);
      void StartFlashing(const RgbColour& colour, uint32_t periodMs);
      void StopFlashing();
      
    private:
      static void flashTimerHandler(struct k_timer *timer);
      void toggleFlash();
      
      pwm_dt_spec m_led_red;
      pwm_dt_spec m_led_green;
      pwm_dt_spec m_led_blue;
      
      k_timer m_flash_timer;
      RgbColour m_flash_colour;
      bool m_flash_state;
      
      static Led* s_instance;
  };
}
