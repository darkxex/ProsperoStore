# ProsperoStore - Local TLS server, with an ephemeral localhost certificate.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import threading


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def handle(self):
        try:
            super().handle()
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
            pass  # Expected when the client refuses TLS or an oversized response.

    def log_message(self, *_):
        pass

    def do_GET(self):
        if self.path.startswith("/redirect-body"):
            size = 65537 if self.path == "/redirect-body-large" else 65536
            self.send_response(302)
            self.send_header("Location", "/ok")
            self.send_header("Content-Length", str(size))
            self.end_headers()
            self.wfile.write(b"x" * size)
            return
        if self.path == "/interim":
            self.wfile.write(b"HTTP/1.1 103 Early Hints\r\nETag: stale\r\n\r\n")
        self.send_response(302 if self.path == "/redirect" else
                           500 if self.path == "/error" else 200)
        self.send_header("ETag", f'"connection-{self.client_address[1]}"'
                         if self.path == "/reuse" else '"current"')
        if self.path == "/redirect":
            self.send_header("Location", "https://example.com/forbidden")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if self.path == "/headers":
            for index in range(80):
                self.send_header(f"X-Large-{index}", "x" * 1024)
        self.send_header("Content-Length", "4")
        self.end_headers()
        try:
            self.wfile.write(b"okay")
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
            pass


with tempfile.TemporaryDirectory(prefix="prospero-tls-") as temporary:
    cert = str(Path(temporary) / "cert.pem")
    key = str(Path(temporary) / "key.pem")
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                    "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
                    "-keyout", key, "-out", cert], check=True, capture_output=True)
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever)
    thread.start()
    try:
        subprocess.run([sys.argv[1], str(server.server_port), cert], check=True, timeout=30)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
