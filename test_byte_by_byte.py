#!/usr/bin/env python3
"""
Byte-by-byte TCP test for UART2ETH channels

Send messages byte-by-byte with configurable delay to test problematic
message sizes and identify potential buffer or timing issues.

Usage:
    python3 test_byte_by_byte.py TARGET_IP TARGET_PORT [OPTIONS]

Examples:
    # Test Channel 1 (UART1)
    python3 test_byte_by_byte.py 10.10.10.19 4002
    
    # Test Channel 2 (PIO UART) 
    python3 test_byte_by_byte.py 10.10.10.19 4003
    
    # Test Channel 3 (PIO UART)
    python3 test_byte_by_byte.py 10.10.10.19 4004
    
    # Custom message size and delay
    python3 test_byte_by_byte.py 10.10.10.19 4002 --size 539 --delay-ms 100
"""

import socket
import time
import argparse
import sys

def send_byte_by_byte(target_ip, target_port, size, delay_ms=10):
    """Send message byte-by-byte with delay"""
    payload_size = size - 8
    msg = f"#ABCD{'A' * payload_size}!\r\n"
    
    print(f"Connecting to {target_ip}:{target_port}")
    print(f"Sending {len(msg)} bytes, one by one with {delay_ms}ms delay...")
    
    i = -1  # Initialize i to handle errors before loop starts
    s = None
    try:
        s = socket.socket()
        print(f"Trying to connect to {target_ip} port {target_port}...")
        s.connect((target_ip, target_port))
        
        for i, byte in enumerate(msg.encode()):
            s.send(bytes([byte]))
            print(f"Sent byte {i+1}: 0x{byte:02X} ('{chr(byte) if 32 <= byte <= 126 else '?'}')")
            time.sleep(delay_ms / 1000.0)
            
        print("All bytes sent, waiting for response...")
        s.settimeout(3)
        response = s.recv(1024)
        print(f"Got response: {len(response)} bytes")
        return True
        
    except Exception as e:
        if i == -1:
            print(f"Error during connection: {e}")
        else:
            print(f"Error at byte {i+1}: {e}")
        return False
    finally:
        if s:
            try:
                s.close()
            except:
                pass

def create_argument_parser():
    """Create command line argument parser."""
    parser = argparse.ArgumentParser(
        description='Byte-by-byte TCP test for UART2ETH channels',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Test Channel 1 (UART1)
  %(prog)s 10.10.10.19 4002

  # Test Channel 2 (PIO UART) 
  %(prog)s 10.10.10.19 4003

  # Test Channel 3 (PIO UART)
  %(prog)s 10.10.10.19 4004

  # Custom message size and delay
  %(prog)s 10.10.10.19 4002 --size 539 --delay-ms 100
        """
    )
    
    # Positional arguments
    parser.add_argument('target_ip', help='Target device IP address')
    parser.add_argument('target_port', type=int, help='Target port (e.g., 4002, 4003, 4004)')
    
    # Optional arguments
    parser.add_argument('--size', type=int, default=539,
                       help='Message size in bytes (default: 539)')
    parser.add_argument('--delay-ms', type=int, default=50,
                       help='Delay between bytes in milliseconds (default: 50)')
    
    return parser

def main():
    """Main entry point."""
    parser = create_argument_parser()
    args = parser.parse_args()
    
    print("UART2ETH Byte-by-byte Test")
    print("=" * 40)
    print(f"Target: {args.target_ip}:{args.target_port}")
    print(f"Message size: {args.size} bytes")
    print(f"Delay between bytes: {args.delay_ms}ms")
    print()
    
    # Run the test
    success = send_byte_by_byte(args.target_ip, args.target_port, args.size, args.delay_ms)
    
    if success:
        print("\nTest completed successfully!")
        return 0
    else:
        print("\nTest failed!")
        return 1

if __name__ == '__main__':
    sys.exit(main())
