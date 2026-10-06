import importlib.util
import struct
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class MqttCodecTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # mqtt_bridge imports its sibling gateway_bridge when executed normally.
        import sys
        sys.path.insert(0, str(ROOT / "adapters"))
        cls.mqtt = load_module("mqtt_bridge", ROOT / "adapters" / "mqtt_bridge.py")

    def test_remaining_length_encoding(self):
        self.assertEqual(self.mqtt.remaining_length(0), b"\x00")
        self.assertEqual(self.mqtt.remaining_length(127), b"\x7f")
        self.assertEqual(self.mqtt.remaining_length(128), b"\x80\x01")
        self.assertEqual(self.mqtt.remaining_length(321), b"\xc1\x02")

    def test_publish_parser_qos_zero(self):
        topic = "industrial/device-42/telemetry"
        payload = '{"temperature":41.2}'
        body = self.mqtt.mqtt_string(topic) + payload.encode()
        parsed_topic, parsed_payload, qos, packet_id = self.mqtt.parse_publish(0x30, body)
        self.assertEqual(parsed_topic, topic)
        self.assertEqual(parsed_payload, payload)
        self.assertEqual(qos, 0)
        self.assertIsNone(packet_id)
        self.assertEqual(self.mqtt.device_from_topic(topic), "device-42")

    def test_publish_parser_qos_one(self):
        topic = "industrial/device-7/telemetry"
        body = self.mqtt.mqtt_string(topic) + struct.pack("!H", 99) + b"state=1"
        _, payload, qos, packet_id = self.mqtt.parse_publish(0x32, body)
        self.assertEqual(payload, "state=1")
        self.assertEqual(qos, 1)
        self.assertEqual(packet_id, 99)


if __name__ == "__main__":
    unittest.main()

