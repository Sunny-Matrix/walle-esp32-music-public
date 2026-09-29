from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.request import Request, urlopen
from urllib.error import HTTPError, URLError
import json
from pathlib import Path

DEVICE = "http://192.168.1.100"


class ControlHandler(SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(Path(__file__).resolve().parent), **kwargs)

    def _proxy(self):
        target = DEVICE + self.path[len("/device"):]
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length else None
        request = Request(target, data=body, method=self.command,
                          headers={"Content-Type": "text/plain;charset=UTF-8",
                                   "Connection": "close"})
        try:
            with urlopen(request, timeout=5) as response:
                payload = response.read()
                self.send_response(response.status)
                self.send_header("Content-Type", response.headers.get("Content-Type", "application/json"))
                self.send_header("Content-Length", str(len(payload)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(payload)
        except HTTPError as error:
            payload = error.read()
            self.send_response(error.code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(payload)
        except (URLError, TimeoutError) as error:
            payload = json.dumps({"error": str(error)}).encode()
            self.send_response(502)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(payload)

    def do_GET(self):
        if self.path.startswith("/device/"):
            self._proxy()
        else:
            super().do_GET()

    def do_POST(self):
        if self.path.startswith("/device/"):
            self._proxy()
        else:
            self.send_error(404)

    def log_message(self, format, *args):
        print(format % args)


if __name__ == "__main__":
    ThreadingHTTPServer(("0.0.0.0", 8080), ControlHandler).serve_forever()
