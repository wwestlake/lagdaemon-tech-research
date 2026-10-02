# FrustMUD Required Pod Work Order

## Immediate Goal

Enable a single local player to connect to an authoritative FrustMUD server over TCP at `127.0.0.1:4000`, send newline-terminated commands, and receive lines ending with `.`.

## Finding

The live registry lists useful pod names, but direct source inspection shows that several implementations are placeholders. Registry presence and exported symbol names must not be treated as proof that a pod works.

## P0: Complete `frust_net`

Current source:

```text
D:\FrustLang\projects\frust_net
```

Current published version: `0.1.0`.

Minimum v0.2 API:

```text
Ipv4Addr
ipv4_new(a, b, c, d)
ipv4_loopback()

TcpListener
TcpStream
tcp_listen(address, port, backlog)
tcp_accept(listener)
tcp_connect(address, port)
tcp_read(stream, buffer, capacity)
tcp_write(stream, buffer, length)
tcp_close_stream(stream)
tcp_close_listener(listener)
tcp_last_error()
```

Behavioral requirements:

- Real OS socket calls, not placeholder handles.
- Windows support first; preserve a platform-neutral Frust API.
- Blocking calls are acceptable for the one-client prototype.
- Reads and writes report byte counts or negative status values.
- Partial writes are possible and must be representable.
- EOF/disconnect is distinguishable from a temporary or hard error.
- Listener restart should support address reuse where the OS permits it.
- Handles have explicit ownership and close semantics.
- A smoke test must bind `127.0.0.1` on an ephemeral port, accept one connection, exchange bytes in both directions, and close cleanly.

Do not split client and server into separate pods yet. They share types, error rules, and socket lifetime semantics; one complete `frust_net` is easier to teach and use correctly.

## P0: Text and Command Parsing

The MUD command interpreter needs operations that are not currently exposed as a coherent safe API.

Candidate location:

- extend `core` if these are considered fundamental String operations; or
- publish `frust_text` if ownership/allocation and parsing deserve a separate pod.

Minimum API:

```text
trim_ascii(input)
to_lower_ascii(input)
equals_ignore_ascii_case(a, b)
starts_with(input, prefix)
split_first_whitespace(input)
tokenize_whitespace(input)
substring(input, start, length)
```

The exact signatures must match Frust's real String ownership model. Do not invent a method syntax that the language/compiler does not support. A fixed-buffer parser is acceptable for v0.1 if owned dynamic strings are not ready.

Acceptance cases:

```text
"  LOOK  "       -> verb "look", no argument
"go north"       -> verb "go", argument "north"
"take brass key" -> verb "take", argument "brass key"
empty/whitespace  -> no command
```

## P1: Collections

Current source:

```text
D:\FrustLang\projects\frust_collections
```

The current `HashMap`, `RingBuffer`, and `SpscQueue` functions are placeholders. They should not be selected merely because they appear in registry exports.

For FrustMUD v0.1, use intrinsic `Vector<T>` and small linear searches. Complete collections later for:

- command alias lookup;
- room exit maps;
- object lookup by normalized name;
- player/session maps;
- bounded network queues when multi-client support arrives.

A real `HashMap<String, T>` or equivalent is the first useful collection target for the MUD.

## Project Dependency Correction

Current project manifest:

```text
D:\000 FrustMUD\frate.json
```

It currently pins `core` `1.0.0` and contains duplicate `exports` and `dependencies` keys. Normalize the JSON and evaluate updating to:

```json
{
  "name": "frust_mud",
  "version": "0.1.0",
  "type": "bin",
  "description": "Command-line MUD prototype for Frust.",
  "exports": [],
  "dependencies": [
    { "name": "core", "version": "1.0.3" },
    { "name": "frust_net", "version": "<completed-version>" }
  ]
}
```

Add the text pod only if parsing is not delivered through the matching `core` release.

## Not Required for v0.1

Do not block the first MUD milestone on these:

- `frust_http`
- `frust_websocket`
- `frust_json`
- concurrent queues
- multi-client scheduling
- persistence

The current HTTP and JSON implementations are also stubs. The v0.1 protocol is plain TCP text and does not need them.

## Delivery Order

1. Finalize socket/error/ownership signatures.
2. Implement and smoke-test real TCP client/server operations.
3. Publish the completed `frust_net` version and verify registry exports.
4. Implement the minimal text parser surface with tests.
5. Normalize `D:\000 FrustMUD\frate.json` and pin completed versions.
6. Let the embedded agent implement the command interpreter and echo server using only verified APIs.
