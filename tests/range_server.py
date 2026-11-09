# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DeLFuS77
#
# A file served with HTTP Range, as the library app serves /install/f/{id}/{name}, for the
# network source test. It asks for the key as the app does (basic authentication), counts the
# connections open at once and, on purpose, cuts some answers half way or stalls them so the
# source has to ask for the rest.
#   python3 range_server.py PORT SIZE KEY
import base64
import http.server
import socketserver
import sys
import threading
import time

PORT, SIZE, KEY = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
AUTH = "Basic " + base64.b64encode(("nxdock:" + KEY).encode()).decode()

lock = threading.Lock()
state = {"open": 0, "most": 0, "requests": 0, "cut": 0}


# the same bytes as Expected() in http_source_test.cpp, made once
DATA = bytes(((i * 31 + (i >> 8)) & 0xFF) for i in range(SIZE))


def data(start, end):
    return DATA[start:end]


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/stats":
            body = ("%d %d %d" % (state["most"], state["requests"], state["cut"])).encode()
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.headers.get("Authorization") != AUTH:
            self.send_response(401)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        rng = self.headers.get("Range", "")
        if not rng.startswith("bytes="):
            self.send_response(400)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        a, b = rng[6:].split("-")
        a, b = int(a), min(int(b), SIZE - 1)
        with lock:
            state["open"] += 1
            state["most"] = max(state["most"], state["open"])
            state["requests"] += 1
            n = state["requests"]
        try:
            self.send_response(206)
            self.send_header("Content-Range", "bytes %d-%d/%d" % (a, b, SIZE))
            self.send_header("Content-Length", str(b - a + 1))
            self.end_headers()
            body = data(a, b + 1)
            # every fifth answer of more than 64 KB is cut after a third: the connection drops
            if n % 5 == 0 and len(body) > 65536:
                with lock:
                    state["cut"] += 1
                self.wfile.write(body[: len(body) // 3])
                self.wfile.flush()
                self.close_connection = True
                return
            # a slow start, so several requests are open at once
            time.sleep(0.05)
            self.wfile.write(body)
        finally:
            with lock:
                state["open"] -= 1


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


Server(("127.0.0.1", PORT), Handler).serve_forever()
