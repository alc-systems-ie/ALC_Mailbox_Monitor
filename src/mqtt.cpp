#include "mqtt.hpp"
#include "app.hpp"
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/net/socket.h>
#include <nrf_modem_at.h>
#include <modem/modem_key_mgmt.h>

// Certificate - same as Help_At_Hand (HiveMQ ISRG Root X1).
#include "certificate.h"

LOG_MODULE_REGISTER(mqtt, LOG_LEVEL_INF);

namespace alc
{
  MqttClient* MqttClient::s_instance { nullptr };

  MqttClient::MqttClient(App& app)
      : m_app(app)
      , m_connect_attempts(0)
      , m_connected(false)
      , m_puback_received(false)
  {
    s_instance = this;
    
    // Set up broker credentials.
    m_username.utf8 = reinterpret_cast<const uint8_t*>(M_USERNAME);
    m_username.size = static_cast<uint32_t>(strlen(M_USERNAME));
    m_password.utf8 = reinterpret_cast<const uint8_t*>(M_PASSWORD);
    m_password.size = static_cast<uint32_t>(strlen(M_PASSWORD));
    
    // Setup security tag.
    m_sec_tag_list[0] = M_SEC_TAG;
  }

  bool MqttClient::Init()
  {
    // Provision certificate.
    if (!certificateProvision()) {
      LOG_ERR("Failed to provision certificates!");
      return false;
    }
    
    // Get IMEI for client ID.
    constexpr int RESPONSE_LEN { M_IMEI_LEN + 6 + 1 };
    char imeiBuffer[RESPONSE_LEN];
    
    int result { nrf_modem_at_cmd(imeiBuffer, sizeof(imeiBuffer), "AT+CGSN") };
    if (result) {
      LOG_ERR("Failed to get IMEI: %d!", result);
      return false;
    }
    
    // Copy IMEI.
    strncpy(m_imei, imeiBuffer, M_IMEI_LEN);
    m_imei[M_IMEI_LEN] = '\0';
    
    // Build client ID: "alc_mb_" + IMEI (mailbox).
    snprintf(m_client_id, sizeof(m_client_id), "alc_mb_%s", m_imei);
    
    // Initialise MQTT client structure.
    mqtt_client_init(&m_client);
    
    // Configure MQTT client.
    m_client.evt_cb = mqttEventHandler;
    m_client.client_id.utf8 = reinterpret_cast<const uint8_t*>(m_client_id);
    m_client.client_id.size = strlen(m_client_id);
    m_client.user_name = &m_username;
    m_client.password = &m_password;
    m_client.protocol_version = MQTT_VERSION_3_1_1;
    
    // Setup buffers.
    m_client.rx_buf = m_rx_buffer;
    m_client.rx_buf_size = sizeof(m_rx_buffer);
    m_client.tx_buf = m_tx_buffer;
    m_client.tx_buf_size = sizeof(m_tx_buffer);
    
    // Configure TLS.
    m_client.transport.type = MQTT_TRANSPORT_SECURE;
    
    struct mqtt_sec_config* tls_cfg { &m_client.transport.tls.config };
    tls_cfg->peer_verify = M_PEER_VERIFY;
    tls_cfg->cipher_list = nullptr;
    tls_cfg->cipher_count = M_CYPHER_COUNT;
    tls_cfg->sec_tag_count = ARRAY_SIZE(m_sec_tag_list);
    tls_cfg->sec_tag_list = m_sec_tag_list;
    tls_cfg->hostname = M_BROKER_HOSTNAME;
    tls_cfg->session_cache = TLS_SESSION_CACHE_ENABLED;
    
    LOG_INF("MQTT client initialised with ID: %s.", m_client_id);
    return true;
  }

  bool MqttClient::Connect()
  {
    m_connected = false;

    // Resolve broker hostname.
    if (!brokerInit()) {
      LOG_ERR("Broker DNS resolution failed!");
      return false;
    }

    // Assign broker to client.
    m_client.broker = &m_broker;

    if (m_connect_attempts > 0) {
      k_sleep(K_SECONDS(M_RECONNECT_DELAY_S));
    }

    // Set longer timeout for NB-IoT.
    timeval timeout {
      .tv_sec = 60,
      .tv_usec = 0
    };

    int result { mqtt_connect(&m_client) };
    if (result) {
      LOG_ERR("MQTT connect failed: %d!", result);
      m_connect_attempts++;
      return false;
    }

    // Set socket timeout.
    if (m_client.transport.type == MQTT_TRANSPORT_SECURE) {
      setsockopt(m_client.transport.tls.sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
      setsockopt(m_client.transport.tls.sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    }

    fdsInit();

    LOG_INF("MQTT connect initiated, waiting for CONNACK...");

    // Wait for CONNACK - poll and process events until connected or timeout.
    constexpr int CONNACK_TIMEOUT_MS { 30000 };
    constexpr int POLL_INTERVAL_MS { 100 };
    int elapsed { 0 };

    while (!m_connected && elapsed < CONNACK_TIMEOUT_MS) {
      KeepAlive(POLL_INTERVAL_MS);
      ProcessEvents();
      k_msleep(POLL_INTERVAL_MS);
      elapsed += POLL_INTERVAL_MS;
    }

    if (!m_connected) {
      LOG_ERR("CONNACK timeout after %d ms!", CONNACK_TIMEOUT_MS);
      mqtt_disconnect(&m_client, nullptr);
      m_connect_attempts++;
      return false;
    }

    LOG_INF("MQTT connected after %d ms.", elapsed);
    return true;
  }

  bool MqttClient::Disconnect()
  {
    int result { mqtt_disconnect(&m_client, nullptr) };
    if (result) {
      LOG_ERR("Disconnect failed: %d!", result);
      return false;
    }
    
    m_connected = false;
    LOG_INF("MQTT client disconnected.");
    return true;
  }

  bool MqttClient::Publish(const char* topic, const char* message, size_t length, bool retain)
  {
    if (!m_connected) {
      LOG_WRN("Not connected, cannot publish!");
      return false;
    }
    
    LOG_INF("Publishing to %s: %.*s", topic, static_cast<int>(length), message);
    
    mqtt_publish_param param;
    param.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
    param.message.topic.topic.utf8 = reinterpret_cast<const uint8_t*>(topic);
    param.message.topic.topic.size = strlen(topic);
    param.message.payload.data = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(message));
    param.message.payload.len = length;
    param.message_id = sys_rand32_get();
    param.dup_flag = 0;
    param.retain_flag = retain ? 1 : 0;
    
    int result { mqtt_publish(&m_client, &param) };
    if (result) {
      LOG_ERR("Publish failed: %d!", result);
      return false;
    }
    
    return true;
  }

  bool MqttClient::ClearRetained(const char* topic)
  {
    if (!m_connected) {
      LOG_WRN("Not connected, cannot clear retained!");
      return false;
    }
    
    LOG_INF("Clearing retained message on topic: %s", topic);
    
    mqtt_publish_param param;
    param.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
    param.message.topic.topic.utf8 = reinterpret_cast<const uint8_t*>(topic);
    param.message.topic.topic.size = strlen(topic);
    param.message.payload.data = nullptr;
    param.message.payload.len = 0;
    param.message_id = sys_rand32_get();
    param.dup_flag = 0;
    param.retain_flag = 1;
    
    int result { mqtt_publish(&m_client, &param) };
    if (result) {
      LOG_ERR("Clear retained failed: %d!", result);
      return false;
    }
    
    return true;
  }

  int MqttClient::KeepAlive(int timeoutMs)
  {
    int timeout { (timeoutMs >= 0) ? timeoutMs : mqtt_keepalive_time_left(&m_client) };
    
    int result { poll(&m_fds, 1, timeout) };
    if (result < 0) {
      LOG_ERR("Poll error: %d!", result);
    }
    
    return result;
  }

  bool MqttClient::ProcessEvents()
  {
    int result { mqtt_live(&m_client) };
    if (result != 0 && result != -EAGAIN) {
      LOG_ERR("mqtt_live error: %d!", result);
      return false;
    }
    
    if ((m_fds.revents & POLLIN) == POLLIN) {
      result = mqtt_input(&m_client);
      if (result != 0) {
        LOG_ERR("mqtt_input error: %d!", result);
        return false;
      }
    }
    
    if ((m_fds.revents & POLLERR) == POLLERR) {
      LOG_ERR("POLLERR");
      return false;
    }
    
    if ((m_fds.revents & POLLNVAL) == POLLNVAL) {
      LOG_ERR("POLLNVAL");
      return false;
    }
    
    return true;
  }

  bool MqttClient::WaitForPuback(int timeoutMs)
  {
    m_puback_received = false;
    int elapsed { 0 };
    constexpr int pollInterval { 100 };
    
    while (!m_puback_received && elapsed < timeoutMs) {
      KeepAlive(pollInterval);
      ProcessEvents();
      k_sleep(K_MSEC(pollInterval));
      elapsed += pollInterval;
    }
    
    if (m_puback_received) {
      LOG_INF("PUBACK confirmed after %d ms.", elapsed);
      return true;
    }
    
    LOG_WRN("PUBACK timeout after %d ms!", timeoutMs);
    return false;
  }

  // ========== Private Methods ==========

  bool MqttClient::brokerInit()
  {
    constexpr int MAX_DNS_RETRIES { 3 };
    constexpr int DNS_RETRY_DELAY_MS { 2000 };
    
    addrinfo* resultAddr { nullptr };
    addrinfo hints { };
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    int result { -1 };
    
    for (int attempt { 0 }; attempt < MAX_DNS_RETRIES; attempt++) {
      if (attempt > 0) {
        LOG_INF("DNS retry %d/%d in %dms...", attempt + 1, MAX_DNS_RETRIES, DNS_RETRY_DELAY_MS);
        k_sleep(K_MSEC(DNS_RETRY_DELAY_MS));
      }
      
      LOG_INF("Resolving broker: %s (attempt %d/%d).", M_BROKER_HOSTNAME, attempt + 1, MAX_DNS_RETRIES);
      result = getaddrinfo(M_BROKER_HOSTNAME, nullptr, &hints, &resultAddr);
      
      if (result == 0) { break; }
      
      LOG_WRN("DNS resolution failed: %d", result);
    }
    
    if (result != 0) {
      LOG_ERR("DNS resolution failed after %d attempts: %d!", MAX_DNS_RETRIES, result);
      return false;
    }
    
    while (resultAddr != nullptr) {
      if (resultAddr->ai_addrlen == sizeof(struct sockaddr_in)) {
        sockaddr_in* broker4 { reinterpret_cast<struct sockaddr_in*>(&m_broker) };
        char ipv4_addr[NET_IPV4_ADDR_LEN];
        
        broker4->sin_addr.s_addr = reinterpret_cast<struct sockaddr_in*>(resultAddr->ai_addr)->sin_addr.s_addr;
        broker4->sin_family = AF_INET;
        broker4->sin_port = htons(M_BROKER_PORT);
        
        inet_ntop(AF_INET, &broker4->sin_addr.s_addr, ipv4_addr, sizeof(ipv4_addr));
        LOG_INF("Broker IPv4: %s", ipv4_addr);
        break;
      }
      resultAddr = resultAddr->ai_next;
    }
    
    freeaddrinfo(resultAddr);
    return true;
  }

  void MqttClient::fdsInit()
  {
    m_fds.events = POLLIN;
    m_fds.fd = m_client.transport.tls.sock;
  }

  int MqttClient::Subscribe(const char* topic)
  {
    if (!m_connected) {
      LOG_WRN("Not connected, cannot subscribe!");
      return -ENOTCONN;
    }
    
    mqtt_topic sub_topic;
    sub_topic.topic.utf8 = reinterpret_cast<const uint8_t*>(topic);
    sub_topic.topic.size = strlen(topic);
    sub_topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
    
    mqtt_subscription_list sub_list {
      .list = &sub_topic,
      .list_count = 1,
      .message_id = static_cast<uint16_t>(sys_rand32_get())
    };
    
    LOG_INF("Subscribing to: %s", topic);
    return mqtt_subscribe(&m_client, &sub_list);
  }

  int MqttClient::getReceivedPayload(size_t length)
  {
    if (length > sizeof(m_payload_buffer)) {
      LOG_ERR("Payload too large: %d bytes!", static_cast<int>(length));
      return -EMSGSIZE;
    }
    
    int result { mqtt_readall_publish_payload(&m_client, m_payload_buffer, length) };
    if (result) {
      LOG_ERR("Failed to read payload: %d!", result);
      return result;
    }
    
    return 0;
  }

  bool MqttClient::certificateProvision()
  {
    bool exists { false };
    
    int result { modem_key_mgmt_exists(M_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, &exists) };
    if (result) {
      LOG_ERR("Failed to check for certificates: %d!", result);
      return false;
    }
    
    if (exists) {
      result = modem_key_mgmt_cmp(M_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, 
                                   CA_CERTIFICATE, strlen(CA_CERTIFICATE));
      LOG_INF("Existing certificate %s.", result ? "doesn't match" : "matches");
      if (result == 0) {
        return true;
      }
    }
    
    result = modem_key_mgmt_write(M_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, 
                                   CA_CERTIFICATE, strlen(CA_CERTIFICATE));
    if (result) {
      LOG_ERR("Failed to provision CA certificate: %d!", result);
      return false;
    }
    
    LOG_INF("Certificate provisioned.");
    return true;
  }

  // ========== Event Handlers ==========

  void MqttClient::mqttEventHandler(mqtt_client* client, const mqtt_evt* event)
  {
    switch (event->type) {
      case MQTT_EVT_CONNACK:
        s_instance->onConnack(event->result);
        break;
        
      case MQTT_EVT_DISCONNECT:
        s_instance->onDisconnect(event->result);
        break;
        
      case MQTT_EVT_PUBLISH:
        s_instance->onPublish(&event->param.publish);
        break;
        
      case MQTT_EVT_PUBACK:
        s_instance->onPuback(event->param.puback.message_id);
        break;
        
      case MQTT_EVT_SUBACK:
        s_instance->onSuback(event->param.suback.message_id);
        break;
        
      case MQTT_EVT_PINGRESP:
        if (event->result != 0) {
          LOG_ERR("PINGRESP error: %d", event->result);
        }
        break;
        
      default:
        LOG_DBG("Unhandled MQTT event: %d.", event->type);
        break;
    }
  }

  void MqttClient::onConnack(int result)
  {
    if (result != 0) {
      LOG_ERR("MQTT connect failed: %d", result);
      m_connected = false;
      m_app.OnMqttDisconnected();
      return;
    }
    
    LOG_INF("MQTT connected.");
    m_connected = true;
    m_connect_attempts = 0;
    
    // Notify application - app is responsible for subscribing.
    m_app.OnMqttConnected();
  }

  void MqttClient::onDisconnect(int result)
  {
    LOG_INF("MQTT disconnected: %d.", result);
    m_connected = false;
    m_app.OnMqttDisconnected();
  }

  void MqttClient::onPublish(const mqtt_publish_param* param)
  {
    size_t messageLength { param->message.payload.len };
    
    LOG_INF("Received message, length: %d.", static_cast<int>(messageLength));
    
    int result { getReceivedPayload(messageLength) };
    if (result < 0) {
      LOG_ERR("Failed to get payload: %d!", result);
      return;
    }
    
    // Send ACK for QoS1.
    if (param->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
      mqtt_puback_param ack { .message_id = param->message_id };
      mqtt_publish_qos1_ack(&m_client, &ack);
    }
    
    // Notify application.
    m_app.OnMqttMessageReceived(reinterpret_cast<const char*>(m_payload_buffer), messageLength);
  }

  void MqttClient::onPuback(uint16_t messageId)
  {
    LOG_INF("PUBACK received, message Id: %u.", messageId);
    m_puback_received = true;
  }

  void MqttClient::onSuback(uint16_t messageId)
  {
    LOG_INF("SUBACK received, message Id: %u.", messageId);
  }
}
