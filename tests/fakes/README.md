# Stand-ins for the system, for the tests' desktops

`tests/run.sh` puts this folder first on the shell's `PATH` and points the
agents at the test's own D-Bus, so a test desktop never changes this
computer: its speakers, its volume, its password prompts, its updates.

| File | Stands in for | What it does |
|---|---|---|
| `pw-dump` | PipeWire's `pw-dump --monitor` | one output, "Test Speakers" (id 42), at 50%, then waits |
| `pw-play`, `paplay`, `aplay` | the sound players | play nothing; write which sound to `$W95_FAKE_LOG` |
| `nmcli` | NetworkManager's `nmcli` | only with `W95_FAKE_NETWORK` set (a folder of answers: a connection, its addresses, a WireGuard tunnel); otherwise the real one, read-only |
| `wpctl` | WirePlumber's `wpctl` | writes its arguments to `$W95_FAKE_LOG` |
| `unix_chkpwd` | PAM's password checker (the screen saver's password) | `right-password` is right, anything else is wrong |
| `polkit-agent-helper-1` | polkit's password checker | asks for the password; `right-password` is right, anything else is wrong |
| `logind.py` | logind and hostname1, on the test's session bus | a laptop that can sleep; the test opens and closes its lid (SetLid); calls are logged |
| `timedate.py` | systemd-timedated, on the test's session bus | the computer's time zone (America/New_York to start); SetTimezone changes it and says so; calls are logged |
| `udisks.py` | UDisks2, on the test's session bus | a USB stick, STICK, that "mounts" as a folder under `$W95_FAKE_MEDIA` and powers off; the computer's own disk (a system one, never listed); Plug(label) and Pull(label) plug one in and pull one out; a file `busy` next to the log makes Unmount fail as it does with files open; calls are logged |
| `packagekit.py` | PackageKit, on the test's session bus | every check finds two updates: Wayland 95 0.9.9 (a security fix) and one other |
