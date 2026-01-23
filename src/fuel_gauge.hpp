#pragma once

#include <nrf_fuel_gauge.h>

namespace alc 
{

  /**
   * @brief Battery Fuel Gauge using Nordic nrf_fuel_gauge library.
   * 
   * Provides battery state-of-charge (SoC), time-to-empty (TTE), and 
   * time-to-full (TTF) estimations based on voltage, current, and temperature
   * measurements.
   * 
   * The fuel gauge is sensor-agnostic and can work with any source of
   * battery voltage, current, and temperature data.
   */
  
  class FuelGauge 
  {
    public:
      /**
       * @brief Fuel gauge state structure.
       */
      struct State {
          float soc;          // State of charge (0.0 to 100.0).
          float tte;          // Time to empty in seconds (-1 if not applicable).
          float ttf;          // Time to full in seconds (-1 if not applicable).
          float voltage;      // Last voltage reading.
          float current;      // Last current reading.
          float temperature;  // Last temperature reading.
      };

      /**
       * @brief Constructor.
       * @param battery_model Pointer to battery model parameters (must remain valid).
       */
      explicit FuelGauge(const struct battery_model* battery_model);

      /**
       * @brief Initialise the fuel gauge.
       * @param initialVoltage Initial battery voltage in volts.
       * @param initialCurrent Initial battery current in amps (positive = charging).
       * @param initialTemp Initial battery temperature in celsius.
       * @param maxChargeCurrent Maximum charging current in amps.
       * @param termChargeCurrent Termination charging current in amps.
       * @return 0 on success, negative error code on failure.
       */
      int Init(float initialVoltage, float initialCurrent, float initialTemp, float maxChargeCurrent, float termChargeCurrent);

      /**
       * @brief Update fuel gauge with new measurements.
       * @param voltage Battery voltage in volts.
       * @param current Battery current in amps (positive = charging, negative = discharging).
       * @param temp Battery temperature in celsius.
       * @param vbusConnected True if VBUS/charger is connected.
       * @param deltaSeconds Time since last update in seconds.
       * @return 0 on success, negative error code on failure.
       */
      int Update(float voltage, float current, float temp, bool vbusConnected, float deltaSeconds);

      /**
       * @brief Inform fuel gauge of charge state change.
       * @param chargeState New charge state.
       * @return 0 on success, negative error code on failure.
       */
      int UpdateChargeState(enum nrf_fuel_gauge_charge_state chargeState);

      /**
       * @brief Get current fuel gauge state.
       * @return Current state.
       */
      const State& GetState() const { return m_state; }

      /**
       * @brief Get state of charge.
       * @return SoC percentage (0.0 to 100.0)
       */
      float GetSoc() const { return m_state.soc; }

      /**
       * @brief Get time to empty.
       * @return Time to empty in seconds, -1 if not applicable.
       */
      float GetTte() const { return m_state.tte; }

      /**
       * @brief Get time to full.
       * @return Time to full in seconds, -1 if not applicable.
       */
      float GetTtf() const { return m_state.ttf; }

      /**
       * @brief Check if fuel gauge is initialised.
       * @return True if initialised.
       */
      bool IsInitialised() const { return m_initialised; }

    private:
      const struct battery_model* m_battery_model;
      State m_state;
      bool m_initialised;

      /**
       * @brief Update VBUS connection state in fuel gauge.
       */
      int updateVbusState(bool connected);
  };
}
