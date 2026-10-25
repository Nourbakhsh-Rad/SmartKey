# Contributing to SmartKey

Thanks for helping! Bug reports, fixes, new scanners and examples are welcome.

## Development setup

The library is header-only C++11 (`src/`). All logic takes the time as an argument and
never calls `millis()`, so it is tested on the PC:

```bash
pip install platformio
pio test -e native            # unit tests (Unity)
python3 scripts/check_version.py
```

Please keep the build free of warnings (`-Wall -Wextra`) and add or update a test for every
behaviour change. Tests live in `test/test_smartkey.cpp` and use a simulated clock
(see the `run()` helpers at the top of the file).

## Guidelines

- No dynamic allocation (`new`, `malloc`, `String`) in the library.
- Never call `millis()` / `micros()` inside the library: take the time as a parameter. Convenience
  overloads without a time argument are allowed only inside `#ifdef ARDUINO`.
- Never call user callbacks while interrupts are disabled.
- Comments, documentation and example text are written in English.
- Every example is a sketch in its own folder (`examples/<Group>/<Name>/<Name>.ino`).
- Update `CHANGELOG.md` (Keep a Changelog format) and, for user-visible changes, `ReadMe.md`.

## Pull requests

1. Fork and create a branch.
2. Make the change, add tests, run `pio test -e native`.
3. Open the pull request and describe the problem and the behaviour change.

## Releasing (maintainers)

1. Choose the new version (semantic versioning).
2. Update it in **all** of these places, then run `python3 scripts/check_version.py`:
   `library.properties`, `library.json`, `src/SmartKeyCore.h`
   (`SMARTKEY_VERSION*`), the newest heading of `CHANGELOG.md`, and the version badge in `ReadMe.md`.
3. Run the unit tests and build a few examples.
4. Commit, tag (`git tag v2.2.0`) and push the tag (`git push origin v2.2.0`).
5. Create a GitHub release from the tag and paste the CHANGELOG section.
6. First release only: submit the repository to the Arduino Library Manager index
   (https://github.com/arduino/library-registry) and to the PlatformIO registry
   (`pio pkg publish`). Later tags are picked up automatically by the Arduino index.
