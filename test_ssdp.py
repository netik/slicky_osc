#!/usr/bin/env python3
"""
SSDP Service Discovery Test Client
Tests discovery of OSC services via SSDP
"""

import socket
import struct
import time
import argparse
from urllib.parse import urlparse

def discover_ssdp_services(duration=10):
    """Discover SSDP services on the network"""
    if duration == 0:
        print("Discovering SSDP services forever...".format(duration))
    else: 
        print("Discovering SSDP services for {} seconds...".format(duration))
        
    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    # Bind to a different port to avoid conflicts
    sock.bind(('', 1901))
    
    # Join multicast group
    mcast_group = '239.255.255.250'
    mcast_addr = struct.pack('4sl', socket.inet_aton(mcast_group), socket.INADDR_ANY)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mcast_addr)
    
    # Set timeout
    sock.settimeout(1.0)
    
    discovered_services = []
    start_time = time.time()
    
    try:
        while time.time() - start_time < duration or duration == 0:
            try:
                data, addr = sock.recvfrom(4096)
                message = data.decode('utf-8', errors='ignore')
                
                # Debug: print all SSDP messages
                print("\n📨 SSDP Message from {}:{}".format(addr[0], addr[1]))
                print("Raw message:")
                print(message)
                
                # Parse SSDP message
                lines = message.split('\r\n')
                method = lines[0] if lines else ''
                
                if 'NOTIFY' in method:
                    service_info = parse_ssdp_message(lines)
                    if service_info and 'OSC' in service_info.get('NT', ''):
                        print_discovered_service(addr, service_info)
                        discovered_services.append(service_info)
                
                elif 'M-SEARCH' in method:
                    print("📡 M-SEARCH request from {}:{}".format(addr[0], addr[1]))
                    
            except socket.timeout:
                continue
            except (socket.error, UnicodeDecodeError) as e:
                print("Error receiving data: {}".format(e))
                
    except KeyboardInterrupt:
        print("\nDiscovery interrupted by user")
    
    finally:
        sock.close()
    
    return discovered_services

def parse_ssdp_message(lines):
    """Parse SSDP message lines into service info dict"""
    service_info = {}
    for line in lines[1:]:
        if ':' in line:
            key, value = line.split(':', 1)
            service_info[key.strip()] = value.strip()
    return service_info

def print_discovered_service(addr, service_info):
    """Print discovered service information"""
    print("\n🎯 OSC Service Discovered!")
    print("   From: {}:{}".format(addr[0], addr[1]))
    print("   Service Type: {}".format(service_info.get('NT', 'Unknown')))
    print("   Location: {}".format(service_info.get('LOCATION', 'Unknown')))
    print("   Server: {}".format(service_info.get('SERVER', 'Unknown')))
    print("   USN: {}".format(service_info.get('USN', 'Unknown')))

def test_osc_connection(service_info):
    """Test OSC connection to discovered service"""
    location = service_info.get('LOCATION', '')
    if not location:
        print("No location URL found in service info")
        return False
    
    try:
        parsed = urlparse(location)
        host = parsed.hostname
        port = parsed.port or 9000  # Default OSC port
        
        print("\n🔌 Testing OSC connection to {}:{}...".format(host, port))
        
        # Create UDP socket for OSC
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(2.0)
        
        # Try to send a simple OSC message (ping)
        osc_message = b'/ping\x00\x00\x00\x00,i\x00\x00\x00\x00'
        sock.sendto(osc_message, (host, port))
        
        print("✅ OSC message sent to {}:{}".format(host, port))
        return True
        
    except (socket.error, ValueError) as e:
        print("❌ Failed to test OSC connection: {}".format(e))
        return False

def main():
    parser = argparse.ArgumentParser(description='SSDP Service Discovery Test Client')
    parser.add_argument('-d', '--duration', type=int, default=10, 
                       help='Discovery duration in seconds (default: 10)')
    parser.add_argument('-v', '--verbose', action='store_true', 
                       help='Enable verbose output')
    parser.add_argument('--test-connection', action='store_true',
                       help='Test OSC connection to discovered services')
    
    args = parser.parse_args()
    
    print("🔍 SSDP Service Discovery Test Client")
    print("=" * 50)
    
    # Discover services
    services = discover_ssdp_services(args.duration)
    
    if not services:
        print("\n❌ No OSC services discovered")
        return
    
    print("\n✅ Discovered {} OSC service(s)".format(len(services)))
    
    # Test connections if requested
    if args.test_connection:
        for i, service in enumerate(services, 1):
            print("\n--- Testing Service {} ---".format(i))
            test_osc_connection(service)

if __name__ == '__main__':
    main()
