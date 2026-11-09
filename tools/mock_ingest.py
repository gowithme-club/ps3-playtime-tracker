#!/usr/bin/env python3
"""Заглушка сервера приёма — чтобы проверять плагин, пока настоящих ручек нет.

Отвечает 200 на всё, что описано в docs/ingest-api-v1-client.md, и печатает
каждый запрос: заголовки, тело, разобранный JSON. Этого достаточно, чтобы
увидеть, что консоль шлёт ровно то, что нужно.

    python3 tools/mock_ingest.py            # слушает 0.0.0.0:8080

Затем в /dev_hdd0/plugins/gwm.cfg на консоли:

    token   = test
    account = Player
    host    = <ip этой машины>
    port    = 8080
"""

import functools
import json
import sys
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080

# Без этого вывод буферизуется при перенаправлении в файл, и лога не видно,
# пока процесс не завершится.
print = functools.partial(__builtins__.print, flush=True)

# Watermark отдаём, хотя PS3-клиент его не читает: пусть ответ будет
# правдоподобным на случай, если позже появится второй клиент.
watermark = None


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass  # свой формат ниже

    def _stamp(self):
        return datetime.now(timezone.utc).strftime("%H:%M:%S")

    def _reply(self, payload, code=200):
        body = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        print(f"[{self._stamp()}] GET {self.path}")
        self._reply({"sessionsWatermark": watermark, "resyncTrophies": False})

    def do_POST(self):
        global watermark

        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length)
        auth = self.headers.get("Authorization", "<нет>")
        client = self.headers.get("X-Client-Version", "<нет>")

        print(f"\n[{self._stamp()}] POST {self.path}")
        print(f"  Authorization: {auth}")
        print(f"  X-Client-Version: {client}")
        print(f"  Content-Length: {length}")

        try:
            data = json.loads(raw)
        except Exception as exc:
            print(f"  !! тело не разобралось: {exc}")
            print(f"  сырое: {raw!r}")
            self._reply({"error": "bad json"}, 400)
            return

        print(f"  тело: {json.dumps(data, ensure_ascii=False)}")

        if self.path.endswith("/heartbeat"):
            cur = data.get("current", [])
            if cur:
                for c in cur:
                    print(f"  -> играет: {c.get('titleName')} [{c.get('titleId')}]"
                          f" с {c.get('startedAt')}, профиль {c.get('account')}")
            else:
                print("  -> консоль включена, никто не играет")
            self._reply({"sessionsWatermark": watermark, "resyncTrophies": False})

        elif self.path.endswith("/sessions"):
            sessions = data.get("sessions", [])
            for s in sessions:
                print(f"  -> сессия: {s.get('title')} [{s.get('titleId')}]"
                      f" {s.get('startedAt')} .. {s.get('endedAt')}"
                      f" = {s.get('seconds')} с, профиль {s.get('account')}")
                if s.get("endedAt"):
                    watermark = max(watermark or "", s["endedAt"])
            self._reply({"accepted": len(sessions), "duplicates": 0,
                         "rejected": [], "sessionsWatermark": watermark})

        else:
            self._reply({"ok": True})


if __name__ == "__main__":
    print(f"заглушка слушает 0.0.0.0:{PORT}, Ctrl-C для выхода")
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
