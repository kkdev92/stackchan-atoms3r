---
name: Bug Report
about: Report a problem with stackchan-atoms3r
title: '[Bug] '
labels: bug
assignees: ''
---

Thank you for taking the time to report a problem.

Before posting, remove API tokens, Wi-Fi credentials, setup passphrases,
private addresses, recordings, and sensitive core-dump content. For a security
issue, use the private process in [SECURITY.md](../../SECURITY.md).

## What happened?

Describe the problem and its impact.

## Steps to reproduce

1.
2.
3.

## What did you expect?

## Environment

- Board and base:
- Firmware environment:
- Commit (`git rev-parse --short HEAD`):
- Host OS:
- PlatformIO version (`pio --version`):

## Device information

If available, include the redacted response from `device.describe`.

```json
```

## Logs

Include the relevant portion of the startup or command log. Remove secrets
before posting.

```text
```

## Additional checks

These are optional but can help narrow the problem:

- [ ] Reproduces in the QEMU boot environment
- [ ] Reproduces only on hardware
- [ ] Not tested in QEMU

QEMU does not emulate Wi-Fi, the display, I2C, or audio, so hardware-dependent
behavior cannot be reproduced there.

## Additional context
