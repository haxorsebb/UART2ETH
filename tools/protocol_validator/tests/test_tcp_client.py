#!/usr/bin/env python3
"""
Test suite for TCP client functionality.

Tests the TCPClient class for connecting to UART2ETH device and
measuring round-trip times and throughput performance.

References: ADR-010 External Protocol Validation Tool
"""

import asyncio
import unittest
from unittest.mock import AsyncMock, patch, MagicMock
from tools.protocol_validator.networking.tcp_client import TCPClient
from tools.protocol_validator.protocol.formats import ProtocolMessage


class TestTCPClient(unittest.TestCase):
    """Test TCP client connection and performance measurement."""

    def setUp(self):
        """Set up test fixture."""
        self.client = TCPClient()
        self.test_host = "10.10.10.10"
        self.test_port = 4001

    def test_client_initialization(self):
        """Test TCP client initialization."""
        self.assertIsNotNone(self.client)
        self.assertIsNone(self.client.reader)
        self.assertIsNone(self.client.writer)
        self.assertFalse(self.client.is_connected())

    @patch('asyncio.open_connection')
    async def test_connect_success(self, mock_open_connection):
        """Test successful TCP connection."""
        # Mock successful connection
        mock_reader = AsyncMock()
        mock_writer = MagicMock()
        mock_writer.is_closing.return_value = False
        mock_open_connection.return_value = (mock_reader, mock_writer)
        
        result = await self.client.connect(self.test_host, self.test_port)
        
        self.assertTrue(result)
        self.assertTrue(self.client.is_connected())
        mock_open_connection.assert_called_once_with(self.test_host, self.test_port)

    @patch('asyncio.open_connection')
    async def test_connect_failure(self, mock_open_connection):
        """Test TCP connection failure."""
        # Mock connection failure
        mock_open_connection.side_effect = ConnectionRefusedError("Connection refused")
        
        result = await self.client.connect(self.test_host, self.test_port)
        
        self.assertFalse(result)
        self.assertFalse(self.client.is_connected())

    async def test_send_message_not_connected(self):
        """Test sending message when not connected."""
        message = ProtocolMessage(hex_header="1234", payload="test")
        
        with self.assertRaises(RuntimeError):
            await self.client.send_message(message)

    @patch('time.perf_counter')
    async def test_send_message_with_timing(self, mock_perf_counter):
        """Test message sending with round-trip time measurement."""
        # Setup mock connection
        mock_reader = AsyncMock()
        mock_writer = AsyncMock()
        mock_reader.readline.return_value = b"#1234test!\r\n"
        # Make is_closing synchronous but other methods async
        mock_writer.is_closing = MagicMock(return_value=False)
        
        self.client.reader = mock_reader
        self.client.writer = mock_writer
        
        # Mock timing
        mock_perf_counter.side_effect = [1.0, 1.005]  # 5ms round-trip
        
        message = ProtocolMessage(hex_header="1234", payload="test")
        rtt = await self.client.send_message(message)
        
        # Verify timing measurement
        self.assertAlmostEqual(rtt, 0.005, places=6)  # 5ms
        
        # Verify message was sent
        mock_writer.write.assert_called_once_with(b"#1234test!\r\n")
        mock_writer.drain.assert_called_once()
        mock_reader.readline.assert_called_once()

    async def test_send_message_timeout(self):
        """Test message timeout handling."""
        # Setup mock connection with timeout
        mock_reader = AsyncMock()
        mock_writer = AsyncMock()
        mock_reader.readline.side_effect = asyncio.TimeoutError()
        mock_writer.is_closing = MagicMock(return_value=False)
        
        self.client.reader = mock_reader
        self.client.writer = mock_writer
        
        message = ProtocolMessage(hex_header="1234", payload="test")
        
        with self.assertRaises(asyncio.TimeoutError):
            await self.client.send_message(message, timeout=1.0)

    async def test_disconnect(self):
        """Test proper connection cleanup."""
        # Setup mock connection
        mock_writer = AsyncMock()
        self.client.writer = mock_writer
        self.client.reader = AsyncMock()
        
        await self.client.disconnect()
        
        mock_writer.close.assert_called_once()
        mock_writer.wait_closed.assert_called_once()
        self.assertIsNone(self.client.reader)
        self.assertIsNone(self.client.writer)
        self.assertFalse(self.client.is_connected())

    async def test_connection_recovery(self):
        """Test automatic connection recovery."""
        # Setup initial connection
        with patch('asyncio.open_connection') as mock_open_connection:
            mock_reader = AsyncMock()
            mock_writer = MagicMock()
            mock_writer.is_closing.return_value = False
            mock_open_connection.return_value = (mock_reader, mock_writer)
            
            await self.client.connect(self.test_host, self.test_port)
            
            # Simulate connection failure
            mock_writer.write.side_effect = ConnectionResetError("Connection reset")
            
            message = ProtocolMessage(hex_header="1234", payload="test")
            
            # Should detect connection failure
            with self.assertRaises(ConnectionError):  # Should be ConnectionError, not ConnectionResetError
                await self.client.send_message(message)
            
            # Connection should be marked as disconnected
            self.assertFalse(self.client.is_connected())

    @patch('time.perf_counter')
    async def test_multiple_messages_performance(self, mock_perf_counter):
        """Test performance measurement with multiple messages."""
        # Setup mock connection
        mock_reader = AsyncMock()
        mock_writer = AsyncMock()
        mock_reader.readline.return_value = b"#1234test!\r\n"
        mock_writer.is_closing = MagicMock(return_value=False)
        
        self.client.reader = mock_reader
        self.client.writer = mock_writer
        
        # Mock different timing values for each message
        mock_perf_counter.side_effect = [1.0, 1.003, 1.003, 1.007, 1.007, 1.012]  # 3ms, 4ms, 5ms
        
        message = ProtocolMessage(hex_header="1234", payload="test")
        rtts = []
        
        for _ in range(3):
            rtt = await self.client.send_message(message)
            rtts.append(rtt)
        
        expected_rtts = [0.003, 0.004, 0.005]
        for actual, expected in zip(rtts, expected_rtts):
            self.assertAlmostEqual(actual, expected, places=6)

    def test_get_connection_info(self):
        """Test connection information retrieval."""
        # Test when not connected
        info = self.client.get_connection_info()
        self.assertIsNone(info['host'])
        self.assertIsNone(info['port'])
        self.assertFalse(info['connected'])
        
        # Test when connected
        self.client._host = self.test_host
        self.client._port = self.test_port
        self.client.reader = AsyncMock()
        
        # Create proper mock writer that behaves like StreamWriter
        mock_writer = MagicMock()
        mock_writer.is_closing.return_value = False
        self.client.writer = mock_writer
        
        info = self.client.get_connection_info()
        self.assertEqual(info['host'], self.test_host)
        self.assertEqual(info['port'], self.test_port)
        self.assertTrue(info['connected'])


class TestTCPClientIntegration(unittest.TestCase):
    """Integration tests for TCP client (require manual setup)."""
    
    def setUp(self):
        """Set up integration test fixture."""
        self.client = TCPClient()
    
    async def test_actual_connection_placeholder(self):
        """
        Placeholder for actual device connection test.
        
        This test requires the UART2ETH device to be available at 10.10.10.10:4001
        and should be run manually during integration testing.
        """
        # Skip this test in automated runs
        self.skipTest("Requires actual UART2ETH device - run manually for integration testing")
        
        # Uncomment for manual testing with actual device:
        # result = await self.client.connect("10.10.10.10", 4001)
        # self.assertTrue(result)
        # 
        # message = ProtocolMessage(hex_header="1234", payload="test")
        # rtt = await self.client.send_message(message)
        # self.assertGreater(rtt, 0)
        # self.assertLess(rtt, 0.1)  # Should be < 100ms
        # 
        # await self.client.disconnect()


# Helper to run async tests in unittest
def async_test(coro):
    def wrapper(self):
        return asyncio.run(coro(self))
    return wrapper

# Convert async test methods to sync wrappers
for name in dir(TestTCPClient):
    method = getattr(TestTCPClient, name)
    if name.startswith('test_') and asyncio.iscoroutinefunction(method):
        setattr(TestTCPClient, name, async_test(method))

for name in dir(TestTCPClientIntegration):
    method = getattr(TestTCPClientIntegration, name)
    if name.startswith('test_') and asyncio.iscoroutinefunction(method):
        setattr(TestTCPClientIntegration, name, async_test(method))

if __name__ == '__main__':
    unittest.main()
