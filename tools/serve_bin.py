#!/usr/bin/env python3
"""Simple web server to serve build/bosch-tank.bin only."""

from http.server import HTTPServer, BaseHTTPRequestHandler
from pathlib import Path
import os
import shutil

BIN_FILE = Path(__file__).resolve().parent.parent / "build" / "bosch-tank.bin"
HOST = "0.0.0.0"
PORT = 80

class BinOnlyHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        print("[{}] {} {} - {}".format(self.client_address[0], self.command, self.path, format % args))

    def send_bin_file(self):
        if not BIN_FILE.exists():
            self.send_error(500, "Firmware binary not found")
            return

        file_size = BIN_FILE.stat().st_size
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Disposition", "attachment; filename=bosch-tank.bin")
        self.send_header("Content-Length", str(file_size))
        self.end_headers()

        with open(BIN_FILE, "rb") as f:
            shutil.copyfileobj(f, self.wfile)

        self.log_message("Served binary (%d bytes)", file_size)

    def do_GET(self):
        if self.path == "/":
            content = (
                f"<html><head><title>OTA Firmware Server</title></head><body>"
                f"<h1>OTA Firmware Server</h1>"
                f"<p>Firmware file: <a href=\"/bosch-tank.bin\">/bosch-tank.bin</a></p>"
                f"<p>Size: {BIN_FILE.stat().st_size if BIN_FILE.exists() else 'unknown'} bytes</p>"
                f"<p>Use this URL in your OTA client:</p>"
                f"<pre>http://{HOST}:{PORT}/bosch-tank.bin</pre>"
                f"</body></html>"
            )
            data = content.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            self.log_message("Served status page")
            return

        if self.path == "/bosch-tank.bin":
            self.send_bin_file()
            return

        self.send_error(404, "Only / and /bosch-tank.bin are supported")

    def do_HEAD(self):
        if self.path == "/":
            data = b""
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        if self.path == "/bosch-tank.bin":
            if not BIN_FILE.exists():
                self.send_error(500, "Firmware binary not found")
                return
            file_size = BIN_FILE.stat().st_size
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Disposition", "attachment; filename=bosch-tank.bin")
            self.send_header("Content-Length", str(file_size))
            self.end_headers()
            return

        self.send_error(404, "Only / and /bosch-tank.bin are supported")

if __name__ == "__main__":
    if not BIN_FILE.exists():
        raise SystemExit(f"Firmware not found: {BIN_FILE}\nRun idf.py build first.")

    os.chdir(BIN_FILE.parent)
    server = HTTPServer((HOST, PORT), BinOnlyHandler)
    print(f"Serving {BIN_FILE.name} at http://{HOST}:{PORT}/bosch-tank.bin")
    print("Root path / shows server status and download instructions.")
    print("Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nServer stopped.")
    finally:
        server.server_close()
