# Device interface v1

This document describes the interface implemented by the current firmware.
When behavior and documentation differ, please report the mismatch so they can
be corrected together.

The interface uses plain HTTP on a trusted local network. See
[SECURITY.md](../../SECURITY.md) before connecting the device to a shared
network.

## 1. HTTP routes

| Method and path | Authentication | Availability | Purpose |
|---|---|---|---|
| `POST /api/v1/command` | `X-StackChan-Token` | Any active device interface | Run a command |
| `GET /` | None | Setup access point only | Show provisioning instructions |
| `GET /scan` | None | Setup access point only | List cached Wi-Fi scan results |
| `POST /save` | None | Setup access point only | Store Wi-Fi credentials |
| `GET /info` | None | Setup access point only | Return basic device and network information |

The command route returns a JSON result envelope. Application-level errors use
that envelope and normally retain HTTP status 200. Provisioning requests made
outside the setup access point return HTTP 403.

## 2. Command requests

Send JSON to:

```http
POST /api/v1/command
Content-Type: application/json
X-StackChan-Token: <32-character token>
```

Canonical request:

```json
{
  "v": 1,
  "kind": "command",
  "id": "request-1",
  "name": "device.describe",
  "payload": {}
}
```

The current request parser uses `id`, `name`, and `payload`. It accepts
`v` and `kind` for symmetry with responses but does not currently validate
them.

| Field | Current behavior |
|---|---|
| `name` | Required non-empty string; maximum 63 bytes after unescaping |
| `id` | Optional string; maximum 63 bytes after unescaping |
| `payload` | Optional object; an absent payload and `{}` are equivalent; other JSON types are rejected |
| Other fields | Ignored |

The entire request body must be 1–2048 bytes and be a complete JSON object;
leading and trailing whitespace are allowed.
Duplicate object member names and more than 16 nested containers are rejected.

### JSON string support

The firmware uses a bounded scanner for this protocol rather than a general
JSON parser. Send text as UTF-8. The following string escapes are supported:

```text
\"  \\  \/  \b  \f  \n  \r  \t
```

Unicode escape sequences such as `\u3042` are not supported. Configure JSON
serializers to emit UTF-8 characters directly. This also avoids changing
base64 data in gateway events.

### Result envelopes

Success:

```json
{
  "v": 1,
  "kind": "result",
  "id": "request-1",
  "ok": true,
  "payload": {}
}
```

Failure:

```json
{
  "v": 1,
  "kind": "result",
  "id": "request-1",
  "ok": false,
  "error": {
    "code": "invalid_argument",
    "retryable": false
  }
}
```

The optional `message` field is intended for diagnostics. Clients should
branch on `code`, not on message text.

| Error code | Retryable | Meaning |
|---|---:|---|
| `bad_request` | No | Authentication, body, or envelope could not be accepted |
| `unknown_command` | No | No registered command has that name |
| `invalid_argument` | No | A command argument is missing or unsupported |
| `not_found` | No | A referenced item does not exist |
| `unsupported` | No | The command is registered but unavailable on this unit |
| `estop_engaged` | No | Clear the emergency stop before starting activity |
| `busy` | Yes | A required resource is currently in use |
| `unavailable` | Yes | A required service, interface, or device is unavailable |
| `timeout` | Yes | A deadline expired |
| `cancelled` | No | The operation was cancelled |
| `internal` | No | The firmware or stream reached an inconsistent state |

## 3. Registered commands

### `device.describe`

Payload: none.

Returns identity, firmware information, protocol version, and the command
registry. This is an abridged example:

```json
{
  "device_id": "atoms3r-001122334455",
  "boot_id": "<26-character id>",
  "uptime_ms": 1234,
  "firmware": {
    "version": "0.1.0",
    "idf": "<framework version>",
    "build": "<ELF hash prefix>"
  },
  "protocol": 1,
  "capabilities": [
    {"name": "device.describe"},
    {
      "name": "face.set_expression",
      "params": {
        "expression": ["neutral", "happy", "sad", "doubt", "sleepy", "angry"]
      }
    }
  ]
}
```

A capability that depends on unavailable hardware remains listed with
`"available": false` and a `reason`. The `available` field is omitted when
the command is available.

### `device.state`

Payload: none.

```json
{
  "estop": false,
  "conversation": "idle",
  "audio_busy": false,
  "expression": "neutral",
  "network_connected": true,
  "can_start_conversation": false,
  "blocked_by": "gateway url is not configured"
}
```

Conversation phases are `idle`, `listening`, `thinking`, and `speaking`.
`blocked_by` is empty when a conversation can start. Otherwise it is
human-readable diagnostic text, not a stable error code.
`can_start_conversation` and `blocked_by` cover dynamic state only; also check
that the `conversation.start` capability is available on the device.
`audio_busy` is true while a conversation is active, and `expression` is the
resting expression selected by `face.set_expression`.

### `device.health`

Payload: none.

```json
{
  "uptime_ms": 1234,
  "level": "healthy",
  "memory": {
    "internal_free": 100000,
    "largest_internal_block": 60000,
    "psram_free": 7000000
  }
}
```

Health levels are `healthy`, `degraded`, and `critical`.

### `face.set_expression`

```json
{"expression":"happy"}
```

Accepted expressions: `neutral`, `happy`, `sad`, `doubt`, `sleepy`,
and `angry`.

Returns the selected expression. The command returns `unsupported` when the
display is unavailable and `estop_engaged` while the emergency stop is active.

### `conversation.start`

Record and send audio:

```json
{}
```

Send text without recording:

```json
{"text":"Hello"}
```

Recording mode is selected only when the payload is absent or `{}`. When
supplied, `text` must be a non-empty string of at most 480 UTF-8 bytes.
An unreadable, empty, or non-string value is rejected before recording starts.
An accepted request returns:

```json
{"accepted":true,"mode":"listen"}
```

or:

```json
{"accepted":true,"mode":"text"}
```

The result acknowledges that the conversation task accepted the work. Follow
`device.state` or device logs for completion. A conversation requires an
available voice base, a network connection, a configured gateway, an idle
conversation task, and a cleared emergency stop.

### `conversation.cancel`

Payload: none.

```json
{"cancelled":true}
```

`cancelled` is false when no conversation was running. This cancellation does
not remain active for the next conversation.

### `estop.engage`

Payload: none.

```json
{"estop":true}
```

Cancels current work and blocks new conversations and expression changes.

### `estop.clear`

Payload: none.

```json
{"estop":false}
```

Clears the emergency-stop state for the current boot. A device restart also
clears this in-memory state.

### `gateway.configure`

```json
{"url":"http://192.168.1.20:8080"}
```

The URL must:

- start with `http://`
- contain 8–120 bytes
- contain no whitespace or double quote

The device appends `/v1/converse` without normalizing the stored value, so omit
a trailing slash, query, or fragment. Do not include credentials or other
secrets: the URL is stored in NVS and written to device logs. It is returned in
the success payload.

### `token.rotate`

Payload: none.

```json
{"token":"<new 32-character token>"}
```

The request must authenticate with the old token. After success, the new token
applies to the next request and persists across restarts. Rotation returns
`busy` during a conversation and may return `unavailable` when no network
interface is active.

## 4. Provisioning routes

Provisioning routes are accepted only over the setup access point.

### `GET /`

Returns short plain-text instructions for `/scan` and `/save`. It is not an
HTML configuration form.

### `GET /scan`

Returns up to 20 networks cached from the scan performed before the setup
access point started:

```json
[
  {"ssid":"example","rssi":-55}
]
```

The list may be empty. Hidden networks are not included.

### `POST /save`

Maximum body size: 512 bytes.

```json
{"ssid":"example","pass":"secret"}
```

| Field | Requirement |
|---|---|
| `ssid` | Required UTF-8 string, 1–32 bytes |
| `pass` | Optional UTF-8 string, 0–64 bytes |

Success:

```json
{"ok":true}
```

The firmware stores the credentials and starts connecting asynchronously.
An incomplete request body is rejected without changing the stored
credentials.

### `GET /info`

```json
{
  "device_id": "atoms3r-001122334455",
  "version": "0.1.0",
  "ap": true,
  "ip": ""
}
```

## 5. Device-to-gateway conversation

The configured base URL is extended with `/v1/converse`.

### Request

For a recorded conversation:

```http
POST <gateway_url>/v1/converse
Content-Type: audio/wav
Accept: text/event-stream
X-StackChan-Token: <token>
X-StackChan-Device: <device_id>
X-StackChan-Boot: <boot_id>
X-StackChan-Conversation: <conversation_id>
```

The body is a 44-byte WAV header followed by 16 kHz, signed 16-bit,
little-endian mono PCM.

For a text-mode conversation, the same endpoint and headers are used with:

```http
Content-Type: application/json
```

```json
{"text":"Hello"}
```

A gateway should authenticate the token and use the device, boot, and
conversation headers for correlation.

### Response

A successful gateway response uses HTTP 200 and:

```http
Content-Type: text/event-stream; charset=utf-8
```

Each SSE event contains one `data:` field with an event envelope, followed by
a blank line:

```text
data: {"v":1,"kind":"event","name":"conversation.text","payload":{"text":"Hello","final":true}}

```

Every event envelope requires:

- `v: 1`
- `kind: "event"`
- a string `name`
- an object `payload`

Unknown event names are ignored for forward compatibility. SSE comment lines
may be used as keep-alives. One assembled event may contain at most 8192 bytes.

### `conversation.text`

```json
{"text":"recognized text","final":true}
```

`text` is required and may contain at most 512 UTF-8 bytes after unescaping.
`final` defaults to true when omitted.

### `reply.audio`

```json
{
  "seq": 0,
  "text": "[happy]Hello.",
  "rate": 16000,
  "pcm": "<base64 PCM>",
  "last": true
}
```

| Field | Requirement |
|---|---|
| `seq` | Required integer, starting at 0 and increasing by one |
| `rate` | Required integer; only 16000 is supported |
| `pcm` | Required base64, at most 4096 decoded bytes |
| `text` | Optional on continuation chunks; identifies the sentence; at most 512 UTF-8 bytes after unescaping |
| `last` | Optional boolean; flushes the final pending sentence when true |

A sequence gap fails the conversation. Invalid base64 or an odd decoded byte
count also fails the conversation. Events at another sample rate are discarded
and counted.

A `text` value may begin with one of these expression markers:

```text
[neutral] [happy] [sad] [doubt] [sleepy] [angry]
```

The marker is removed from the sentence text and selects the expression used for
that sentence. An unrecognized bracketed marker remains ordinary text.

### `error.raised`

```json
{"code":"unavailable","message":"optional detail","retryable":true}
```

The firmware records the recognized error `code`. Complete a failed stream
with `conversation.finished` and `reason: "failed"`.

### `conversation.finished`

```json
{"reason":"completed"}
```

Supported reasons are `completed`, `cancelled`, and `failed`. This event
ends the conversation and must be the final semantic event. Closing the stream
without it fails the conversation.

The device ignores `data: [DONE]` if received, but it does not treat that
value as completion.

### Timing and cancellation

- Response headers must arrive within 10 seconds.
- Once streaming begins, the connection is closed after 30 seconds without
  received data.
- The device reads in intervals of up to one second so cancellation can be
  observed.
- Cancelling or engaging the emergency stop closes the gateway connection.
- The device command server remains available while the conversation task is
  recording, waiting, or playing.

## 6. Compatibility

Protocol version 1 is raised only for breaking envelope or semantic changes.
Receivers should ignore unknown object fields and unknown gateway event names.

Changes to required fields, field meaning, authentication, sample format, or
completion behavior require corresponding firmware tests and documentation
updates.
