#include <zephyr/logging/log.h>
#include "led.hpp"

LOG_MODULE_REGISTER(led, LOG_LEVEL_INF);
namespace alc 
{
  Led* Led::s_instance = nullptr;

  Led::Led():
    m_led_red(PWM_DT_SPEC_GET(DT_ALIAS(led0_red))),
    m_led_green(PWM_DT_SPEC_GET(DT_ALIAS(led0_green))),
    m_led_blue(PWM_DT_SPEC_GET(DT_ALIAS(led0_blue))),
    m_flash_colour(LedColours::OFF),
    m_flash_state(false)
  {
    s_instance = this;
    k_timer_init(&m_flash_timer, flashTimerHandler, nullptr);
  }

  bool Led::Init()
  {
    if (!pwm_is_ready_dt(&m_led_red) || !pwm_is_ready_dt(&m_led_green) || !pwm_is_ready_dt(&m_led_blue)) {
      LOG_ERR("LED PWM not ready!");
      return false;
    }
      
    SetColour(LedColours::OFF);
    LOG_INF("LED initialised.");
    return true;
  }

  void Led::SetColour(const RgbColour& colour)
  {
    uint32_t period { m_led_red.period };
    
    pwm_set_dt(&m_led_red, period, (period * colour.red / 100));
    pwm_set_dt(&m_led_green, period, (period * colour.green / 100));
    pwm_set_dt(&m_led_blue, period, (period * colour.blue / 100));
  }

  void Led::StartFlashing(const RgbColour& colour, uint32_t periodMs)
  {
    k_timer_stop(&m_flash_timer);
    m_flash_colour = colour;
    m_flash_state = false;
    k_timer_start(&m_flash_timer, K_MSEC(periodMs), K_MSEC(periodMs));
  }

  void Led::StopFlashing() 
  {
    k_timer_stop(&m_flash_timer);
    SetColour(LedColours::OFF);
  }

  void Led::toggleFlash() 
  {
    m_flash_state = !m_flash_state;
    SetColour(m_flash_state ? m_flash_colour : LedColours::OFF);
  }

  void Led::flashTimerHandler(k_timer* timer) 
  {
    s_instance->toggleFlash();
  }
}
