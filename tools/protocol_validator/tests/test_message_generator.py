#!/usr/bin/env python3
"""
Test suite for protocol message generation.

Tests the MessageGenerator class for creating various test message patterns
including minimal, medium, maximum, and invalid message variations.

References: ADR-010 External Protocol Validation Tool
"""

import unittest
from tools.protocol_validator.protocol.message_generator import MessageGenerator
from tools.protocol_validator.protocol.formats import ProtocolMessage


class TestMessageGenerator(unittest.TestCase):
    """Test message generation for protocol validation."""

    def setUp(self):
        """Set up test fixture."""
        self.generator = MessageGenerator()

    def test_generate_minimal_messages(self):
        """Test generation of minimal valid messages."""
        messages = self.generator.generate_minimal_set()
        
        # Should generate messages from #0000!\r\n to #FFFF!\r\n
        self.assertGreater(len(messages), 0)
        
        # Check first and last in sequence
        first_msg = messages[0]
        self.assertEqual(first_msg.hex_header, "0000")
        self.assertEqual(first_msg.payload, "")
        self.assertTrue(first_msg.is_valid())
        
        # All messages should be valid and minimal
        for msg in messages:
            self.assertTrue(msg.is_valid())
            self.assertEqual(msg.payload, "")
            self.assertEqual(len(msg.to_wire_format()), 8)

    def test_generate_specific_hex_message(self):
        """Test generation of message with specific hex value."""
        hex_value = "ABCD"
        message = self.generator.generate_minimal_with_hex(hex_value)
        
        self.assertEqual(message.hex_header, hex_value)
        self.assertEqual(message.payload, "")
        self.assertTrue(message.is_valid())
        self.assertEqual(message.to_wire_format(), b"#ABCD!\r\n")

    def test_generate_medium_messages(self):
        """Test generation of messages with variable payload sizes."""
        payload_size = 100
        message = self.generator.generate_medium(payload_size)
        
        self.assertTrue(message.is_valid())
        # Total length should be payload_size + 8 (for '#XXXX!\r\n')
        expected_length = payload_size + 8
        self.assertEqual(len(message.to_wire_format()), expected_length)

    def test_generate_maximum_size_message(self):
        """Test generation of maximum valid message (1024 bytes)."""
        message = self.generator.generate_maximum()
        
        self.assertTrue(message.is_valid())
        self.assertEqual(len(message.to_wire_format()), 1024)

    def test_generate_random_size_messages(self):
        """Test generation of random size messages within valid range."""
        min_size = 8   # Minimum: '#XXXX!\r\n'
        max_size = 1024
        
        for _ in range(10):  # Test multiple random generations
            message = self.generator.generate_random_size(min_size, max_size)
            self.assertTrue(message.is_valid())
            msg_len = len(message.to_wire_format())
            self.assertGreaterEqual(msg_len, min_size)
            self.assertLessEqual(msg_len, max_size)

    def test_generate_invalid_messages(self):
        """Test generation of various invalid message types."""
        invalid_messages = self.generator.generate_invalid_set()
        
        self.assertGreater(len(invalid_messages), 0)
        
        # All messages should be invalid
        for msg in invalid_messages:
            self.assertFalse(msg.is_valid())

    def test_generate_stress_pattern(self):
        """Test generation of high-volume stress testing messages."""
        count = 1000
        messages = self.generator.generate_stress_pattern(count)
        
        self.assertEqual(len(messages), count)
        
        # All stress messages should be valid
        for msg in messages:
            self.assertTrue(msg.is_valid())

    def test_hex_value_coverage(self):
        """Test systematic coverage of hex value space."""
        # Test systematic sampling of hex space
        step = 0x1000  # Every 4096th value
        messages = self.generator.generate_hex_sampling(step)
        
        expected_count = (0xFFFF // step) + 1
        self.assertLessEqual(len(messages), expected_count + 1)  # Allow some variance
        
        # Verify hex values are properly distributed
        hex_values = [int(msg.hex_header, 16) for msg in messages]
        for i, hex_val in enumerate(hex_values[:-1]):
            next_val = hex_values[i + 1]
            self.assertLessEqual(next_val - hex_val, step * 2)  # Allow some variance

    def test_performance_pattern_generation(self):
        """Test generation of patterns optimized for performance testing."""
        # Generate messages for sustained throughput testing
        target_rate_kbps = 500
        duration_seconds = 1
        
        messages = self.generator.generate_performance_pattern(
            target_rate_kbps, duration_seconds
        )
        
        self.assertGreater(len(messages), 0)
        
        # Calculate total bytes and verify it's reasonable for the target rate
        total_bytes = sum(len(msg.to_wire_format()) for msg in messages)
        expected_bytes = (target_rate_kbps * 1024) * duration_seconds
        
        # Allow 20% variance for practical considerations
        self.assertGreater(total_bytes, expected_bytes * 0.8)
        self.assertLess(total_bytes, expected_bytes * 1.2)


if __name__ == '__main__':
    unittest.main()
