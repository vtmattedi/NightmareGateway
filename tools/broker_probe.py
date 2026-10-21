#!/usr/bin/env python3
"""Probes the remote MQTT broker the way the gateway uses it.

Two probes run side by side, both configured from include/creds.h:

  connect  every --interval s: DNS, TCP and TLS handshake to a fresh socket.
           Catches the broker or the path being unreachable.
  hold     one long-lived, authenticated MQTT session that sends PINGREQ every
           --ping s. Catches what `connect` can't: an established connection
           that silently stops being answered (NAT/proxy dropping the flow,
           a stalled broker), which is how the gateway loses its session.

A third one watches the gateway itself, which tells a dead Wi-Fi path from a
dead TCP session:

  gateway  ICMP echo to each --gateway host every --gw-interval s. If these
           replies stop at the moment the gateway logs `No PING_RESP`, the radio
           path blacked out; if they keep coming, the stall is above it. Add the
           router too (`--gateway nightmare-gateway.local --gateway 10.10.3.1`):
           when both stop, it is this PC's own link, not the gateway's.

Prints (and logs) only when something is wrong: a failure, a slow stage or ping,
a lost ping, or a dropped session with its age. Times are local and UTC (the
broker log is UTC).

    python tools/broker_probe.py [--interval 10] [--ping 5] [--slow 1.0] [--no-hold]
                                 [--gateway HOST ...] [--gw-interval 1] [--gw-big 1400] [--no-gateway]
"""
import argparse
import os
import re
import socket
import ssl
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def load_creds(path):
    text = path.read_text(encoding="utf-8")

    def define(name, pattern):
        match = re.search(rf"#define\s+{name}\s+{pattern}", text)
        return match.group(1) if match else None

    host = define("REMOTE_MQTT_URL", r'"([^"]+)"')
    port = int(define("REMOTE_MQTT_PORT", r"(\d+)"))
    user = define("MQTT_USERA", r'"([^"]*)"')
    password = define("MQTT_PASSWDA", r'"([^"]*)"')
    ca = None
    block = re.search(r"#define\s+ROOT_CA\s*((?:\\\s*\n)?(?:\s*\"[^\n]*\"\s*\\?\s*\n?)+)", text)
    if block:
        ca = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', block.group(1))).replace("\\n", "\n")
    return host, port, ca, user, password


def stamp():
    now = datetime.now().astimezone()
    return f"{now:%Y-%m-%d %H:%M:%S} (UTC {now.astimezone(timezone.utc):%H:%M:%S})"


class Reporter:
    def __init__(self, log_path):
        log_path.parent.mkdir(parents=True, exist_ok=True)
        self.log = open(log_path, "a", encoding="utf-8", buffering=1)
        self.lock = threading.Lock()

    def __call__(self, message):
        line = f"{stamp()}  {message}"
        with self.lock:
            print(line, flush=True)
            self.log.write(line + "\n")


def make_context(ca):
    return ssl.create_default_context(cadata=ca) if ca else ssl.create_default_context()


# --- connect probe -------------------------------------------------------------

def probe_connect(host, port, ca, timeout):
    """Returns (ok, timings, failure). Timings are seconds per completed stage."""
    timings = {}
    started = time.perf_counter()
    try:
        address = socket.getaddrinfo(host, port, socket.AF_INET, socket.SOCK_STREAM)[0][4]
    except OSError as error:
        return False, timings, f"dns failed: {error}"
    timings["dns"] = time.perf_counter() - started

    started = time.perf_counter()
    try:
        raw = socket.create_connection(address, timeout=timeout)
    except OSError as error:
        return False, timings, f"tcp connect to {address[0]} failed after {time.perf_counter() - started:.1f}s: {error}"
    timings["tcp"] = time.perf_counter() - started

    started = time.perf_counter()
    try:
        raw.settimeout(timeout)
        with make_context(ca).wrap_socket(raw, server_hostname=host) as tls:
            timings["tls"] = time.perf_counter() - started
            timings["version"] = tls.version()
    except (OSError, ssl.SSLError) as error:
        raw.close()
        return False, timings, f"tls handshake failed after {time.perf_counter() - started:.1f}s: {error}"
    timings["ip"] = address[0]
    return True, timings, None


def run_connect(args, host, port, ca, report):
    failing_since = None
    failures = 0
    while True:
        cycle = time.monotonic()
        ok, timings, failure = probe_connect(host, port, ca, args.timeout)
        if not ok:
            failures += 1
            failing_since = failing_since or time.monotonic()
            report(f"[connect] FAIL #{failures}: {failure}")
        else:
            if failing_since is not None:
                report(f"[connect] RECOVERED after {failures} failed probe(s), {time.monotonic() - failing_since:.0f}s down "
                       f"(tcp {timings['tcp']:.2f}s, tls {timings['tls']:.2f}s)")
                failing_since, failures = None, 0
            slow = [f"{stage} {timings[stage]:.2f}s" for stage in ("dns", "tcp", "tls") if timings[stage] > args.slow]
            if slow:
                report(f"[connect] SLOW: {', '.join(slow)} ({timings['ip']}, {timings['version']})")
        time.sleep(max(0.0, args.interval - (time.monotonic() - cycle)))


# --- hold probe: a minimal MQTT 3.1.1 client -----------------------------------

def mqtt_string(text):
    data = text.encode()
    return len(data).to_bytes(2, "big") + data


def mqtt_length(length):
    out = bytearray()
    while True:
        digit, length = length % 128, length // 128
        out.append(digit | (0x80 if length else 0))
        if not length:
            return bytes(out)


def mqtt_connect_packet(client_id, user, password, keepalive):
    flags = 0x02 | (0x80 if user else 0) | (0x40 if password else 0)
    body = mqtt_string("MQTT") + bytes([4, flags]) + keepalive.to_bytes(2, "big") + mqtt_string(client_id)
    if user:
        body += mqtt_string(user)
    if password:
        body += mqtt_string(password)
    return b"\x10" + mqtt_length(len(body)) + body


def read_exact(sock, count):
    data = b""
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise ConnectionResetError("closed by peer")
        data += chunk
    return data


def read_packet_type(sock):
    """Reads one packet and returns its type nibble; the body is discarded."""
    first = read_exact(sock, 1)[0]
    length, shift = 0, 0
    while True:
        digit = read_exact(sock, 1)[0]
        length |= (digit & 0x7F) << shift
        shift += 7
        if not digit & 0x80:
            break
    if length:
        read_exact(sock, length)
    return first >> 4


def run_hold(args, host, port, ca, user, password, report):
    client_id = f"broker-probe-{os.getpid()}"
    context = make_context(ca)
    down_since = None
    attempts = 0

    while True:
        sock = None
        opened = None
        pings = 0
        try:
            started = time.perf_counter()
            raw = socket.create_connection((host, port), timeout=args.timeout)
            raw.settimeout(args.timeout)
            sock = context.wrap_socket(raw, server_hostname=host)
            sock.sendall(mqtt_connect_packet(client_id, user, password, 30))
            if read_packet_type(sock) != 2:
                raise ConnectionError("no CONNACK")
            if down_since is not None:
                report(f"[hold] session re-established after {attempts} attempt(s), "
                       f"{time.monotonic() - down_since:.0f}s without one "
                       f"(connect+login {time.perf_counter() - started:.2f}s)")
                down_since, attempts = None, 0
            opened = time.monotonic()

            while True:
                time.sleep(args.ping)
                sent = time.perf_counter()
                sock.sendall(b"\xc0\x00")
                sock.settimeout(args.ping_timeout)
                try:
                    kind = read_packet_type(sock)
                except socket.timeout:
                    raise TimeoutError(f"no PINGRESP within {args.ping_timeout:g}s") from None
                if kind != 13:
                    continue
                pings += 1
                rtt = time.perf_counter() - sent
                if rtt > args.slow:
                    report(f"[hold] SLOW ping {rtt:.2f}s (session age {time.monotonic() - opened:.0f}s, ping #{pings})")
        except (OSError, ssl.SSLError, ConnectionError) as error:
            if down_since is None:
                down_since = time.monotonic()
                where = (f"session age {time.monotonic() - opened:.0f}s, {pings} ping(s) answered"
                         if opened is not None else "while connecting")
                report(f"[hold] SESSION LOST ({error.__class__.__name__}: {error}) {where}")
            attempts += 1
        finally:
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass
        time.sleep(args.ping)


# --- gateway probe: ICMP echo --------------------------------------------------

def ping_once(address, timeout, size=0):
    """One echo through the system ping. Returns the round trip in seconds, or
    None for no reply. A "Destination host unreachable" reply is a failure too,
    which is why success needs a TTL (Windows exits 0 for those). `size` is the
    payload in bytes (0 = the system default of 32/56): a link can answer small
    echoes fine while dropping the full-size frames TLS records travel in."""
    if sys.platform == "win32":
        command = ["ping", "-n", "1", "-w", str(int(timeout * 1000))]
        if size:
            command += ["-l", str(size)]
        flags = subprocess.CREATE_NO_WINDOW
    else:
        command = ["ping", "-c", "1", "-W", str(max(1, round(timeout)))]
        if size:
            command += ["-s", str(size)]
        flags = 0
    command.append(address)
    started = time.perf_counter()
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout + 3,
                                creationflags=flags)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if not re.search(r"TTL=", result.stdout, re.IGNORECASE):
        return None
    # "time=3ms", "time<1ms" or a translated word ("tempo=3ms"); the number is
    # the ping's own measurement, without this process's start-up cost.
    match = re.search(r"[=<]\s*(\d+(?:\.\d+)?)\s*ms", result.stdout)
    return float(match.group(1)) / 1000 if match else time.perf_counter() - started


def run_gateway(args, host, report, size=0):
    label = f"{host} big" if size else host
    missed = 0
    lost_since = None
    unresolved = False
    while True:
        cycle = time.monotonic()
        try:
            address = socket.gethostbyname(host)
            if unresolved:
                report(f"[gateway {label}] name resolves again: {address}")
                unresolved = False
        except OSError:
            if not unresolved:
                report(f"[gateway {label}] cannot resolve the name; use the IP from the gateway log")
                unresolved = True
            time.sleep(args.gw_interval)
            continue

        rtt = ping_once(address, args.gw_timeout, size)
        if rtt is None:
            missed += 1
            if lost_since is None:
                lost_since = time.monotonic()
                report(f"[gateway {label}] NO REPLY from {address}" + (f" ({size}-byte echo)" if size else ""))
        else:
            if lost_since is not None:
                report(f"[gateway {label}] replying again after {missed} missed, "
                       f"{time.monotonic() - lost_since:.0f}s without a reply")
                lost_since, missed = None, 0
            if rtt > args.gw_slow:
                report(f"[gateway {label}] SLOW reply {rtt * 1000:.0f}ms")
        time.sleep(max(0.0, args.gw_interval - (time.monotonic() - cycle)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--creds", type=Path, default=ROOT / "include" / "creds.h")
    parser.add_argument("--interval", type=float, default=10.0, help="seconds between connect probes")
    parser.add_argument("--ping", type=float, default=5.0, help="seconds between pings on the held session")
    parser.add_argument("--ping-timeout", type=float, default=10.0, help="PINGRESP wait before the session counts as lost")
    parser.add_argument("--timeout", type=float, default=10.0, help="per connect stage; the gateway uses 10")
    parser.add_argument("--slow", type=float, default=1.0, help="report a stage or ping slower than this")
    parser.add_argument("--no-hold", action="store_true", help="skip the held-session probe")
    parser.add_argument("--gateway", action="append", metavar="HOST",
                        help="host to ICMP-ping (repeatable); default nightmare-gateway.local, "
                             "the name the gateway advertises over mDNS")
    parser.add_argument("--gw-interval", type=float, default=1.0, help="seconds between echoes")
    parser.add_argument("--gw-timeout", type=float, default=1.0, help="echo wait before it counts as missed")
    parser.add_argument("--gw-slow", type=float, default=0.3, help="report an echo slower than this")
    parser.add_argument("--gw-big", type=int, default=1400, metavar="BYTES",
                        help="also send echoes with this payload size (default 1400, about one TLS record "
                             "per TCP segment); 0 disables")
    parser.add_argument("--no-gateway", action="store_true", help="skip the ICMP probe")
    parser.add_argument("--log", type=Path, default=ROOT / "logs" / "broker_probe.log")
    args = parser.parse_args()

    gateways = [] if args.no_gateway else (args.gateway or ["nightmare-gateway.local"])
    host, port, ca, user, password = load_creds(args.creds)
    report = Reporter(args.log)
    report(f"probe started: {host}:{port}, connect every {args.interval:g}s"
           + ("" if args.no_hold else f", held session pinging every {args.ping:g}s as '{user}'")
           + (f", ICMP every {args.gw_interval:g}s to {', '.join(gateways)}"
              + (f" (+{args.gw_big}-byte echoes)" if args.gw_big else "") if gateways else "")
           + f", slow>{args.slow:g}s, CA {'from creds.h' if ca else 'NOT found (system store)'}")

    for gateway in gateways:
        threading.Thread(target=run_gateway, args=(args, gateway, report), daemon=True).start()
        if args.gw_big:
            threading.Thread(target=run_gateway, args=(args, gateway, report, args.gw_big), daemon=True).start()
    if not args.no_hold:
        threading.Thread(target=run_hold, args=(args, host, port, ca, user, password, report), daemon=True).start()
    run_connect(args, host, port, ca, report)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
