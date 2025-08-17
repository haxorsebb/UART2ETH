#!/usr/bin/env python3
"""
UART2ETH Protocol Validation Tool

Main entry point for the external protocol validation tool that provides
comprehensive testing of the UART2ETH TCP socket server implementation.

Usage:
    python3 uart2eth_protocol_validator.py [OPTIONS] TARGET_IP [TARGET_PORTS...]

Examples:
    # Basic compliance test
    python3 uart2eth_protocol_validator.py 10.10.10.10 4001
    
    # Performance test with multiple ports
    python3 uart2eth_protocol_validator.py --test-scenarios performance \\
        --duration 300 --throughput 500 10.10.10.10 4001 4002 4003 4004
    
    # Stress test with custom parameters
    python3 uart2eth_protocol_validator.py --test-scenarios stress \\
        --duration 1800 --concurrent-connections 4 10.10.10.10 4001

References: ADR-010 External Protocol Validation Tool
"""

import argparse
import asyncio
import json
import sys
import time
import os
from typing import List, Dict, Any

# Add the tools directory to Python path for imports
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from protocol_validator.testing.performance_scenario import PerformanceTestScenario
from protocol_validator.utils.logger import setup_logger
from protocol_validator.statistics.metrics_collector import MetricsCollector


async def run_compliance_test(scenario: PerformanceTestScenario, args: argparse.Namespace) -> Dict[str, Any]:
    """Run protocol compliance validation test."""
    print(f"Starting compliance test on {args.target_ip}:{args.target_ports[0]}")
    
    # Connect to device
    connected = await scenario.connect_to_device(args.target_ports[0])
    if not connected:
        print(f"ERROR: Failed to connect to {args.target_ip}:{args.target_ports[0]}")
        return {"error": "Connection failed"}
    
    try:
        # Run compliance test
        results = await scenario.run_compliance_test(message_count=200)
        print(f"Compliance test completed:")
        print(f"  Total messages: {results['total_messages']}")
        print(f"  Successful: {results['successful_messages']}")
        print(f"  Failed: {results['failed_messages']}")
        print(f"  Compliance rate: {results['compliance_rate']:.1f}%")
        
        return results
    
    finally:
        await scenario.disconnect_from_device()


async def run_performance_test(scenario: PerformanceTestScenario, args: argparse.Namespace) -> Dict[str, Any]:
    """Run performance measurement test."""
    print(f"Starting performance test on {args.target_ip}:{args.target_ports[0]}")
    print(f"Duration: {args.duration}s, Target: {args.throughput} kbps")
    
    # Connect to device
    connected = await scenario.connect_to_device(args.target_ports[0])
    if not connected:
        print(f"ERROR: Failed to connect to {args.target_ip}:{args.target_ports[0]}")
        return {"error": "Connection failed"}
    
    try:
        # Run performance test
        results = await scenario.run_performance_test(duration=args.duration)
        
        print(f"Performance test completed:")
        print(f"  Test duration: {results['test_duration']:.1f}s")
        print(f"  Messages sent: {results['messages_sent']}")
        print(f"  Average throughput: {results['average_throughput_kbps']:.1f} kbps")
        print(f"  Average latency: {results['average_latency_ms']:.2f}ms")
        
        if results['latency_percentiles']:
            print(f"  Latency percentiles:")
            for percentile, value in results['latency_percentiles'].items():
                print(f"    {percentile}: {value:.2f}ms")
        
        return results
    
    finally:
        await scenario.disconnect_from_device()


async def run_stress_test(scenario: PerformanceTestScenario, args: argparse.Namespace) -> Dict[str, Any]:
    """Run stress test scenario."""
    print(f"Starting stress test on {args.target_ip}:{args.target_ports[0]}")
    print(f"Duration: {args.duration}s, Connections: {args.concurrent_connections}")
    
    # Connect to device
    connected = await scenario.connect_to_device(args.target_ports[0])
    if not connected:
        print(f"ERROR: Failed to connect to {args.target_ip}:{args.target_ports[0]}")
        return {"error": "Connection failed"}
    
    try:
        # Run stress test
        results = await scenario.run_stress_test(
            duration=args.duration,
            concurrent_connections=args.concurrent_connections,
            message_rate=1000  # Fixed rate for now
        )
        
        print(f"Stress test completed:")
        print(f"  Test duration: {results['stress_duration']:.1f}s")
        print(f"  Total messages: {results['total_messages']}")
        print(f"  Connection failures: {results['connection_failures']}")
        print(f"  Actual message rate: {results['actual_message_rate']:.1f} msg/s")
        
        return results
    
    finally:
        await scenario.disconnect_from_device()


def save_results(results: Dict[str, Any], filename: str) -> None:
    """Save test results to file."""
    try:
        with open(filename, 'w') as f:
            json.dump(results, f, indent=2)
        print(f"Results saved to: {filename}")
    except Exception as e:
        print(f"WARNING: Failed to save results to {filename}: {e}")


def create_argument_parser() -> argparse.ArgumentParser:
    """Create command line argument parser."""
    parser = argparse.ArgumentParser(
        description='UART2ETH Protocol Validation Tool',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Basic compliance test
  %(prog)s 10.10.10.10 4001

  # Performance test with multiple ports  
  %(prog)s --test-scenarios performance --duration 300 --throughput 500 \\
           10.10.10.10 4001 4002 4003 4004

  # Stress test
  %(prog)s --test-scenarios stress --duration 1800 --concurrent-connections 4 \\
           10.10.10.10 4001
        """
    )
    
    # Positional arguments
    parser.add_argument('target_ip', help='Target device IP address')
    parser.add_argument('target_ports', nargs='+', type=int, 
                       help='Target ports (e.g., 4001 4002 4003 4004)')
    
    # Test configuration
    parser.add_argument('--test-scenarios', choices=['compliance', 'performance', 'stress', 'all'],
                       default='compliance', help='Test scenarios to run (default: compliance)')
    parser.add_argument('--duration', type=int, default=60,
                       help='Test duration in seconds (default: 60)')
    parser.add_argument('--throughput', type=int, default=500,
                       help='Target throughput in kbps (default: 500)')
    parser.add_argument('--concurrent-connections', type=int, default=1,
                       help='Number of concurrent connections per port (default: 1)')
    
    # Message configuration  
    parser.add_argument('--message-sizes', default='8:1024:100',
                       help='Message size range: min:max:step (default: 8:1024:100)')
    parser.add_argument('--invalid-ratio', type=int, default=10,
                       help='Percentage of invalid messages (default: 10)')
    
    # Output configuration
    parser.add_argument('--output-format', choices=['json', 'csv', 'human'], 
                       default='human', help='Output format (default: human)')
    parser.add_argument('--statistics-interval', type=int, default=10,
                       help='Statistics reporting interval in seconds (default: 10)')
    parser.add_argument('--log-level', choices=['debug', 'info', 'warn', 'error'],
                       default='info', help='Logging level (default: info)')
    
    # File operations
    parser.add_argument('--save-results', metavar='FILE',
                       help='Save detailed results to file')
    parser.add_argument('--baseline-file', metavar='FILE',
                       help='Compare against baseline results')
    
    return parser


async def main():
    """Main entry point for the protocol validation tool."""
    parser = create_argument_parser()
    args = parser.parse_args()
    
    # Setup logging
    logger = setup_logger(args.log_level)
    
    print("UART2ETH Protocol Validation Tool")
    print("=" * 40)
    print(f"Target: {args.target_ip}:{args.target_ports}")
    print(f"Test scenarios: {args.test_scenarios}")
    print(f"Duration: {args.duration}s")
    print()
    
    # Create scenario with configuration
    scenario = PerformanceTestScenario(
        target_host=args.target_ip,
        target_ports=args.target_ports,
        test_duration=args.duration,
        target_throughput_kbps=args.throughput
    )
    
    # Validate configuration
    if not scenario.validate_configuration():
        print("ERROR: Invalid configuration parameters")
        return 1
    
    # Run tests based on selected scenarios
    all_results = {}
    
    try:
        if args.test_scenarios in ['compliance', 'all']:
            all_results['compliance'] = await run_compliance_test(scenario, args)
        
        if args.test_scenarios in ['performance', 'all']:
            all_results['performance'] = await run_performance_test(scenario, args)
        
        if args.test_scenarios in ['stress', 'all']:
            all_results['stress'] = await run_stress_test(scenario, args)
        
        # Get comprehensive summary
        summary = scenario.get_test_summary()
        all_results['summary'] = summary
        
        # Save results if requested
        if args.save_results:
            save_results(all_results, args.save_results)
        
        # Output final summary
        print("\nTest Summary:")
        print("=" * 40)
        if 'overall_metrics' in summary:
            metrics = summary['overall_metrics']
            print(f"Total messages sent: {metrics.get('messages_sent', 0)}")
            print(f"Total bytes transferred: {metrics.get('total_bytes', 0)}")
            if metrics.get('throughput_kbps', 0) > 0:
                print(f"Overall throughput: {metrics['throughput_kbps']:.1f} kbps")
        
        print("\nTest completed successfully!")
        return 0
        
    except KeyboardInterrupt:
        print("\nTest interrupted by user")
        return 130
    
    except Exception as e:
        print(f"ERROR: Test failed: {e}")
        logger.error(f"Test execution failed: {e}")
        return 1


if __name__ == '__main__':
    sys.exit(asyncio.run(main()))
