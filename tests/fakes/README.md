# Stand-ins for the system, for the tests' desktops

`tests/run.sh` puts this folder first on the shell's `PATH` and points the
agents at the test's own D-Bus, so a test desktop never changes this
computer: its speakers, its volume, its password prompts, its updates.

| File | Stands in for | What it does |
|---|---|---|
| `pw-dump` | PipeWire's `pw-dump --monitor` | one output, "Test Speakers" (id 42), at 50%, then waits |
| `pw-play`, `paplay`, `aplay` | the sound players | play nothing; write which sound to `$W95_FAKE_LOG` |
| `wpctl` | WirePlumber's `wpctl` | writes its arguments to `$W95_FAKE_LOG` |
| `polkit-agent-helper-1` | polkit's password checker | asks for the password; `right-password` is right, anything else is wrong |
| `packagekit.py` | PackageKit, on the test's session bus | every check finds two updates: Wayland 95 0.9.9 (a security fix) and one other |
