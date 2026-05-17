# `sikradio`

`sikradio` is a simple internet radio client written in C.  
It connects to online radio streams over HTTP or HTTPS, receives MP3 audio data, and sends the audio directly to `stdout`.  
The program can also receive ICY metadata (for example current song titles) and print it to `stderr`.

The client supports:

- HTTP and HTTPS streams
- IPv4 and IPv6
- ICY/Icecast/SHOUTcast servers
- redirects (`301`, `302`, etc.)
- ICY metadata (`StreamTitle`)
- automatic reconnect after timeout
- graceful quit using the `quit` command

The project uses low-level TCP sockets and OpenSSL for TLS support.

---

# Building

The project uses a simple `Makefile`.

Build the program:

```bash
make
```

This creates the executable:

```text
sikradio
```

Remove build files:

```bash
make clean
```

---

# Usage

Basic syntax:

```bash
./sikradio -u URL [options]
```

Example:

```bash
./sikradio -u http://stream.radiobaobab.pl:8000/radiobaobab.mp3
```

To actually hear the audio, pipe the output to a player such as `mpv`:

```bash
./sikradio -u http://stream.radiobaobab.pl:8000/radiobaobab.mp3 | mpv --really-quiet -
```

Example with metadata enabled:

```bash
./sikradio -u https://stream.nowyswiat.online/mp3 -m | mpv --really-quiet -
```

---

# Command Line Options

| Option | Description |
|---|---|
| `-u URL` | Stream URL (required) |
| `-m` | Request ICY metadata from server |
| `-t TIMEOUT` | Timeout in milliseconds before reconnect |
| `-4` | Force IPv4 |
| `-6` | Force IPv6 |
| `-v LEVEL` | Verbosity level `0-4` |
| `-q` | Shortcut for `-v0` |

Examples:

```bash
./sikradio -u http://example.com/stream.mp3 -m
```

```bash
./sikradio -u https://example.com/stream -t 3500
```

```bash
./sikradio -u http://example.com/stream -4
```

---

# Output Behavior

The program strictly separates audio data and diagnostics:

| Stream | Contents |
|---|---|
| `stdout` | Raw audio stream |
| `stderr` | Logs, errors, ICY metadata |

This allows piping audio safely into external players without corrupting the stream.

Example:

```bash
./sikradio -u http://example.com/stream | mpv --really-quiet -
```

---

# ICY Metadata

When the `-m` option is enabled, the client requests ICY metadata from the server.

Example metadata:

```text
StreamTitle='Louis Armstrong - La Vie En Rose';
```

Metadata is printed to `stderr`, while audio continues to be written to `stdout`.

---

# Verbosity Levels

| Level | Description |
|---|---|
| `0` | No diagnostic output |
| `1` | Communication logs |
| `2` | Critical errors |
| `3` | Non-critical errors |
| `4` | Debug information |

Example logs:

```text
2026.05.13 18.26.51
resolving name stream.radiobaobab.pl
connecting to server 94.23.88.220:8000
```

---

# Redirect Support

`sikradio` automatically follows HTTP redirects and preserves ICY metadata requests across redirects.

Example redirect flow:

```text
HTTP/1.0 302 Found
Location: http://n44a-eu.rcs.revma.com/...
```

The client reconnects to the new address automatically.

---

# Timeout and Reconnect

If no data is received for the configured timeout period, the client reconnects automatically.

Example:

```text
data receiving timeout
```

---

# Stopping the Program

You can stop the client in two ways:

1. The server closes the connection
2. You type:

```text
quit
```

and press Enter.

---

# Project Structure

Main source files:

```text
main.c
config.c
url.c
io.c
net.c
http.c
icy.c
stream.c
client.c
sikradio.h
```

Main responsibilities:

| File | Purpose |
|---|---|
| `config.c` | Command-line parsing |
| `url.c` | URL and redirect parsing |
| `net.c` | TCP and TLS connections |
| `http.c` | HTTP request/response handling |
| `icy.c` | ICY metadata demultiplexing |
| `stream.c` | Streaming loop and timeout handling |
| `client.c` | Main client logic |

---

# Requirements

- GCC
- POSIX-compatible system
- OpenSSL (`libssl`, `libcrypto`)

Example compile flags:

```text
-lssl -lcrypto
```

---

# Notes

- Audio data is never interpreted by the client.
- The client only forwards received bytes.
- External software is responsible for decoding and playback.
- The implementation focuses on correctness, stream stability, and clean separation of audio and diagnostics.
