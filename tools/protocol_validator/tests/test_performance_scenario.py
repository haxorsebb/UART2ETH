#!/usr/bin/env python3
"""
Test suite for performance test scenarios.

Tests the PerformanceTestScenario class for running end-to-end protocol
validation with integrated metrics collection and reporting.

References: ADR-010 External Protocol Validation Tool
"""

import unittest
import asyncio
from unittest.mock import AsyncMock, MagicMock, patch
from tools.protocol_validator.testing.performance_scenario import PerformanceTestScenario
from tools.protocol_validator.protocol.formats import ProtocolMessage


class TestPerformanceTestScenario(unittest.TestCase):
    """Test performance test scenario execution."""

    def setUp(self):
        """Set up test fixture."""
        self.scenario = PerformanceTestScenario()

    def test_scenario_initialization(self):
        """Test scenario initialization with default parameters."""
        self.assertIsNotNone(self.scenario.metrics_collector)
        self.assertIsNone(self.scenario.tcp_client)
        self.assertEqual(self.scenario.target_host, "10.10.10.10")
        self.assertEqual(self.scenario.target_ports, [4001, 4002, 4003, 4004])
        self.assertEqual(self.scenario.test_duration, 60)

    def test_scenario_configuration(self):
        """Test scenario configuration with custom parameters."""
        custom_scenario = PerformanceTestScenario(
            target_host="192.168.1.100",
            target_ports=[8001, 8002],
            test_duration=30,
            target_throughput_kbps=1000
        )
        
        self.assertEqual(custom_scenario.target_host, "192.168.1.100")
        self.assertEqual(custom_scenario.target_ports, [8001, 8002])
        self.assertEqual(custom_scenario.test_duration, 30)
        self.assertEqual(custom_scenario.target_throughput_kbps, 1000)

    @patch('tools.protocol_validator.testing.performance_scenario.TCPClient')
    async def test_connect_to_device_success(self, mock_tcp_client_class):
        """Test successful connection to UART2ETH device."""
        # Setup mock
        mock_client = AsyncMock()
        mock_client.connect.return_value = True
        mock_tcp_client_class.return_value = mock_client
        
        result = await self.scenario.connect_to_device(4001)
        
        self.assertTrue(result)
        mock_client.connect.assert_called_once_with("10.10.10.10", 4001)

    @patch('tools.protocol_validator.testing.performance_scenario.TCPClient')
    async def test_connect_to_device_failure(self, mock_tcp_client_class):
        """Test connection failure to UART2ETH device."""
        # Setup mock
        mock_client = AsyncMock()
        mock_client.connect.return_value = False
        mock_tcp_client_class.return_value = mock_client
        
        result = await self.scenario.connect_to_device(4001)
        
        self.assertFalse(result)

    async def test_send_test_message(self):
        """Test sending a single test message."""
        # Setup mock client
        mock_client = AsyncMock()
        mock_client.send_message.return_value = 0.005  # 5ms RTT
        self.scenario.tcp_client = mock_client
        
        message = ProtocolMessage(hex_header="1234", payload="test")
        
        result = await self.scenario.send_test_message(message, port=4001)
        
        self.assertTrue(result)
        mock_client.send_message.assert_called_once_with(message)

    async def test_send_test_message_failure(self):
        """Test handling of message send failure."""
        # Setup mock client with failure
        mock_client = AsyncMock()
        mock_client.send_message.side_effect = ConnectionError("Connection lost")
        self.scenario.tcp_client = mock_client
        
        message = ProtocolMessage(hex_header="1234", payload="test")
        
        result = await self.scenario.send_test_message(message, port=4001)
        
        self.assertFalse(result)

    async def test_run_compliance_test(self):
        """Test running compliance test scenario."""
        # Setup mocks
        mock_client = AsyncMock()
        mock_client.send_message.return_value = 0.003
        self.scenario.tcp_client = mock_client
        
        results = await self.scenario.run_compliance_test(message_count=10)
        
        self.assertIsNotNone(results)
        self.assertIn('total_messages', results)
        self.assertIn('successful_messages', results)
        self.assertIn('failed_messages', results)
        self.assertIn('compliance_rate', results)
        
        # Should have attempted to send messages
        self.assertGreater(mock_client.send_message.call_count, 0)

    async def test_run_performance_test(self):
        """Test running performance test scenario."""
        # Setup mocks
        mock_client = AsyncMock()
        mock_client.send_message.return_value = 0.004
        self.scenario.tcp_client = mock_client
        
        results = await self.scenario.run_performance_test(duration=1)  # 1 second test
        
        self.assertIsNotNone(results)
        self.assertIn('test_duration', results)
        self.assertIn('messages_sent', results)
        self.assertIn('average_throughput_kbps', results)
        self.assertIn('average_latency_ms', results)
        self.assertIn('latency_percentiles', results)

    async def test_run_stress_test(self):
        """Test running stress test scenario."""
        # Setup mocks
        mock_client = AsyncMock()
        mock_client.send_message.return_value = 0.006
        self.scenario.tcp_client = mock_client
        
        results = await self.scenario.run_stress_test(
            duration=1,
            concurrent_connections=2,
            message_rate=100
        )
        
        self.assertIsNotNone(results)
        self.assertIn('stress_duration', results)
        self.assertIn('concurrent_connections', results)
        self.assertIn('total_messages', results)
        self.assertIn('connection_failures', results)

    def test_get_test_summary(self):
        """Test getting test results summary."""
        # Add some test data to metrics
        self.scenario.metrics_collector.start_collection()
        self.scenario.metrics_collector.record_message(0.005, 64, 4001)
        self.scenario.metrics_collector.record_message(0.003, 128, 4001)
        
        summary = self.scenario.get_test_summary()
        
        self.assertIsNotNone(summary)
        self.assertIn('overall_metrics', summary)
        self.assertIn('port_metrics', summary)
        self.assertIn('test_configuration', summary)

    def test_reset_scenario(self):
        """Test resetting scenario state."""
        # Add some test data
        self.scenario.metrics_collector.start_collection()
        self.scenario.metrics_collector.record_message(0.005, 64, 4001)
        
        # Reset
        self.scenario.reset()
        
        # Verify metrics are cleared
        metrics = self.scenario.metrics_collector.get_overall_metrics()
        self.assertEqual(metrics.messages_sent, 0)

    async def test_end_to_end_performance_test(self):
        """Test complete end-to-end performance test workflow."""
        # Setup mocks for complete workflow
        with patch('tools.protocol_validator.testing.performance_scenario.TCPClient') as mock_tcp_class:
            mock_client = AsyncMock()
            mock_client.connect.return_value = True
            mock_client.send_message.return_value = 0.004
            mock_client.disconnect.return_value = None
            mock_tcp_class.return_value = mock_client
            
            # Run complete test
            connected = await self.scenario.connect_to_device(4001)
            self.assertTrue(connected)
            
            # Start metrics collection
            self.scenario.metrics_collector.start_collection()
            
            # Run performance test
            results = await self.scenario.run_performance_test(duration=0.1)  # Very short test
            
            # Get summary
            summary = self.scenario.get_test_summary()
            
            # Verify results
            self.assertIsNotNone(results)
            self.assertIsNotNone(summary)
            self.assertGreater(results['messages_sent'], 0)

    def test_validate_configuration(self):
        """Test configuration validation."""
        # Valid configuration
        self.assertTrue(self.scenario.validate_configuration())
        
        # Invalid configuration
        invalid_scenario = PerformanceTestScenario(
            target_host="",  # Invalid empty host
            target_ports=[],  # Invalid empty ports
            test_duration=0   # Invalid zero duration
        )
        
        self.assertFalse(invalid_scenario.validate_configuration())

    def test_estimate_test_messages(self):
        """Test estimation of messages for performance test."""
        # Test with known parameters
        estimated = self.scenario.estimate_test_messages(
            duration=60,
            target_throughput_kbps=500,
            average_message_size=256
        )
        
        self.assertGreater(estimated, 0)
        self.assertIsInstance(estimated, int)


# Helper to run async tests in unittest
def async_test(coro):
    def wrapper(self):
        return asyncio.run(coro(self))
    return wrapper

# Convert async test methods to sync wrappers
for name in dir(TestPerformanceTestScenario):
    method = getattr(TestPerformanceTestScenario, name)
    if name.startswith('test_') and asyncio.iscoroutinefunction(method):
        setattr(TestPerformanceTestScenario, name, async_test(method))

if __name__ == '__main__':
    unittest.main()
