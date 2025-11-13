#!/usr/bin/env python3
import socket
import time
import sys

def send_message(size):
    """Send message of specific size and return success status"""
    try:
        # Create message: #ABCD + payload + !\r\n
        payload_size = size - 8
        if payload_size < 0:
            payload_size = 0
        
        msg = f"#ABCD{'A' * payload_size}!\r\n"
        actual_size = len(msg)
        
        print(f"Sending {actual_size} bytes... ", end='', flush=True)
        
        # Connect and send
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect(('10.10.10.19', 4002))
        sock.send(msg.encode())
        
        # Receive response (no timeout)
        response = sock.recv(2048)
        sock.close()
        
        if len(response) == actual_size:
            print("OK")
            return True
        else:
            print(f"MISMATCH (got {len(response)} bytes)")
            return False
            
    except Exception as e:
        print(f"ERROR: {e}")
        return False

def main():
    print("Testing incremental message sizes...")
    print("Press Ctrl+C to stop\n")
    
    # Test from 500 to 1024 in steps of 10
    for size in range(500, 1025, 10):
        success = send_message(size)
        if not success:
            print(f"Failed at {size} bytes - stopping")
            break
            
        # Wait 500ms between messages
        time.sleep(0.5)
    
    print("\nTest completed")

if __name__ == "__main__":
    main()
