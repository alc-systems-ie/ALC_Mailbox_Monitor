#!/usr/bin/env python3
"""
MQTT Command Test Script for ALC Mailbox Monitor

Tests the MQTT command handling functionality by sending configuration
commands to the device and observing responses.

Requirements:
    pip install paho-mqtt

Usage:
    python mqtt_command_test.py [--device-id DEVICE_ID]
"""

import argparse
import json
import ssl
import sys
import time
from datetime import datetime

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("Error: paho-mqtt not installed. Run: pip install paho-mqtt")
    sys.exit(1)


# MQTT Broker Configuration
BROKER_HOST = "e40e8a66d54d495c86d6336e20375793.s1.eu.hivemq.cloud"
BROKER_PORT = 8883
USERNAME = "alcsystems"
PASSWORD = "#bErtie2017"  # Replace with actual password

# Default device ID (from app.hpp)
DEFAULT_DEVICE_ID = "ccccddddeeeeffff0000111122221111"


class MqttCommandTester:
    """Test harness for MQTT command handling."""

    def __init__(self, device_id: str):
        self.device_id = device_id
        self.client = mqtt.Client(client_id=f"test_client_{int(time.time())}")
        self.connected = False
        self.responses_received = []

        # Topic definitions
        self.topic_commands = f"alc/{device_id}/commands"
        self.topic_status = f"alc/{device_id}/status"
        self.topic_events = f"alc/{device_id}/events"
        self.topic_battery = f"alc/{device_id}/battery"
        self.topic_heartbeat = f"alc/{device_id}/heartbeat"

    def on_connect(self, client, userdata, flags, rc):
        """Callback when connected to broker."""
        if rc == 0:
            print(f"[{self._timestamp()}] Connected to MQTT broker")
            self.connected = True
            # Subscribe to response topics
            topics = [
                self.topic_status,
                self.topic_events,
                self.topic_battery,
                self.topic_heartbeat
            ]
            for topic in topics:
                client.subscribe(topic)
                print(f"[{self._timestamp()}] Subscribed to: {topic}")
        else:
            print(f"[{self._timestamp()}] Connection failed with code: {rc}")

    def on_disconnect(self, client, userdata, rc):
        """Callback when disconnected."""
        print(f"[{self._timestamp()}] Disconnected from broker (rc={rc})")
        self.connected = False

    def on_message(self, client, userdata, message):
        """Callback when message received."""
        topic = message.topic
        payload = message.payload.decode('utf-8')
        print(f"\n[{self._timestamp()}] RECEIVED on {topic}:")
        try:
            data = json.loads(payload)
            print(json.dumps(data, indent=2))
        except json.JSONDecodeError:
            print(payload)
        self.responses_received.append({
            'topic': topic,
            'payload': payload,
            'timestamp': datetime.now()
        })

    def connect(self):
        """Connect to the MQTT broker."""
        print(f"[{self._timestamp()}] Connecting to {BROKER_HOST}:{BROKER_PORT}...")

        self.client.on_connect = self.on_connect
        self.client.on_disconnect = self.on_disconnect
        self.client.on_message = self.on_message

        # TLS configuration
        self.client.tls_set(tls_version=ssl.PROTOCOL_TLS)
        self.client.username_pw_set(USERNAME, PASSWORD)

        try:
            self.client.connect(BROKER_HOST, BROKER_PORT, keepalive=60)
            self.client.loop_start()

            # Wait for connection
            timeout = 10
            while not self.connected and timeout > 0:
                time.sleep(0.5)
                timeout -= 0.5

            if not self.connected:
                print("[ERROR] Connection timeout!")
                return False
            return True
        except Exception as e:
            print(f"[ERROR] Connection failed: {e}")
            return False

    def disconnect(self):
        """Disconnect from the broker."""
        self.client.loop_stop()
        self.client.disconnect()
        print(f"[{self._timestamp()}] Disconnected")

    def send_command(self, command: dict, description: str = ""):
        """Send a command to the device."""
        payload = json.dumps(command)
        print(f"\n{'='*60}")
        print(f"[{self._timestamp()}] SENDING: {description or 'Command'}")
        print(f"  Topic: {self.topic_commands}")
        print(f"  Payload: {payload}")

        result = self.client.publish(
            self.topic_commands,
            payload,
            qos=1,
            retain=True  # Retain so device sees it on next wake
        )

        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            print(f"  Status: Published successfully")
        else:
            print(f"  Status: Publish failed (rc={result.rc})")

        return result.rc == mqtt.MQTT_ERR_SUCCESS

    def wait_for_response(self, timeout: float = 5.0):
        """Wait for a response message."""
        print(f"[{self._timestamp()}] Waiting for response ({timeout}s)...")
        initial_count = len(self.responses_received)
        start = time.time()
        while time.time() - start < timeout:
            if len(self.responses_received) > initial_count:
                return True
            time.sleep(0.1)
        print(f"[{self._timestamp()}] No response received")
        return False

    def clear_retained(self):
        """Clear the retained command message."""
        self.client.publish(self.topic_commands, "", qos=1, retain=True)
        print(f"[{self._timestamp()}] Cleared retained message on commands topic")

    @staticmethod
    def _timestamp():
        return datetime.now().strftime("%H:%M:%S")


def test_mail_window(tester: MqttCommandTester):
    """Test SET_MAIL_WINDOW command."""
    print("\n" + "="*60)
    print("TEST: Set Mail Window")
    print("="*60)

    # Test valid value
    tester.send_command(
        {"mail_window": 180},
        "Set mail window to 180 seconds (3 minutes)"
    )

    # Wait a bit between commands
    time.sleep(1)

    # Test boundary value
    tester.send_command(
        {"mail_window": 86400},
        "Set mail window to 86400 seconds (24 hours - max)"
    )


def test_activity_thresholds(tester: MqttCommandTester):
    """Test activity/inactivity threshold commands."""
    print("\n" + "="*60)
    print("TEST: Activity/Inactivity Thresholds")
    print("="*60)

    # Activity threshold
    tester.send_command(
        {"activity_threshold": 500},
        "Set activity threshold to 500mg"
    )
    time.sleep(1)

    # Activity time
    tester.send_command(
        {"activity_time": 2},
        "Set activity time to 2 samples"
    )
    time.sleep(1)

    # Inactivity threshold
    tester.send_command(
        {"inactivity_threshold": 800},
        "Set inactivity threshold to 800mg"
    )
    time.sleep(1)

    # Inactivity time
    tester.send_command(
        {"inactivity_time": 5},
        "Set inactivity time to 5 samples"
    )


def test_status_request(tester: MqttCommandTester):
    """Test REQUEST_STATUS command."""
    print("\n" + "="*60)
    print("TEST: Status Request")
    print("="*60)

    tester.send_command(
        {"status_request": True},
        "Request current configuration status"
    )

    # The device will respond on the status topic
    tester.wait_for_response(timeout=10.0)


def test_reset_config(tester: MqttCommandTester):
    """Test RESET_CONFIG command."""
    print("\n" + "="*60)
    print("TEST: Reset Configuration")
    print("="*60)

    tester.send_command(
        {"reset_config": True},
        "Reset all configuration to defaults"
    )


def test_invalid_commands(tester: MqttCommandTester):
    """Test handling of invalid commands."""
    print("\n" + "="*60)
    print("TEST: Invalid Commands (edge cases)")
    print("="*60)

    # Invalid mail window (too large)
    tester.send_command(
        {"mail_window": 100000},
        "Invalid: mail_window > 86400"
    )
    time.sleep(1)

    # Invalid mail window (negative/zero)
    tester.send_command(
        {"mail_window": 0},
        "Invalid: mail_window = 0"
    )
    time.sleep(1)

    # Unknown command
    tester.send_command(
        {"unknown_command": 123},
        "Unknown command (should be ignored)"
    )
    time.sleep(1)

    # Invalid JSON format
    tester.send_command(
        {"activity_threshold": "not_a_number"},
        "Invalid: string instead of number"
    )


def run_interactive_mode(tester: MqttCommandTester):
    """Run in interactive mode for manual testing."""
    print("\n" + "="*60)
    print("INTERACTIVE MODE")
    print("="*60)
    print("Commands:")
    print("  1 - Set mail_window to 240s (default)")
    print("  2 - Set mail_window to 60s (quick test)")
    print("  3 - Set activity_threshold to 250mg (default)")
    print("  4 - Set activity_threshold to 100mg (sensitive)")
    print("  5 - Request status")
    print("  6 - Reset to defaults")
    print("  c - Clear retained commands")
    print("  q - Quit")
    print("")

    commands = {
        '1': ({"mail_window": 240}, "mail_window = 240s (default)"),
        '2': ({"mail_window": 60}, "mail_window = 60s (quick test)"),
        '3': ({"activity_threshold": 250}, "activity_threshold = 250mg"),
        '4': ({"activity_threshold": 100}, "activity_threshold = 100mg"),
        '5': ({"status_request": True}, "Request status"),
        '6': ({"reset_config": True}, "Reset to defaults"),
    }

    while True:
        try:
            choice = input("\nCommand (1-6, c, q): ").strip().lower()

            if choice == 'q':
                break
            elif choice == 'c':
                tester.clear_retained()
            elif choice in commands:
                cmd, desc = commands[choice]
                tester.send_command(cmd, desc)
                tester.wait_for_response(timeout=5.0)
            else:
                print("Unknown option. Try 1-6, c, or q.")
        except KeyboardInterrupt:
            break


def main():
    parser = argparse.ArgumentParser(
        description="Test MQTT command handling for ALC Mailbox Monitor"
    )
    parser.add_argument(
        "--device-id",
        default=DEFAULT_DEVICE_ID,
        help=f"Device ID to target (default: {DEFAULT_DEVICE_ID})"
    )
    parser.add_argument(
        "--interactive", "-i",
        action="store_true",
        help="Run in interactive mode"
    )
    parser.add_argument(
        "--test", "-t",
        choices=["all", "mail_window", "thresholds", "status", "reset", "invalid"],
        default="all",
        help="Which test to run (default: all)"
    )
    args = parser.parse_args()

    print("="*60)
    print("ALC Mailbox Monitor - MQTT Command Test")
    print("="*60)
    print(f"Device ID: {args.device_id}")
    print(f"Broker: {BROKER_HOST}:{BROKER_PORT}")
    print("")

    tester = MqttCommandTester(args.device_id)

    if not tester.connect():
        print("[ERROR] Failed to connect to MQTT broker")
        print("Check your credentials and network connection")
        sys.exit(1)

    try:
        if args.interactive:
            run_interactive_mode(tester)
        else:
            tests = {
                "mail_window": test_mail_window,
                "thresholds": test_activity_thresholds,
                "status": test_status_request,
                "reset": test_reset_config,
                "invalid": test_invalid_commands,
            }

            if args.test == "all":
                for name, test_func in tests.items():
                    test_func(tester)
                    time.sleep(2)
            else:
                tests[args.test](tester)

            # Clear retained message after tests
            time.sleep(1)
            tester.clear_retained()

            # Summary
            print("\n" + "="*60)
            print("TEST SUMMARY")
            print("="*60)
            print(f"Responses received: {len(tester.responses_received)}")
            for resp in tester.responses_received:
                print(f"  - {resp['topic']}: {resp['payload'][:50]}...")

    except KeyboardInterrupt:
        print("\n[Interrupted by user]")

    finally:
        tester.clear_retained()
        tester.disconnect()


if __name__ == "__main__":
    main()
