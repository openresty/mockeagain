"""
A simple threaded echo server.
"""

import socket
import threading
import SocketServer
import subprocess
import sys

class ThreadedTCPRequestHandler(SocketServer.BaseRequestHandler):

    def handle(self):
        while True:
            try:
                data = self.request.recv(1024)
                if not data:
                    break
                self.request.sendall(data)
            except:
                break

class ThreadedTCPServer(SocketServer.ThreadingMixIn, SocketServer.TCPServer):
    pass

if __name__ == "__main__":
    HOST, PORT = "localhost", 0

    server = ThreadedTCPServer((HOST, PORT), ThreadedTCPRequestHandler)

    server_thread = threading.Thread(target=server.serve_forever)
    server_thread.daemon = True
    server_thread.start()

    print('Listening on IP %s and port %d' % server.server_address)
    env = {
              "LD_PRELOAD": sys.argv[3],
              "DYLD_INSERT_LIBRARIES": sys.argv[3],
              "DYLD_FORCE_FLAT_NAMESPACE": "1",
              "MOCKEAGAIN_VERBOSE": "1",
          }

    if len(sys.argv) == 4 and sys.argv[2] == '1': # valgrind
        ret = subprocess.call(['valgrind', '--leak-check=full',
                               sys.argv[1], server.server_address[0],
                               str(server.server_address[1])], env=env)
    else:
        ret = subprocess.call([sys.argv[1], server.server_address[0],
                              str(server.server_address[1])], env=env)

    sys.exit(ret)
