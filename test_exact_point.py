#!/usr/bin/env python3
import socket
import time
import sys

def test_size(size, timeout=3):
    """Test specific message size with timeout"""
    try:
        payload_size = max(0, size - 8)
        msg = f"#ABCD{'A' * payload_size}!\r\n"
        actual_size = len(msg)
        
        print(f"{actual_size}: ", end='', flush=True)
        
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect(('10.10.10.10', 4001))
        sock.send(msg.encode())
        
        response = sock.recv(2048)
        sock.close()
        
        if len(response) == actual_size:
            print("OK")
            return True
        else:
            print(f"MISMATCH ({len(response)})")
            return False
            
    except socket.timeout:
        print("TIMEOUT")
        try:
            sock.close()
        except:
            pass
        return False
    except Exception as e:
        print(f"ERROR: {e}")
        return False

def main():
    print("Finding exact breaking point...")
    
    # Test around known working/failing boundary
    for size in range(530, 545):
        success = test_size(size)
        time.sleep(0.5)
        
        if not success:
            print(f"\nBreaking point: {size} bytes")
            break
    
    print("\nTesting reproducibility...")
    # Test the failing size 3 times
    failing_size = 540
    for i in range(3):
        print(f"Retry {i+1}: ", end='')
        test_size(failing_size)
        time.sleep(1)

if __name__ == "__main__":
    main()
