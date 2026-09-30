# Ramora — Setup Guide

Ramora is a zero-dependency C project. You only need a Linux machine (epoll is Linux-only) with `gcc` and `make`.

## 1. Prerequisites

- Linux (native or WSL2 — epoll does not exist on macOS/Windows natively)
- `gcc` (or any C11-compatible compiler)
- `make`
- `valgrind` (optional, recommended for memory-safety work)

Check you have the toolchain:

```bash
gcc --version
make --version
```

## 2. Build

From the repository root:

```bash
make
```

This produces three binaries in the repo root:

- `ramora-server`
- `ramora-client`
- `ramora-test`

To clean build artifacts:

```bash
make clean
```

## 3. Run the server

With the default, built-in config (binds `127.0.0.1:5000`):

```bash
./ramora-server
```

Or with the example config file (binds `127.0.0.1:6969`, see `conf/ramora.conf`):

```bash
./ramora-server conf/ramora.conf
```

Edit `conf/ramora.conf` (or write your own) to change bind address/port, hashmap sizing, buffer limits, idle timeout, etc. — see the "Configuration" section in [README.md](README.md).

> `logfile` in the conf points at `/var/log/ramora/ramora.log` by default, which usually needs `sudo`/pre-created directory permissions. Point it at a path you own while developing, e.g. `logfile ./ramora.log`.

## 4. Talk to it

In a second terminal:

```bash
./ramora-client
```

This connects to `127.0.0.1:5000` by default. To hit a custom host/port:

```bash
./ramora-client -h 127.0.0.1 -p 6969
```

Inside the client you can issue commands such as:

```
PING
SET foo bar
GET foo
TTL foo 30
TTL foo
DEL foo
```

## 5. Run the benchmark / test client

```bash
./ramora-test -h
```

shows all flags (client count, pipeline depth, op count, key space, etc.). This is also how the numbers in `tests/testing_report.txt` were produced — feel free to regenerate them after you make changes.

## 6. Memory-safety checking

Every PR/feature should stay Valgrind-clean:

```bash
valgrind --leak-check=full --show-leak-kinds=all ./ramora-server conf/ramora.conf
```

run the server, hit it with `ramora-client`/`ramora-test`, shut it down (Ctrl+C), and check the summary.

## 7. Project layout recap

```
Ramora/
├── src/        # server implementation (event loop, hashmap, heap, buffers, command handlers, ...)
├── include/    # corresponding headers
├── client/     # interactive CLI client
├── conf/       # example server config
├── tests/      # benchmark/functional client (test.c) + Redis comparison report
```

Read [README.md](README.md) for the architecture overview, then [PROBLEM_STATEMENT.md](PROBLEM_STATEMENT.md) for what you're expected to build.
