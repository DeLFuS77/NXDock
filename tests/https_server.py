# A tiny HTTPS server for the pin test: answers /install/hello with its certificate.
import http.server, ssl, sys

class Hello(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b'{"app":"switch-library-manager-web"}')
    def log_message(self, *a):
        pass

server = http.server.HTTPServer(("127.0.0.1", int(sys.argv[1])), Hello)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(sys.argv[2], sys.argv[3])
server.socket = ctx.wrap_socket(server.socket, server_side=True)
server.serve_forever()
