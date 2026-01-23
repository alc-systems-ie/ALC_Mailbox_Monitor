#pragma once

/**
 * @file mqtt.hpp
 * @brief MQTT Client for ALC Mailbox Monitor.
 * 
 * Handles secure MQTT communication with HiveMQ Cloud broker.
 * Adapted from ALC_Help_At_Hand with mailbox-specific topics.
 */

#include <zephyr/kernel.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>

namespace alc
{
  class App;  // Forward declaration.

  class MqttClient 
  {
    public:
      /**
       * @brief Constructor.
       * @param app Reference to application for callbacks.
       */
      MqttClient(App& app);
      
      /**
       * @brief Initialise MQTT client.
       * 
       * Provisions certificates and configures client.
       * Requires modem to be initialised (for IMEI).
       * 
       * @return true on success.
       */
      bool Init();
      
      /**
       * @brief Connect to MQTT broker.
       * 
       * Requires active network connection.
       * 
       * @return true if connection initiated.
       */
      bool Connect();
      
      /**
       * @brief Disconnect from broker.
       * @return true on success.
       */
      bool Disconnect();
      
      /**
       * @brief Publish a message.
       * @param topic Topic string.
       * @param message Message payload.
       * @param length Message length.
       * @param retain Whether to retain the message.
       * @return true on success.
       */
      bool Publish(const char* topic, const char* message, size_t length, bool retain);
      
      int Subscribe(const char* topic);
      /**
       * @brief Clear a retained message.
       * @param topic Topic to clear.
       * @return true on success.
       */
      bool ClearRetained(const char* topic);
      
      /**
       * @brief Process MQTT keepalive and events.
       * @param timeoutMs Poll timeout (-1 for default).
       * @return poll result.
       */
      int KeepAlive(int timeoutMs = -1);
      
      /**
       * @brief Process incoming MQTT events.
       * @return true on success.
       */
      bool ProcessEvents();
      
      /**
       * @brief Wait for PUBACK after QoS1 publish.
       * @param timeoutMs Maximum wait time.
       * @return true if PUBACK received.
       */
      bool WaitForPuback(int timeoutMs);
      
      /**
       * @brief Check connection status.
       * @return true if connected.
       */
      bool IsConnected() const { return m_connected; }
      
      /**
       * @brief Get device IMEI.
       * @return IMEI string.
       */
      const char* GetImei() const { return m_imei; }
      
    private:
      // Buffer sizes.
      static constexpr int M_IMEI_LEN { 15 };
      static constexpr size_t M_RX_BUFFER_SIZE { 512 };
      static constexpr size_t M_TX_BUFFER_SIZE { 512 };
      static constexpr size_t M_PAYLOAD_BUFFER_SIZE { 512 };
      
      // Credentials (same as Help_At_Hand - shared HiveMQ account).
      static constexpr const char* M_USERNAME { "alcsystems" };
      static constexpr const char* M_PASSWORD { "#bErtie2017" };
      
      // Broker settings.
      static constexpr const char* M_BROKER_HOSTNAME { "e40e8a66d54d495c86d6336e20375793.s1.eu.hivemq.cloud" };
      static constexpr uint16_t M_BROKER_PORT { 8883 };
      
      // TLS settings.
      static constexpr int M_CYPHER_COUNT { 0 };
      static constexpr sec_tag_t M_SEC_TAG { 24 };
      static constexpr int M_PEER_VERIFY { 2 };
      
      // Timing.
      static constexpr uint32_t M_RECONNECT_DELAY_S { 60 };
      
      // Initialisation helpers.
      bool certificateProvision();
      bool brokerInit();
      void fdsInit();
      
      // Event handling.
      static void mqttEventHandler(mqtt_client* client, const mqtt_evt* event);
      void onConnack(int result);
      void onDisconnect(int result);
      void onPublish(const mqtt_publish_param* param);
      void onPuback(uint16_t messageId);
      void onSuback(uint16_t messageId);
      
      // Payload handling.
      int getReceivedPayload(size_t length);
      
      // Members.
      App& m_app;
      
      char m_imei[16];
      char m_client_id[32];
      mqtt_client m_client;
      
      sockaddr_storage m_broker;
      mqtt_utf8 m_username;
      mqtt_utf8 m_password;
      
      pollfd m_fds;
      
      uint8_t m_rx_buffer[M_RX_BUFFER_SIZE];
      uint8_t m_tx_buffer[M_TX_BUFFER_SIZE];
      uint8_t m_payload_buffer[M_PAYLOAD_BUFFER_SIZE];
      
      sec_tag_t m_sec_tag_list[1];
      
      int m_connect_attempts;
      bool m_connected;
      bool m_puback_received;
      
      static MqttClient* s_instance;
  };
}
