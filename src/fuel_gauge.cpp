#include "fuel_gauge.hpp"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(fuel_gauge, LOG_LEVEL_INF);

namespace alc 
{

  FuelGauge::FuelGauge(const struct battery_model* batteryModel)
      : m_battery_model(batteryModel)
      , m_state{}
      , m_initialised(false)
  {
  }

  int FuelGauge::Init(float initialVoltage, float initialCurrent, float initialTemp, float maxChargeCurrent, float termChargeCurrent)
  {
    constexpr int OK { 0 };

    LOG_INF("Initializing fuel gauge, nRF Fuel Gauge version: %s", nrf_fuel_gauge_version);

    struct nrf_fuel_gauge_init_parameters parameters = {
      .model = m_battery_model,
      .opt_params = nullptr,
      .state = nullptr,
    };
    parameters.v0 = initialVoltage;
    // Note: nrf_fuel_gauge expects negative current for charging,
    // but we use positive for charging (Zephyr convention is opposite).
    parameters.i0 = -initialCurrent;
    parameters.t0 = initialTemp;

    int result { nrf_fuel_gauge_init(&parameters, nullptr) };
    if (result < 0) {
      LOG_ERR("Failed to initialise fuel gauge: %d!", result);
      return result;
    }

    // Set maximum charge current.
    union nrf_fuel_gauge_ext_state_info_data chargeLimitData;
    chargeLimitData.charge_current_limit = maxChargeCurrent;
    
    result = nrf_fuel_gauge_ext_state_update( NRF_FUEL_GAUGE_EXT_STATE_INFO_CHARGE_CURRENT_LIMIT, &chargeLimitData);
    if (result < 0) {
      LOG_ERR("Failed to set max charge current: %d!", result);
      return result;
    }

    // Set termination charge current.
    union nrf_fuel_gauge_ext_state_info_data termCurrentData;
    termCurrentData.charge_term_current = termChargeCurrent;
    
    result = nrf_fuel_gauge_ext_state_update(NRF_FUEL_GAUGE_EXT_STATE_INFO_TERM_CURRENT, &termCurrentData);
    if (result < 0) {
      LOG_ERR("Failed to set term charge current: %d!", result);
      return result;
    }

    // Initialise state.
    constexpr float socInitValue { 0.0f };
    constexpr float ttxInitValue { -1.0f };

    m_state.voltage = initialVoltage;
    m_state.current = initialCurrent;
    m_state.temperature = initialTemp;
    m_state.soc = socInitValue;
    m_state.tte = ttxInitValue;
    m_state.ttf = ttxInitValue;

    m_initialised = true;

    LOG_INF("Fuel gauge initialised: V=%.3fV, I=%.3fA, T=%.1f degC", (double)initialVoltage, (double)initialCurrent, (double)initialTemp);

    return OK;
  }

  int FuelGauge::Update(float voltage, float current, float temp, bool vbusConnected, float deltaSeconds)
  {
    constexpr int OK { 0 };

    if (!m_initialised) {
      LOG_ERR("Fuel gauge not initialised!");
      return -EINVAL;
    }

    // Update VBUS state if it changed.
    int result = updateVbusState(vbusConnected);
    if (result < 0) {
      LOG_ERR("Failed to update VBUS state: %d!", result);
      return result;
    }

    // Store current measurements.
    m_state.voltage = voltage;
    m_state.current = current;
    m_state.temperature = temp;

    // Process fuel gauge algorithm.
    // Note: nrf_fuel_gauge expects negative current for charging.
    m_state.soc = nrf_fuel_gauge_process(voltage, -current, temp, deltaSeconds, nullptr);
    m_state.tte = nrf_fuel_gauge_tte_get();
    m_state.ttf = nrf_fuel_gauge_ttf_get();

    LOG_DBG("Fuel gauge: V=%.3f, I=%.3f, T=%.1f, SoC=%.1f%%, TTE=%.0fs, TTF=%.0fs",
           (double)voltage, (double)current, (double)temp, 
           (double)m_state.soc, (double)m_state.tte, (double)m_state.ttf);

    return OK;
  }

  int FuelGauge::UpdateChargeState(enum nrf_fuel_gauge_charge_state chargeState)
  {
    constexpr int OK { 0 };

    if (!m_initialised) {
      LOG_ERR("Fuel gauge not initialized");
      return -EINVAL;
    }

    union nrf_fuel_gauge_ext_state_info_data stateInfo;
    stateInfo.charge_state = chargeState;

    int result { nrf_fuel_gauge_ext_state_update(NRF_FUEL_GAUGE_EXT_STATE_INFO_CHARGE_STATE_CHANGE, &stateInfo) };
    if (result < 0) {
      LOG_ERR("Failed to update charge state: %d!", result);
      return result;
    }

    const char* stateStr;
    switch (chargeState) 
    {
      case NRF_FUEL_GAUGE_CHARGE_STATE_IDLE:
        stateStr = "Idle";
        break;
      case NRF_FUEL_GAUGE_CHARGE_STATE_TRICKLE:
        stateStr = "Trickle";
        break;
      case NRF_FUEL_GAUGE_CHARGE_STATE_CC:
        stateStr = "Constant Current";
        break;
      case NRF_FUEL_GAUGE_CHARGE_STATE_CV:
        stateStr = "Constant Voltage";
        break;
      case NRF_FUEL_GAUGE_CHARGE_STATE_COMPLETE:
        stateStr = "Complete";
        break;
      default:
        stateStr = "Unknown";
        break;
    }

    LOG_DBG("Charge state: %s", stateStr);

    return OK;
  }

  int FuelGauge::updateVbusState(bool connected)
  {
    constexpr int OK { 0 };

    enum nrf_fuel_gauge_ext_state_info_type state = connected 
      ? NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_CONNECTED
      : NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_DISCONNECTED;

    int result = nrf_fuel_gauge_ext_state_update(state, nullptr);
    if (result < 0) {
      LOG_ERR("Failed to update VBUS state: %d", result);
      return result;
    }

    return OK;
  }
} 
