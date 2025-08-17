#!/usr/bin/env python3
"""
Performance test scenarios for UART2ETH protocol validation.

Implements comprehensive test scenarios that integrate protocol validation,
message generation, TCP communication, and metrics collection for 
end-to-end performance testing.

References: ADR-010 External Protocol Validation Tool
"""

import asyncio
import time
from typing import Dict, List, Optional, Union, Any
from ..networking.tcp_client import TCPClient
from ..protocol.message_generator import MessageGenerator
from ..protocol.formats import ProtocolMessage
from ..statistics.metrics_collector import MetricsCollector


class PerformanceTestScenario:
    """
    Orchestrates end-to-end performance testing scenarios.
    
    Integrates all protocol validation components to provide comprehensive
    testing including compliance validation, performance measurement,
    and stress testing capabilities.
    """
    
    def __init__(
        self,
        target_host: str = "10.10.10.10",
        target_ports: List[int] = None,
        test_duration: int = 60,
        target_throughput_kbps: int = 500
    ):
        """
        Initialize performance test scenario.
        
        Args:
            target_host: UART2ETH device IP address
            target_ports: List of ports to test (defaults to 4001-4004)
            test_duration: Default test duration in seconds
            target_throughput_kbps: Target throughput for performance tests
        """
        self.target_host = target_host
        self.target_ports = target_ports or [4001, 4002, 4003, 4004]
        self.test_duration = test_duration
        self.target_throughput_kbps = target_throughput_kbps
        
        # Initialize components
        self.metrics_collector = MetricsCollector()
        self.message_generator = MessageGenerator()
        self.tcp_client: Optional[TCPClient] = None
    
    async def connect_to_device(self, port: int) -> bool:
        """
        Establish connection to UART2ETH device.
        
        Args:
            port: Target port number
            
        Returns:
            bool: True if connection successful
        """
        self.tcp_client = TCPClient()
        return await self.tcp_client.connect(self.target_host, port)
    
    async def disconnect_from_device(self) -> None:
        """Disconnect from UART2ETH device."""
        if self.tcp_client:
            await self.tcp_client.disconnect()
            self.tcp_client = None
    
    async def send_test_message(self, message: ProtocolMessage, port: int) -> bool:
        """
        Send a test message and record metrics.
        
        Args:
            message: Protocol message to send
            port: Port number for metrics tracking
            
        Returns:
            bool: True if message sent successfully
        """
        if not self.tcp_client:
            return False
        
        try:
            rtt = await self.tcp_client.send_message(message)
            message_size = len(message.to_wire_format())
            
            # Record metrics
            self.metrics_collector.record_message(rtt, message_size, port)
            
            return True
        except Exception:
            return False
    
    async def run_compliance_test(self, message_count: int = 100) -> Dict[str, Any]:
        """
        Run protocol compliance test scenario.
        
        Tests various message formats to ensure protocol compliance.
        
        Args:
            message_count: Number of test messages to send
            
        Returns:
            Dict: Compliance test results
        """
        if not self.tcp_client:
            raise RuntimeError("Not connected to device")
        
        # Generate test messages (mix of valid and invalid)
        test_messages = []
        
        # Add valid minimal messages
        minimal_messages = self.message_generator.generate_minimal_set()
        test_messages.extend(minimal_messages[:message_count // 2])
        
        # Add medium-sized messages
        for _ in range(message_count // 4):
            test_messages.append(self.message_generator.generate_medium(128))
        
        # Add maximum-size messages
        for _ in range(message_count // 4):
            test_messages.append(self.message_generator.generate_maximum())
        
        # Run tests
        successful_messages = 0
        failed_messages = 0
        
        for message in test_messages:
            if message.is_valid():
                success = await self.send_test_message(message, self.target_ports[0])
                if success:
                    successful_messages += 1
                else:
                    failed_messages += 1
            else:
                # Invalid messages should be rejected
                failed_messages += 1
        
        total_messages = len(test_messages)
        compliance_rate = (successful_messages / total_messages) * 100 if total_messages > 0 else 0
        
        return {
            'total_messages': total_messages,
            'successful_messages': successful_messages,
            'failed_messages': failed_messages,
            'compliance_rate': compliance_rate
        }
    
    async def run_performance_test(self, duration: int = None) -> Dict[str, Any]:
        """
        Run performance measurement test scenario.
        
        Measures sustained throughput and latency characteristics.
        
        Args:
            duration: Test duration in seconds (uses default if None)
            
        Returns:
            Dict: Performance test results
        """
        if not self.tcp_client:
            raise RuntimeError("Not connected to device")
        
        test_duration = duration or self.test_duration
        
        # Start metrics collection
        self.metrics_collector.start_collection()
        
        # Generate performance-optimized messages
        messages = self.message_generator.generate_performance_pattern(
            self.target_throughput_kbps, test_duration
        )
        
        start_time = time.time()
        messages_sent = 0
        
        # Send messages for the specified duration
        for message in messages:
            current_time = time.time()
            if current_time - start_time >= test_duration:
                break
                
            await self.send_test_message(message, self.target_ports[0])
            messages_sent += 1
            
            # Small delay to control rate
            await asyncio.sleep(0.001)
        
        # Stop metrics collection
        self.metrics_collector.stop_collection()
        
        # Calculate results
        overall_metrics = self.metrics_collector.get_overall_metrics()
        latency_percentiles = overall_metrics.get_latency_percentiles()
        
        return {
            'test_duration': test_duration,
            'messages_sent': messages_sent,
            'average_throughput_kbps': overall_metrics.get_throughput_kbps(),
            'average_latency_ms': overall_metrics.get_average_latency() * 1000,
            'latency_percentiles': {k: v * 1000 for k, v in latency_percentiles.items()}
        }
    
    async def run_stress_test(
        self,
        duration: int = 300,
        concurrent_connections: int = 1,
        message_rate: int = 1000
    ) -> Dict[str, Any]:
        """
        Run stress test scenario.
        
        Tests system behavior under high load conditions.
        
        Args:
            duration: Test duration in seconds
            concurrent_connections: Number of concurrent connections
            message_rate: Messages per second to attempt
            
        Returns:
            Dict: Stress test results
        """
        if not self.tcp_client:
            raise RuntimeError("Not connected to device")
        
        # Start metrics collection
        self.metrics_collector.start_collection()
        
        # Generate stress test messages
        total_messages = duration * message_rate
        stress_messages = self.message_generator.generate_stress_pattern(total_messages)
        
        start_time = time.time()
        messages_sent = 0
        connection_failures = 0
        
        # Send messages at target rate
        for message in stress_messages:
            current_time = time.time()
            if current_time - start_time >= duration:
                break
            
            try:
                success = await self.send_test_message(message, self.target_ports[0])
                if success:
                    messages_sent += 1
                else:
                    connection_failures += 1
            except Exception:
                connection_failures += 1
            
            # Control message rate
            target_delay = 1.0 / message_rate
            await asyncio.sleep(target_delay)
        
        self.metrics_collector.stop_collection()
        
        return {
            'stress_duration': duration,
            'concurrent_connections': concurrent_connections,
            'target_message_rate': message_rate,
            'total_messages': messages_sent,
            'connection_failures': connection_failures,
            'actual_message_rate': messages_sent / duration if duration > 0 else 0
        }
    
    def get_test_summary(self) -> Dict[str, Any]:
        """
        Get comprehensive test results summary.
        
        Returns:
            Dict: Complete test summary with metrics and configuration
        """
        summary = self.metrics_collector.get_summary_statistics()
        
        # Add configuration information
        summary['test_configuration'] = {
            'target_host': self.target_host,
            'target_ports': self.target_ports,
            'test_duration': self.test_duration,
            'target_throughput_kbps': self.target_throughput_kbps
        }
        
        # Reorganize for clarity
        return {
            'overall_metrics': summary['overall'],
            'port_metrics': summary['by_port'],
            'test_configuration': summary['test_configuration'],
            'collection_active': summary['collection_active']
        }
    
    def reset(self) -> None:
        """Reset scenario state and metrics."""
        self.metrics_collector.reset_metrics()
    
    def validate_configuration(self) -> bool:
        """
        Validate scenario configuration.
        
        Returns:
            bool: True if configuration is valid
        """
        if not self.target_host or not self.target_host.strip():
            return False
        
        if not self.target_ports or len(self.target_ports) == 0:
            return False
        
        if self.test_duration <= 0:
            return False
        
        if self.target_throughput_kbps <= 0:
            return False
        
        return True
    
    def estimate_test_messages(
        self,
        duration: int,
        target_throughput_kbps: int,
        average_message_size: int = 256
    ) -> int:
        """
        Estimate number of messages for a performance test.
        
        Args:
            duration: Test duration in seconds
            target_throughput_kbps: Target throughput in kbytes/s
            average_message_size: Average message size in bytes
            
        Returns:
            int: Estimated number of messages
        """
        target_bytes = target_throughput_kbps * 1024 * duration
        estimated_messages = int(target_bytes / average_message_size)
        return estimated_messages
