#!/usr/bin/env python3
"""MonoPod Wi-Fi Sync v0.10.7.3: interactive consent, authorized session.
Requires: python -m pip install requests
Discovery may use mDNS _monopod._tcp.local; pass the address explicitly here.
"""
import argparse
import time
import requests


def connect(base: str, name: str = "MonoPod Desktop", timeout_s: int = 35):
    base = base.rstrip("/")
    session = requests.Session()
    result = session.post(base + "/api/sync/request", data={"client": name}, timeout=4)
    result.raise_for_status()
    if result.json().get("state") != "pending":
        raise RuntimeError(f"Unexpected consent state: {result.text}")
    end = time.monotonic() + timeout_s
    while time.monotonic() < end:
        time.sleep(0.75)
        status = session.get(base + "/api/sync/status", timeout=4)
        status.raise_for_status()
        state = status.json().get("state")
        if state == "approved":
            token = status.json().get("session")
            if not token:
                raise RuntimeError("Missing session token")
            session.headers.update({"X-MonoPod-Session": token})
            return session
        if state in ("denied", "idle"):
            raise PermissionError("MonoPod denied or timed out the connection")
    raise TimeoutError("No confirmation on MonoPod screen")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base_url", help="e.g. http://192.168.1.50")
    parser.add_argument("--client", default="MonoPod Desktop")
    args = parser.parse_args()
    with connect(args.base_url, args.client) as session:
        print("Approved:", session.get(args.base_url.rstrip("/") + "/api/info", timeout=6).json())
        input("Press Enter to end the session: ")
        r = session.post(args.base_url.rstrip("/") + "/api/sync/end", timeout=5)
        print("Ended:", r.status_code, r.text)

if __name__ == "__main__":
    main()
