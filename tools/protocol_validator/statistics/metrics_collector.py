#!/usr/bin/env python3
"""
Performance metrics collection for UART2ETH protocol validation.

Provides real-time collection and analysis of performance metrics including
round-trip times, throughput, message counts, and statistical analysis.

References: ADR-010 External Protocol Validation Tool
"""

import time
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Union


@dataclass
class PerformanceMetrics:
    """
    Container for performance measurement data.
    
    Tracks message counts, timing data, and provides statistical analysis
    for protocol validation testing.
    """
    
    messages_sent: int = 0
    messages_received: int = 0
    total_bytes: int = 0
    round_trip_times: List[float] = field(default_factory=list)
    start_time: float = field(default_factory=time.time)
    
    def add_measurement(self, rtt: float, message_size: int) -> None:
        """
        Add a performance measurement.
        
        Args:
            rtt: Round-trip time in seconds
            message_size: Message size in bytes
        """
        self.round_trip_times.append(rtt)
        self.total_bytes += message_size
        self.messages_sent += 1
        # Assume message received if we have RTT
        self.messages_received += 1
    
    def get_throughput_kbps(self) -> float:
        """
        Calculate throughput in kilobytes per second.
        
        Returns:
            float: Throughput in kbytes/s
        """
        elapsed = time.time() - self.start_time
        if elapsed <= 0:
            return 0.0
        
        return (self.total_bytes / 1024) / elapsed
    
    def get_latency_percentiles(self) -> Dict[str, float]:
        """
        Calculate latency percentile statistics.
        
        Returns:
            Dict: Percentile statistics (min, median, p95, p99, max)
        """
        if not self.round_trip_times:
            return {}
        
        sorted_rtts = sorted(self.round_trip_times)
        n = len(sorted_rtts)
        
        return {
            'min': sorted_rtts[0],
            'median': sorted_rtts[n // 2],
            'p95': sorted_rtts[int(n * 0.95)] if n > 0 else 0,
            'p99': sorted_rtts[int(n * 0.99)] if n > 0 else 0,
            'max': sorted_rtts[-1]
        }
    
    def get_average_latency(self) -> float:
        """Calculate average round-trip time."""
        if not self.round_trip_times:
            return 0.0
        return sum(self.round_trip_times) / len(self.round_trip_times)


class MetricsCollector:
    """
    Collects and aggregates performance metrics for protocol validation.
    
    Provides real-time metrics collection with per-port tracking and
    overall system statistics for comprehensive performance analysis.
    """
    
    def __init__(self):
        """Initialize metrics collector."""
        self.overall_metrics = PerformanceMetrics()
        self.port_metrics: Dict[int, PerformanceMetrics] = {}
        self._collecting = False
    
    def start_collection(self) -> None:
        """Start metrics collection."""
        self._collecting = True
        # Reset start time to current time
        self.overall_metrics.start_time = time.time()
    
    def stop_collection(self) -> None:
        """Stop metrics collection."""
        self._collecting = False
    
    def is_collecting(self) -> bool:
        """Check if actively collecting metrics."""
        return self._collecting
    
    def record_message(self, rtt: float, message_size: int, port: Optional[int] = None) -> bool:
        """
        Record a message measurement.
        
        Args:
            rtt: Round-trip time in seconds
            message_size: Message size in bytes
            port: Optional port number for per-port tracking
            
        Returns:
            bool: True if recorded successfully, False if not collecting
        """
        if not self._collecting:
            return False
        
        # Record to overall metrics
        self.overall_metrics.add_measurement(rtt, message_size)
        
        # Record to port-specific metrics if port specified
        if port is not None:
            if port not in self.port_metrics:
                self.port_metrics[port] = PerformanceMetrics()
                # Sync start time with overall metrics
                self.port_metrics[port].start_time = self.overall_metrics.start_time
            
            self.port_metrics[port].add_measurement(rtt, message_size)
        
        return True
    
    def get_overall_metrics(self) -> PerformanceMetrics:
        """Get overall performance metrics."""
        return self.overall_metrics
    
    def get_port_metrics(self, port: int) -> Optional[PerformanceMetrics]:
        """
        Get performance metrics for specific port.
        
        Args:
            port: Port number
            
        Returns:
            PerformanceMetrics: Port-specific metrics, or None if port not tracked
        """
        return self.port_metrics.get(port)
    
    def get_tracked_ports(self) -> List[int]:
        """Get list of ports being tracked."""
        return list(self.port_metrics.keys())
    
    def get_current_throughput_kbps(self) -> float:
        """Get current overall throughput in kbytes/s."""
        return self.overall_metrics.get_throughput_kbps()
    
    def reset_metrics(self) -> None:
        """Reset all collected metrics."""
        self.overall_metrics = PerformanceMetrics()
        self.port_metrics.clear()
        
        # If we're currently collecting, reset start time
        if self._collecting:
            self.overall_metrics.start_time = time.time()
    
    def get_summary_statistics(self) -> Dict[str, Union[Dict, bool, int, float]]:
        """
        Get comprehensive summary statistics.
        
        Returns:
            Dict: Complete statistics summary including overall and per-port data
        """
        summary = {
            'collection_active': self._collecting,
            'overall': self._get_metrics_summary(self.overall_metrics),
            'by_port': {}
        }
        
        # Add per-port summaries
        for port, metrics in self.port_metrics.items():
            summary['by_port'][port] = self._get_metrics_summary(metrics)
        
        return summary
    
    def _get_metrics_summary(self, metrics: PerformanceMetrics) -> Dict[str, Union[int, float, Dict]]:
        """
        Generate summary for a metrics object.
        
        Args:
            metrics: PerformanceMetrics instance
            
        Returns:
            Dict: Summary statistics
        """
        return {
            'messages_sent': metrics.messages_sent,
            'messages_received': metrics.messages_received,
            'total_bytes': metrics.total_bytes,
            'throughput_kbps': metrics.get_throughput_kbps(),
            'average_latency_ms': metrics.get_average_latency() * 1000,  # Convert to ms
            'latency_percentiles_ms': {
                k: v * 1000 for k, v in metrics.get_latency_percentiles().items()
            },
            'test_duration_seconds': time.time() - metrics.start_time
        }
