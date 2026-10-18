---
name: serial-debug
description: Open a dedicated Bash terminal for real-time serial log monitoring. Use this before any serial interaction.
---

# Serial Debug Monitor

Opens a Git Bash terminal that tails `build/serial.log` in real-time (UTF-8, no encoding issues).

All subsequent serial commands automatically append to this log, so the user sees TX/RX as it happens.

## Open Monitor Window

```powershell
Start-Process bash -ArgumentList '-c "echo Serial Monitor — watching build/serial.log && tail -f build/serial.log"'
```

This opens a **new Git Bash window** that stays open. The user can move it to a second screen or minimize it.

## Serial Commands (always log)

All serial interaction writes to `build/serial.log` so the monitor window picks it up:

```bash
# Send + read (with log)
python tools/serial_cli.py --send "cmd\r\n" --read 2.0 --log build/serial.log 2>&1

# Reset + capture boot banner (with log)
python tools/capture_boot.py --wait 4.0 --log build/serial.log 2>&1

# Just read for N seconds
python tools/serial_cli.py --read 3.0 --log build/serial.log 2>&1
```

## Usage Flow

1. Invoke `/serial-debug` → monitor window opens
2. Do serial debugging → all TX/RX visible in monitor
3. Monitor window stays open across sessions until user closes it

## Troubleshooting

| Symptom | Fix |
|---|---|
| bash: command not found | Install Git Bash or use PowerShell: `Get-Content build/serial.log -Wait` |
| Garbled text in monitor | Make sure it's Git Bash, not PowerShell (UTF-8 issue) |
| Log file not found | Run any serial command first to create it |
