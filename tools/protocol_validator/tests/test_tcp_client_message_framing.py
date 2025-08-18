#!/usr/bin/env python3
"""
Test TCP client message framing for proper protocol handling.

Tests that the TCP client correctly handles message delimiting by:
- 1024 byte limit
- '!\r\n' terminator  
- 20ms timeout

References: ADR-010 Protocol Validation Tool message framing requirements
"""

import asyncio
import unittest
import time
from unittest.mock import AsyncMock, MagicMock
from ..networking.tcp_client import TCPClient
from ..protocol.formats import ProtocolMessage


class TestTCPClientMessageFraming(unittest.TestCase):
    """Test TCP client message framing according to protocol specification."""

    def setUp(self):
        """Set up test fixture."""
        self.client = TCPClient()

    async def test_message_framing_with_terminator(self):
        """Test message correctly terminated with !\r\n"""
        mock_reader = AsyncMock()
        mock_writer = AsyncMock()
        
        # Test message with proper terminator
        test_message = ProtocolMessage("1234", "test")
        expected_response = b"#1234test!\r\n"
        
        # Mock response with proper terminator - should use read() not readline()
        mock_reader.read.return_value = expected_response
        mock_writer.is_closing = MagicMock(return_value=False)
        
        self.client.reader = mock_reader
        self.client.writer = mock_writer
        
        # Send message and measure response handling
        start_time = time.perf_counter()
        rtt = await self.client.send_message(test_message, timeout=0.02)  # 20ms timeout
        end_time = time.perf_counter()
        
        # Should complete quickly with proper terminator
        self.assertGreater(rtt, 0)
        self.assertLess((end_time - start_time), 0.1)  # Should not timeout

    async def test_message_framing_with_1024_byte_limit(self):
        """Test message framing respects 1024 byte limit"""
        mock_reader = AsyncMock()
        mock_writer = AsyncMock()
        
        # Test message that hits 1024 byte limit (no terminator)
        large_payload = "A" * 1016  # 1016 + 8 protocol bytes = 1024
        test_message = ProtocolMessage("FFFF", large_payload)
        
        # Mock response exactly 1024 bytes (no terminator)
        expected_response = test_message.to_wire_format()[:1024]
        mock_reader.read.return_value = expected_response
        mock_writer.is_closing = MagicMock(return_value=False)
        
        self.client.reader = mock_reader 
        self.client.writer = mock_writer
        
        rtt = await self.client.send_message(test_message, timeout=0.02)
        
        self.assertGreater(rtt, 0)
        # Should have called read (not readline)
        mock_reader.read.assert_called()

    async def test_proper_message_framing_now_used(self):
        """Test that proper message framing is now used"""
        mock_reader = AsyncMock()
        mock_writer = AsyncMock()
        
        # Server sends complete message with !\r\n terminator
        server_response = b"#1234test!\r\n"
        
        # Mock byte-by-byte reading for protocol framing
        response_bytes = [bytes([b]) for b in server_response] + [b""]
        mock_reader.read.side_effect = response_bytes
        mock_writer.is_closing = MagicMock(return_value=False)
        
        self.client.reader = mock_reader
        self.client.writer = mock_writer
        
        test_message = ProtocolMessage("1234", "test")
        
        # Now uses proper protocol framing with check_message_end() logic
        rtt = await self.client.send_message(test_message)
        
        self.assertGreater(rtt, 0)
        # Should use read() method for byte-by-byte protocol parsing
        self.assertGreater(mock_reader.read.call_count, 0)

    async def test_protocol_compliance_performance(self):
        """Test that message processing is fast enough for performance requirements"""
        mock_reader = AsyncMock() 
        mock_writer = AsyncMock()
        
        # Fast response simulation
        mock_reader.read.return_value = b"#1234test!\r\n"
        mock_writer.is_closing = MagicMock(return_value=False)
        
        self.client.reader = mock_reader
        self.client.writer = mock_writer
        
        test_message = ProtocolMessage("1234", "test")
        
        # Measure multiple message processing speed
        start_time = time.perf_counter()
        
        for _ in range(10):
            await self.client.send_message(test_message, timeout=0.02)
            
        end_time = time.perf_counter()
        
        # Should process 10 messages very quickly (< 100ms total)
        total_time = end_time - start_time
        self.assertLess(total_time, 0.1, f"Too slow: {total_time}s for 10 messages")


# Helper to run async tests in unittest
def async_test(coro):
    def wrapper(self):
        return asyncio.run(coro(self))
    return wrapper

# Convert async test methods to sync wrappers
for name in dir(TestTCPClientMessageFraming):
    method = getattr(TestTCPClientMessageFraming, name)
    if name.startswith('test_') and asyncio.iscoroutinefunction(method):
        setattr(TestTCPClientMessageFraming, name, async_test(method))


if __name__ == '__main__':
    unittest.main()
