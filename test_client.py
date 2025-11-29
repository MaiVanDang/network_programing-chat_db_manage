#!/usr/bin/env python3
"""
Simple test client for the chat server
Tests the 3 implemented tasks:
1. Stream processing
2. Socket I/O
3. User registration & authentication
"""

import socket
import sys
import time

DELIMITER = b"\r\n"

class ChatClient:
    def __init__(self, host='localhost', port=8888):
        self.host = host
        self.port = port
        self.sock = None
        
    def connect(self):
        """Connect to the server"""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((self.host, self.port))
            print(f"✓ Connected to {self.host}:{self.port}")
            
            # Receive welcome message
            response = self.receive()
            print(f"Server: {response}")
            return True
        except Exception as e:
            print(f"✗ Connection failed: {e}")
            return False
    
    def disconnect(self):
        """Disconnect from server"""
        if self.sock:
            self.sock.close()
            print("✓ Disconnected")
    
    def send(self, message):
        """Send a message to the server"""
        if not self.sock:
            print("✗ Not connected")
            return False
        
        try:
            # Add delimiter
            full_message = message.encode() + DELIMITER
            self.sock.sendall(full_message)
            print(f"→ Sent: {message}")
            return True
        except Exception as e:
            print(f"✗ Send failed: {e}")
            return False
    
    def receive(self, timeout=2):
        """Receive a response from the server"""
        if not self.sock:
            print("✗ Not connected")
            return None
        
        try:
            self.sock.settimeout(timeout)
            data = b""
            
            while True:
                chunk = self.sock.recv(1024)
                if not chunk:
                    break
                data += chunk
                
                # Check if we have a complete message
                if DELIMITER in data:
                    break
            
            # Remove delimiter and decode
            message = data.replace(DELIMITER, b"").decode().strip()
            print(f"← Received: {message}")
            return message
        except socket.timeout:
            print("✗ Receive timeout")
            return None
        except Exception as e:
            print(f"✗ Receive failed: {e}")
            return None
    
    def test_register(self, username, password):
        """Test REGISTER command"""
        print(f"\n=== Testing REGISTER: {username} ===")
        cmd = f"REGISTER {username} {password}"
        if self.send(cmd):
            response = self.receive()
            if response and response.startswith("101"):
                print("✓ Registration successful")
                return True
            else:
                print("✗ Registration failed")
                return False
        return False
    
    def test_login(self, username, password):
        """Test LOGIN command"""
        print(f"\n=== Testing LOGIN: {username} ===")
        cmd = f"LOGIN {username} {password}"
        if self.send(cmd):
            response = self.receive()
            if response and response.startswith("102"):
                print("✓ Login successful")
                return True
            else:
                print("✗ Login failed")
                return False
        return False
    
    def test_logout(self):
        """Test LOGOUT command"""
        print(f"\n=== Testing LOGOUT ===")
        cmd = "LOGOUT"
        if self.send(cmd):
            response = self.receive()
            if response and response.startswith("103"):
                print("✓ Logout successful")
                return True
            else:
                print("✗ Logout failed")
                return False
        return False
    
    def test_invalid_command(self):
        """Test invalid command"""
        print(f"\n=== Testing INVALID COMMAND ===")
        cmd = "INVALID_CMD test"
        if self.send(cmd):
            response = self.receive()
            if response and response.startswith("500"):
                print("✓ Server correctly rejected invalid command")
                return True
            else:
                print("✗ Unexpected response")
                return False
        return False

def run_basic_tests():
    """Run basic functionality tests"""
    print("=" * 60)
    print("    CHAT SERVER TEST SUITE - Tasks 1, 2, 3")
    print("=" * 60)
    
    client = ChatClient()
    
    # Connect
    if not client.connect():
        return
    
    time.sleep(0.5)
    
    # Test 1: Register new user
    username1 = f"testuser_{int(time.time())}"
    client.test_register(username1, "password123")
    time.sleep(0.5)
    
    # Test 2: Try to register duplicate user
    print("\n=== Testing Duplicate Registration ===")
    client.test_register(username1, "password456")
    time.sleep(0.5)
    
    # Test 3: Login with correct credentials
    client.test_login(username1, "password123")
    time.sleep(0.5)
    
    # Test 4: Try to login again (already logged in)
    print("\n=== Testing Already Logged In ===")
    client.test_login(username1, "password123")
    time.sleep(0.5)
    
    # Test 5: Logout
    client.test_logout()
    time.sleep(0.5)
    
    # Test 6: Login with wrong password
    print("\n=== Testing Wrong Password ===")
    client.test_login(username1, "wrongpassword")
    time.sleep(0.5)
    
    # Test 7: Login with non-existent user
    print("\n=== Testing Non-existent User ===")
    client.test_login("nonexistent_user", "password")
    time.sleep(0.5)
    
    # Test 8: Invalid username (too short)
    print("\n=== Testing Invalid Username ===")
    client.test_register("ab", "password123")
    time.sleep(0.5)
    
    # Test 9: Invalid password (too short)
    print("\n=== Testing Invalid Password ===")
    client.test_register("validuser", "12345")
    time.sleep(0.5)
    
    # Test 10: Invalid command
    client.test_invalid_command()
    time.sleep(0.5)
    
    # Test 11: Stream processing - send multiple commands at once
    print("\n=== Testing Stream Processing (Multiple Commands) ===")
    client.test_register("streamtest1", "password123")
    time.sleep(0.2)
    client.test_register("streamtest2", "password123")
    time.sleep(0.2)
    client.test_register("streamtest3", "password123")
    time.sleep(0.5)
    
    # Disconnect
    client.disconnect()
    
    print("\n" + "=" * 60)
    print("    TEST SUITE COMPLETE")
    print("=" * 60)

def interactive_mode():
    """Interactive mode for manual testing"""
    print("=" * 60)
    print("    INTERACTIVE TEST CLIENT")
    print("=" * 60)
    
    client = ChatClient()
    
    if not client.connect():
        return
    
    print("\nCommands:")
    print("  REGISTER <username> <password>")
    print("  LOGIN <username> <password>")
    print("  LOGOUT")
    print("  quit - Exit client")
    print()
    
    try:
        while True:
            cmd = input("→ ").strip()
            
            if cmd.lower() == 'quit':
                break
            
            if not cmd:
                continue
            
            if client.send(cmd):
                client.receive()
            
            time.sleep(0.1)
    
    except KeyboardInterrupt:
        print("\n\nInterrupted by user")
    
    client.disconnect()

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "-i":
        interactive_mode()
    else:
        run_basic_tests()