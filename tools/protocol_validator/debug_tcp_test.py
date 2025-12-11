#!/usr/bin/env python3
"""
Debug script to isolate the performance/stress vs compliance issue
"""

import asyncio
import sys
import os
sys.path.insert(0, '/home/shueltenschmidt/projects/UART2ETH/tools/protocol_validator')

from networking.tcp_client import TCPClient
from protocol.formats import ProtocolMessage

async def test_single_message():
    """Test sending a single message like compliance test does"""
    client = TCPClient()
    print("=== SINGLE MESSAGE TEST (like compliance) ===")
    
    try:
        print("Connecting to 10.10.10.19:4002...")
        connected = await client.connect("10.10.10.19", 4002)
        if not connected:
            print("ERROR: Failed to connect")
            return
        
        # Send a simple message like compliance test
        message = ProtocolMessage(hex_header="0000", payload="")
        print(f"Sending: {message.to_wire_format()}")
        
        rtt = await client.send_message(message, timeout=30.0)
        print(f"SUCCESS: RTT = {rtt:.3f}s")
        
    except Exception as e:
        print(f"ERROR: {e}")
    finally:
        await client.disconnect()

async def test_rapid_messages():
    """Test sending rapid messages like performance test does"""
    client = TCPClient()
    print("\n=== RAPID MESSAGE TEST (like performance) ===")
    
    try:
        print("Connecting to 10.10.10.19:4002...")
        connected = await client.connect("10.10.10.19", 4002)
        if not connected:
            print("ERROR: Failed to connect")
            return
        
        # Send rapid messages like performance test
        for i in range(5):
            message = ProtocolMessage(hex_header=f"{i:04X}", payload="TEST")
            print(f"Sending #{i}: {message.to_wire_format()}")
            
            try:
                rtt = await client.send_message(message, timeout=30.0)
                print(f"  SUCCESS: RTT = {rtt:.3f}s")
            except Exception as e:
                print(f"  ERROR: {e}")
            
            # 10ms delay like performance test
            await asyncio.sleep(0.01)
        
    except Exception as e:
        print(f"ERROR: {e}")
    finally:
        await client.disconnect()

async def main():
    print("DEBUG: Testing message sending patterns")
    
    await test_single_message()
    await test_rapid_messages()
    
    print("\nDEBUG: Test complete")

if __name__ == "__main__":
    asyncio.run(main())
