'''
Modulo di comunicazione con ESP32
'''

import socket

HOST = "0.0.0.0"  # Listen on all interfaces (use "127.0.0.1" for local-only testing)
PORT = 65432


print("config...")
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
    s.bind((HOST, PORT))
    while True:
        data, addr = s.recvfrom(1024)
        print(f"Ricevuti {len(data)} bytes da {addr}")
        s.sendto(data, addr)