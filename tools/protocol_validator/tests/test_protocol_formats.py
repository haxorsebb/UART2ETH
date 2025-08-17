#!/usr/bin/env python3
"""
Test suite for protocol message format validation.

Tests the UART2ETH protocol format: '#' + 4_hex_digits + payload + '!\r\n'
Validates character set restrictions, length limits, and format compliance.

References: ADR-010 External Protocol Validation Tool
"""

import unittest
from tools.protocol_validator.protocol.formats import ProtocolMessage


class TestProtocolMessage(unittest.TestCase):
    """Test protocol message format validation and generation."""

    def test_minimal_valid_message(self):
        """Test minimal valid message format: #0000!\r\n"""
        message = ProtocolMessage(hex_header="0000", payload="")
        
        self.assertTrue(message.is_valid())
        self.assertEqual(message.to_wire_format(), b"#0000!\r\n")
        self.assertEqual(len(message.to_wire_format()), 8)

    def test_valid_message_with_payload(self):
        """Test valid message with payload content."""
        message = ProtocolMessage(hex_header="1234", payload="Hello")
        
        self.assertTrue(message.is_valid())
        self.assertEqual(message.to_wire_format(), b"#1234Hello!\r\n")
        self.assertEqual(len(message.to_wire_format()), 13)

    def test_maximum_hex_values(self):
        """Test all valid hex digit combinations."""
        valid_hex_values = ["0000", "FFFF", "ABCD", "1234", "9999"]
        
        for hex_val in valid_hex_values:
            message = ProtocolMessage(hex_header=hex_val, payload="test")
            self.assertTrue(message.is_valid(), f"Hex value {hex_val} should be valid")
            expected = f"#{hex_val}test!\r\n".encode()
            self.assertEqual(message.to_wire_format(), expected)

    def test_invalid_hex_header_length(self):
        """Test invalid hex header lengths."""
        invalid_headers = ["123", "12345", "", "12"]
        
        for header in invalid_headers:
            message = ProtocolMessage(hex_header=header, payload="test")
            self.assertFalse(message.is_valid(), f"Header '{header}' should be invalid")

    def test_invalid_hex_characters(self):
        """Test invalid characters in hex header."""
        invalid_headers = ["123G", "12XY", "GGGG", "12!@"]
        
        for header in invalid_headers:
            message = ProtocolMessage(hex_header=header, payload="test")
            self.assertFalse(message.is_valid(), f"Header '{header}' should be invalid")

    def test_message_length_limits(self):
        """Test maximum message length enforcement (1024 bytes total)."""
        # Maximum valid payload: 1024 - 8 (for '#XXXX!\r\n') = 1016 bytes
        max_payload = "A" * 1016
        message = ProtocolMessage(hex_header="1234", payload=max_payload)
        
        self.assertTrue(message.is_valid())
        self.assertEqual(len(message.to_wire_format()), 1024)

    def test_message_too_long(self):
        """Test message exceeding 1024 byte limit."""
        # Payload too long: 1024 - 8 + 1 = 1017 bytes
        too_long_payload = "A" * 1017
        message = ProtocolMessage(hex_header="1234", payload=too_long_payload)
        
        self.assertFalse(message.is_valid())

    def test_invalid_payload_characters(self):
        """Test payload with invalid characters outside allowed set."""
        # According to ADR-010: Valid characters are '0'-'9', 'A'-'F', '#', '!', '\r', '\n'
        # Payload should not contain these special protocol characters
        invalid_payloads = ["test#", "test!", "test\r", "test\n", "test\r\n"]
        
        for payload in invalid_payloads:
            message = ProtocolMessage(hex_header="1234", payload=payload)
            self.assertFalse(message.is_valid(), f"Payload '{payload}' should be invalid")

    def test_wire_format_structure(self):
        """Test wire format structure matches specification."""
        message = ProtocolMessage(hex_header="ABCD", payload="TestData")
        wire_format = message.to_wire_format()
        
        # Verify structure: '#' + hex + payload + '!\r\n'
        self.assertTrue(wire_format.startswith(b"#"))
        self.assertTrue(wire_format.endswith(b"!\r\n"))
        self.assertEqual(wire_format[1:5], b"ABCD")
        self.assertEqual(wire_format[5:-3], b"TestData")


if __name__ == '__main__':
    unittest.main()
