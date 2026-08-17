#!/usr/bin/env python3
"""Run a local diagnostic gateway for the device conversation path.

The server accepts POST /v1/converse and returns two generated tones as
Server-Sent Events. It can be used to check recording, HTTP transport, event
parsing, playback, and expression changes without installing speech services.

Usage:
    python tools/echo-gateway.py
    python tools/echo-gateway.py 8081

Configure the printed base URL with gateway.configure, then press the device
button once. Keep this diagnostic server on a trusted local network.

See docs/api/device-interface.md for the complete protocol.
"""

import argparse
import base64
import json
import math
import socket
import struct
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = 8080

SAMPLE_RATE = 16000
# The contract caps one event at 4096 bytes of audio, which is 2048 samples.
MAX_SAMPLES_PER_EVENT = 2048

# What to say back. Two sentences, two expressions, so that the face is seen
# changing partway through rather than only once at the start.
REPLY = [
    ("[happy]Diagnostic reply received.", 660),
    ("[sad]This reply uses generated tones.", 440),
]
SENTENCE_MS = 900


def tone(hz, milliseconds):
    """Signed 16-bit mono PCM: a tone that fades in and out.

    Fading matters. Starting or stopping a waveform at full amplitude leaves
    a step, which is audible as a click and sounds like a fault.
    """
    total = SAMPLE_RATE * milliseconds // 1000
    fade = SAMPLE_RATE // 100  # 10 ms
    out = bytearray()
    for i in range(total):
        gain = min(i, total - 1 - i, fade) / fade
        value = math.sin(2 * math.pi * hz * i / SAMPLE_RATE) * 6000 * gain
        out += struct.pack("<h", int(value))
    return bytes(out)


def event(name, payload):
    """One Server-Sent Event carrying one envelope.

    ensure_ascii stays False because the device expects UTF-8 text rather
    than Unicode escape sequences.
    """
    envelope = {"v": 1, "kind": "event", "name": name, "payload": payload}
    body = json.dumps(envelope, ensure_ascii=False, separators=(",", ":"))
    return ("data: " + body + "\n\n").encode("utf-8")


def reply_events():
    """The whole reply, as a list of events ready to send."""
    events = [event("conversation.text", {"text": "(not transcribed)", "final": True})]

    chunks = []
    for text, hz in REPLY:
        pcm = tone(hz, SENTENCE_MS)
        for offset in range(0, len(pcm), MAX_SAMPLES_PER_EVENT * 2):
            chunks.append((text, pcm[offset:offset + MAX_SAMPLES_PER_EVENT * 2]))
            # Only the first chunk of a sentence carries its text; repeating
            # it would announce the same sentence several times.
            text = None

    for seq, (text, pcm) in enumerate(chunks):
        payload = {
            "seq": seq,
            "rate": SAMPLE_RATE,
            "pcm": base64.b64encode(pcm).decode("ascii"),
            "last": seq == len(chunks) - 1,
        }
        if text is not None:
            payload["text"] = text
        events.append(event("reply.audio", payload))

    # Required. Without it the device treats the stream as a contract
    # violation and reports the conversation as failed.
    events.append(event("conversation.finished", {"reason": "completed"}))
    return events


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_POST(self):
        if self.path != "/v1/converse":
            self.send_error(404)
            return

        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length) if length else b""

        kind = self.headers.get("Content-Type", "")
        if kind.startswith("application/json"):
            try:
                said = json.loads(body).get("text", "")
            except ValueError:
                said = "(unreadable)"
            heard = "text: " + said
        else:
            # 44 bytes of WAV header, then signed 16-bit samples.
            seconds = max(0, len(body) - 44) / (SAMPLE_RATE * 2)
            heard = "audio: %.1f s" % seconds

        print("  %s from %s" % (heard, self.headers.get("X-StackChan-Device", "?")))

        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()

        # Sent as one write. The device reassembles the stream however it
        # arrives, so there is nothing to gain from pacing it here.
        self.wfile.write(b"".join(reply_events()))
        self.wfile.flush()

    def log_message(self, fmt, *args):
        pass  # the line printed above says more than the access log would


def local_addresses():
    """Addresses the device might reach this on."""
    found = []
    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.connect(("192.0.2.1", 1))  # goes nowhere; reveals the route
        found.append(probe.getsockname()[0])
        probe.close()
    except OSError:
        pass
    return found or ["<this machine>"]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("port", nargs="?", type=int, default=PORT,
                        help="listen port (default: %(default)s)")
    arguments = parser.parse_args()
    port = arguments.port
    server = ThreadingHTTPServer(("0.0.0.0", port), Handler)

    print("echo gateway listening on port %d" % port)
    for address in local_addresses():
        print("  point the device at  http://%s:%d" % (address, port))
    print("  then press its button once. Ctrl-C to stop.")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main()
