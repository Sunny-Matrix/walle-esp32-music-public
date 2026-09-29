from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class LearningHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(Path(__file__).resolve().parent), **kwargs)

    def log_message(self, format, *args):
        print(format % args)


if __name__ == '__main__':
    ThreadingHTTPServer(('0.0.0.0', 8081), LearningHandler).serve_forever()
