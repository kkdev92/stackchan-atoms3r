## Summary

Describe what changed and why.

## Related issue

Fixes #

## Verification

Check the items that apply. It is fine to leave an item unchecked and explain
why it was not available in your environment.

- [ ] `python tools/check-invariants.py`
- [ ] `pio test -e native`
- [ ] `pio check -e native`
- [ ] `python tools/run-clang-tidy.py`
- [ ] Firmware environment(s) built:
- [ ] QEMU startup check
- [ ] Physical hardware check; board and base:

## Interface and documentation

- [ ] User-visible behavior is documented
- [ ] Device-interface changes include matching examples and tests
- [ ] Hardware facts include the tested revision and method
- [ ] No token, credential, passphrase, private address, or sensitive dump is
      included

## Notes for reviewers

List remaining risks, checks, or follow-up work.
