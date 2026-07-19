#!/usr/bin/env python3
"""Mock AirGradient ONE local server for developing airdeck-papr without the
real sensor. Serves /measures/current with slowly-varying, realistic values so
you can watch trends and hero-card switching on the panel.

    python3 tools/mock_airgradient.py [--port 8080] [--scenario spike]

Point cfg::kMonitorUrl at  http://<this-machine-ip>:<port>/measures/current
"""
import argparse
import json
import math
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

START = time.time()
ARGS = None


def sample():
    t = time.time() - START
    slow = math.sin(t / 60.0)          # ~2 min cycle
    fast = math.sin(t / 7.0)
    co2 = 550 + 400 * (0.5 + 0.5 * slow) + 20 * fast
    pm25 = 6 + 4 * (0.5 + 0.5 * math.sin(t / 40.0))
    tvoc = 120 + 250 * (0.5 + 0.5 * math.sin(t / 90.0))
    nox = 1 + 3 * (0.5 + 0.5 * math.sin(t / 120.0))
    if ARGS and ARGS.scenario == "spike" and int(t) % 45 < 8:
        co2 += 900          # push CO2 into Hazardous to test hero swap
        pm25 += 40
    temp = 26 + 2 * slow
    rh = 45 + 8 * fast
    return {
        "serialno": "aabbccddeeff",
        "wifi": -46,
        "rco2": round(co2),
        "pm01": round(pm25 * 0.6, 1),
        "pm02": round(pm25, 1),
        "pm10": round(pm25 * 1.1, 1),
        "pm003Count": round(400 + 120 * fast),
        "atmp": round(temp, 1),
        "atmpCompensated": round(temp - 1.7, 1),
        "rhum": round(rh),
        "rhumCompensated": round(rh + 4),
        "tvocIndex": round(tvoc),
        "tvocRaw": round(31000 + 500 * fast),
        "noxIndex": round(nox),
        "noxRaw": round(16000 + 200 * fast),
        "boot": int(time.time() - START),
        "bootCount": 6,
        "firmware": "3.1.9",
        "model": "I-9PSL",
        "ledMode": "pm",
    }


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/measures/current"):
            body = json.dumps(sample()).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, *a):
        pass  # quiet


def main():
    global ARGS
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=8080)
    p.add_argument("--scenario", choices=["calm", "spike"], default="calm")
    ARGS = p.parse_args()
    srv = ThreadingHTTPServer(("0.0.0.0", ARGS.port), Handler)
    print(f"mock AirGradient on :{ARGS.port} ({ARGS.scenario}) "
          f"-> http://<ip>:{ARGS.port}/measures/current")
    srv.serve_forever()


if __name__ == "__main__":
    main()
