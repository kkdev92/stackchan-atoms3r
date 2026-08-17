# Security Policy

## Supported versions

Only `main` is supported. Fixes land there, and picking one up means rebuilding
from a later commit, since no firmware image is published to update. Older commits
and locally built images are not maintained as separate supported versions.

## Reporting a vulnerability

Please use
[GitHub Private Vulnerability Reporting](https://github.com/kkdev92/stackchan-atoms3r/security/advisories/new)
when it is available. If that form is unavailable, contact
<kkdev92.dev@gmail.com> and include `security` in the subject.

Please do not open a public issue before a vulnerability has been assessed.
Remove API tokens, setup passphrases, Wi-Fi credentials, private addresses,
recordings, and raw core dumps from reports.

This project is maintained on a best-effort basis. Reports will be acknowledged
when possible, and progress will be shared with the reporter when a fix can be
prepared safely.

## Security model

The firmware is designed for a device on a trusted local network.

- `POST /api/v1/command` requires a per-device token.
- Device API and gateway traffic use plain HTTP.
- Secure Boot and flash encryption are not enabled.
- Wi-Fi credentials, the API token, and the gateway URL are stored in NVS.
- The device sends a recording only after a button press or an authenticated
  `conversation.start` command.
- The firmware has no telemetry service.

Do not expose the device or the diagnostic gateway directly to the internet.
Anyone able to observe local HTTP traffic may read the token and conversation
data.

## API token

The token is generated on first boot, stored in NVS, and retained across
restarts. It is logged when first created and when rotated. While the device is
idle, holding the button logs the current token for recovery.

Treat serial logs as sensitive. A normal restart does not invalidate a leaked
token.

To replace the token, call `token.rotate`. The old token stops working on the
next request. Rotation is refused while a conversation is active; cancel the
conversation or use `estop.engage` before rotating when immediate action is
required.

Erasing NVS also removes the token, Wi-Fi credentials, and the configured
gateway URL.

## Microphone and gateway access

A client with the API token can call `gateway.configure` and choose where
future user-initiated conversations are sent. Protect the token as access to
the microphone path, not only as an identifier.

The configured gateway URL must use `http://`. Audio and text therefore travel
unencrypted on the local network. Use an isolated or otherwise trusted network
for sensitive conversations. The complete gateway URL is stored and may appear
in serial diagnostics, so do not include credentials or other secrets in it.

## Wi-Fi provisioning

When credentials are absent, the device creates a WPA2 setup access point. Its
per-boot password is shown on the display. The firmware does not log that
password when a display is available.

The provisioning routes are restricted to requests received through the setup
access point. Submitted SSIDs and passwords are stored in NVS and are not
written to normal logs. Once started, the setup access point remains active
until the device restarts, including after a station connection succeeds.

## Physical access

Secure Boot and flash encryption are not enabled in the supplied
configurations. A person with physical access may be able to read firmware,
NVS data, credentials, and information retained in a core dump.

Treat raw flash images and core dumps as sensitive. Enable irreversible
hardware security settings only after evaluating their effect on development,
recovery, and the intended deployment.

## Examples of relevant reports

- A command succeeds without the correct token
- Token generation or comparison is weaker than documented
- Provisioning credentials or setup secrets appear in logs or responses
- A parser reads or writes outside its bounds
- Emergency stop or cancellation fails
- Audio is sent without a user action or authenticated command
- A provisioning route is reachable outside the setup access point
