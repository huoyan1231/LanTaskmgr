# LanTaskmgr

Kill runaway processes on your PC from your phone's browser.

When something on Windows pins the CPU and the desktop stops repainting, the
task manager is exactly the thing you can no longer open. LanTaskmgr keeps a
tiny HTTP server running on your LAN so you can pull up a process list on your
phone and terminate the offender without touching the frozen machine.

This is a functional rewrite of
[Run Task Manager On Your Phone](https://github.com/gordonwalkedby/RunTaskManagerOnYourPhone)
by Gordon Walkedby, reimplemented in C against the raw Win32 API.

## Why C

The original is VB.NET/WinForms and drags in the whole .NET Framework plus
Mono's `HttpListener`, `Newtonsoft.Json` and a QR library — roughly 2.5 MB of
binaries and a 30-60 MB working set for a program whose entire job is to sit
idle until the day you need it.

Since this tool is meant to be resident 24/7 and, crucially, to still respond
when the machine is already out of resources, every byte of its footprint works
against its own purpose. So it is written the other way round:

| | original | LanTaskmgr |
|---|---|---|
| language | VB.NET | C11 |
| runtime dependency | .NET Framework | none |
| files to ship | 10 (`.exe` + 5 DLLs + web folder + languages) | 1 (`LanTaskmgr.exe`) |
| JSON | Newtonsoft.Json | hand-written serialiser |
| HTTP | Mono.Net.HttpListener | Winsock + `select()` |
| QR code | Gma.QrCodeNet | built-in encoder |
| threads | thread per request | 1 UI + 1 network |

Everything the browser loads (HTML, CSS, JS) is embedded in the executable as
`RCDATA` and served straight from the mapped PE image — no file I/O and no heap
copy per request.

## Building

Needs MSVC (any Visual Studio edition or the standalone Build Tools with the
"Desktop development with C++" workload). No other dependency, no package
manager, no CMake.

```bat
build.bat            :: release, x64
build.bat debug      :: /Od /Zi
build.bat clean
```

The result is `build\LanTaskmgr.exe`, statically linked against the CRT so it
runs on a bare Windows install.

## Usage

Run the executable. The window shows the LAN addresses this PC is reachable on,
a QR code for the first one, and the settings.

1. Note the password (a random 6-digit one is generated on first run) or set
   your own.
2. Scan the QR code with your phone, on the same network.
3. Log in, tap a process, confirm.

Tick **Start with Windows** so it is already listening the next time something
locks up. Closing the window hides the program to the tray; use the tray menu
to quit for real.

### What the phone sees

Processes are grouped by image name and colour coded:

* **blue** — has a visible window (this is usually what you came to kill)
* **grey** — a normal background process
* **red** — a session-0 / system process

Each row shows the private working set and the CPU share since the previous
poll. The list refreshes every two seconds and can be sorted by memory, CPU or
name, or filtered by typing.

A short list of processes that instantly bugcheck or reboot Windows when
terminated (`csrss`, `wininit`, `smss`, `services`, `lsass`, `winlogon`, …) is
refused by the server rather than merely warned about.

## Configuration

`settings.ini`, written next to the executable (or in
`%APPDATA%\LanTaskmgr\` when the executable lives somewhere read-only):

```ini
[LanTaskmgr]
Port=5555
Password=428913
Language=CN        ; EN | CN | TW
AutoStart=0
StartHidden=0
```

`lantaskmgr.log` records service start/stop, new devices and kill attempts, and
rotates at 1 MB.

## Security, honestly

This is a plain-HTTP LAN tool that terminates arbitrary processes. Treat it
accordingly:

* Traffic is **not** encrypted. Anyone sharing the network can see the session
  cookie, so do not run it on café or hotel Wi-Fi.
* The password is stored in clear text in `settings.ini`, and is checked in
  constant time against a random session token issued on login.
* Five failed logins block that IP until the service is restarted.
* Never forward the port through your router.

## Differences from the original

* Added: memory and CPU per process, sorting/filtering, dark mode, minimise to
  tray, random first-run password.
* Dropped: the built-in update check (there is no update server for this
  rewrite) and the external `Languages\*.xml` folder, which is now compiled in.

## Licence

MIT. See [LICENSE](LICENSE).
