#!/usr/bin/env python3
"""
Test suite for performance metrics collection.

Tests the MetricsCollector class for gathering performance data including
round-trip times, throughput, message counts, and statistical analysis.

References: ADR-010 External Protocol Validation Tool
"""

import unittest
import time
from tools.protocol_validator.statistics.metrics_collector import MetricsCollector, PerformanceMetrics


class TestPerformanceMetrics(unittest.TestCase):
    """Test performance metrics data structure."""

    def test_metrics_initialization(self):
        """Test performance metrics initialization."""
        metrics = PerformanceMetrics()
        
        self.assertEqual(metrics.messages_sent, 0)
        self.assertEqual(metrics.messages_received, 0)
        self.assertEqual(metrics.total_bytes, 0)
        self.assertEqual(len(metrics.round_trip_times), 0)
        self.assertIsInstance(metrics.start_time, float)
        self.assertGreater(metrics.start_time, 0)

    def test_add_measurement(self):
        """Test adding measurement data."""
        metrics = PerformanceMetrics()
        
        metrics.add_measurement(rtt=0.005, message_size=64)
        
        self.assertEqual(metrics.messages_sent, 1)
        self.assertEqual(metrics.total_bytes, 64)
        self.assertEqual(len(metrics.round_trip_times), 1)
        self.assertEqual(metrics.round_trip_times[0], 0.005)

    def test_add_multiple_measurements(self):
        """Test adding multiple measurements."""
        metrics = PerformanceMetrics()
        
        measurements = [(0.003, 32), (0.004, 64), (0.006, 128)]
        for rtt, size in measurements:
            metrics.add_measurement(rtt, size)
        
        self.assertEqual(metrics.messages_sent, 3)
        self.assertEqual(metrics.total_bytes, 224)  # 32 + 64 + 128
        self.assertEqual(len(metrics.round_trip_times), 3)

    def test_throughput_calculation(self):
        """Test throughput calculation in kbytes/s."""
        metrics = PerformanceMetrics()
        
        # Set a known start time
        metrics.start_time = time.time() - 1.0  # 1 second ago
        metrics.total_bytes = 1024  # 1 KB
        
        throughput = metrics.get_throughput_kbps()
        
        # Should be approximately 1 kbyte/s
        self.assertAlmostEqual(throughput, 1.0, places=1)

    def test_latency_percentiles_empty(self):
        """Test latency percentiles with no data."""
        metrics = PerformanceMetrics()
        
        percentiles = metrics.get_latency_percentiles()
        
        self.assertEqual(percentiles, {})

    def test_latency_percentiles_calculation(self):
        """Test latency percentiles calculation."""
        metrics = PerformanceMetrics()
        
        # Add test data: 1ms, 2ms, 3ms, 4ms, 5ms
        test_rtts = [0.001, 0.002, 0.003, 0.004, 0.005]
        for rtt in test_rtts:
            metrics.add_measurement(rtt, 64)
        
        percentiles = metrics.get_latency_percentiles()
        
        self.assertIn('min', percentiles)
        self.assertIn('median', percentiles)
        self.assertIn('p95', percentiles)
        self.assertIn('p99', percentiles)
        self.assertIn('max', percentiles)
        
        self.assertEqual(percentiles['min'], 0.001)
        self.assertEqual(percentiles['max'], 0.005)
        self.assertEqual(percentiles['median'], 0.003)


class TestMetricsCollector(unittest.TestCase):
    """Test metrics collection and aggregation."""

    def setUp(self):
        """Set up test fixture."""
        self.collector = MetricsCollector()

    def test_collector_initialization(self):
        """Test metrics collector initialization."""
        self.assertIsNotNone(self.collector.overall_metrics)
        self.assertEqual(len(self.collector.port_metrics), 0)
        self.assertFalse(self.collector.is_collecting())

    def test_start_collection(self):
        """Test starting metrics collection."""
        self.collector.start_collection()
        
        self.assertTrue(self.collector.is_collecting())

    def test_stop_collection(self):
        """Test stopping metrics collection."""
        self.collector.start_collection()
        self.collector.stop_collection()
        
        self.assertFalse(self.collector.is_collecting())

    def test_record_message_overall(self):
        """Test recording message to overall metrics."""
        self.collector.start_collection()
        
        self.collector.record_message(
            rtt=0.005,
            message_size=64,
            port=None  # Overall metrics
        )
        
        metrics = self.collector.get_overall_metrics()
        self.assertEqual(metrics.messages_sent, 1)
        self.assertEqual(metrics.total_bytes, 64)

    def test_record_message_by_port(self):
        """Test recording message metrics by port."""
        self.collector.start_collection()
        
        self.collector.record_message(
            rtt=0.004,
            message_size=32,
            port=4001
        )
        
        port_metrics = self.collector.get_port_metrics(4001)
        self.assertIsNotNone(port_metrics)
        self.assertEqual(port_metrics.messages_sent, 1)
        self.assertEqual(port_metrics.total_bytes, 32)

    def test_multiple_ports_tracking(self):
        """Test tracking metrics for multiple ports."""
        self.collector.start_collection()
        
        # Record for different ports
        self.collector.record_message(0.003, 64, 4001)
        self.collector.record_message(0.004, 128, 4002)
        self.collector.record_message(0.005, 256, 4001)  # Second message to port 4001
        
        port1_metrics = self.collector.get_port_metrics(4001)
        port2_metrics = self.collector.get_port_metrics(4002)
        
        # Port 4001 should have 2 messages
        self.assertEqual(port1_metrics.messages_sent, 2)
        self.assertEqual(port1_metrics.total_bytes, 320)  # 64 + 256
        
        # Port 4002 should have 1 message
        self.assertEqual(port2_metrics.messages_sent, 1)
        self.assertEqual(port2_metrics.total_bytes, 128)

    def test_get_summary_statistics(self):
        """Test getting summary statistics."""
        self.collector.start_collection()
        
        # Add some test data
        test_data = [
            (0.001, 32, 4001),
            (0.002, 64, 4001),
            (0.003, 128, 4002),
            (0.004, 256, 4002)
        ]
        
        for rtt, size, port in test_data:
            self.collector.record_message(rtt, size, port)
        
        summary = self.collector.get_summary_statistics()
        
        self.assertIn('overall', summary)
        self.assertIn('by_port', summary)
        self.assertIn('collection_active', summary)
        
        # Check overall stats
        overall = summary['overall']
        self.assertEqual(overall['messages_sent'], 4)
        self.assertEqual(overall['total_bytes'], 480)  # 32+64+128+256
        
        # Check per-port stats
        by_port = summary['by_port']
        self.assertIn(4001, by_port)
        self.assertIn(4002, by_port)

    def test_reset_metrics(self):
        """Test resetting all metrics."""
        self.collector.start_collection()
        
        # Add some data
        self.collector.record_message(0.005, 64, 4001)
        
        # Verify data exists
        self.assertEqual(self.collector.get_overall_metrics().messages_sent, 1)
        
        # Reset
        self.collector.reset_metrics()
        
        # Verify data is cleared
        self.assertEqual(self.collector.get_overall_metrics().messages_sent, 0)
        self.assertEqual(len(self.collector.port_metrics), 0)

    def test_record_when_not_collecting(self):
        """Test recording message when not actively collecting."""
        # Don't start collection
        result = self.collector.record_message(0.005, 64, 4001)
        
        # Should return False or handle gracefully
        self.assertFalse(result)
        self.assertEqual(self.collector.get_overall_metrics().messages_sent, 0)

    def test_get_port_list(self):
        """Test getting list of ports being tracked."""
        self.collector.start_collection()
        
        # Add data for multiple ports
        self.collector.record_message(0.003, 32, 4001)
        self.collector.record_message(0.004, 64, 4002)
        self.collector.record_message(0.005, 96, 4003)
        
        ports = self.collector.get_tracked_ports()
        
        self.assertEqual(set(ports), {4001, 4002, 4003})

    def test_real_time_throughput_calculation(self):
        """Test real-time throughput calculation."""
        self.collector.start_collection()
        
        # Simulate time passing
        start_time = time.time()
        self.collector.overall_metrics.start_time = start_time - 2.0  # 2 seconds ago
        
        # Add 2048 bytes of data
        self.collector.overall_metrics.total_bytes = 2048
        
        throughput = self.collector.get_current_throughput_kbps()
        
        # Should be approximately 1 kbps (2048 bytes / 2 seconds / 1024)
        self.assertAlmostEqual(throughput, 1.0, places=1)


if __name__ == '__main__':
    unittest.main()
