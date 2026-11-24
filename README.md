# perch

[![CI](https://github.com/martian56/perch/actions/workflows/ci.yml/badge.svg)](https://github.com/martian56/perch/actions/workflows/ci.yml)

Raw terminal control for Raven. Keypresses as they happen, mouse reports,
terminal size, and unbuffered writes, on Linux, macOS, and Windows.

Raven's standard library reads input a full line at a time, which rules out
anything interactive beyond ask-and-answer prompts. perch drops the terminal
into raw mode and decodes the byte stream into typed events, so arrow keys,
Ctrl combinations, and mouse clicks arrive one by one, the moment they
happen. It is the foundation the plumage TUI framework stands on, and it is
useful on its own for menus, key-driven tools, and progress displays.

## Install

```toml
[dependencies]
"github.com/martian56/perch" = "v0.4.0"
```

The package bundles a small C shim (termios and poll on POSIX, the console
API on Windows), so building a program that uses perch needs a C toolchain,
the same one Raven already needs for linking.

## Usage

```rust
from "github.com/martian56/perch" import Terminal
from "github.com/martian56/perch/keys" import InputEvent, Key

fun main() {
    let t = Terminal.new()
    if not t.raw_on() {
        print("stdin is not a terminal")
        return
    }
    t.write("press keys, q quits\r\n")
    var running = true
    while running {
        match t.read_event(100) {
            case KeyPress(k) {
                match k {
                    case Char(c) {
                        t.write("you typed ${c}\r\n")
                        if c == "q" {
                            running = false
                        }
                    }
                    case Up { t.write("up\r\n") }
                    case _ {}
                }
            }
            case Closed {
                running = false
            }
            case _ {}
        }
    }
    t.raw_off()
}
```

## The event model

`read_event(timeout_ms)` returns an `InputEvent`:

- `KeyPress(Key)`: one key. `Key` covers printable characters
  (`Char(str)`, UTF-8 aware), `Enter`, `Tab`, `Backspace`, `Esc`, the
  arrows, `Home`, `End`, `PageUp`, `PageDown`, `Insert`, `Delete`,
  function keys as `F(int)`, `Ctrl(str)` combinations, and `Mod(str)` for a
  named key with modifiers: `"shift+up"`, `"ctrl+right"`,
  `"ctrl+shift+left"`, `"ctrl+delete"`, `"alt+home"`, `"shift+tab"`, and
  Alt with a printable key, `"alt+x"` (ESC and the key in one read; an Esc
  pressed on its own settles on the read timeout). A sideways wheel arrives
  as `ScrollUp`/`ScrollDown` with the shift bit set.
- `Mouse(MouseEvent)`: kind (`Down`, `Up`, `Drag`, `Move`, `ScrollUp`,
  `ScrollDown`), button, a 1-based cell position, and `mods` (bits: 1
  shift, 2 alt, 4 ctrl) for shift-click and the like. Call `mouse_on()`
  first.
- `Paste(str)`: one whole bracketed paste, newlines normalized to
  `\n`, so pasted text arrives as a single event instead of a stream of
  keystrokes (and a pasted newline cannot act as Enter). Call
  `paste_on()` first.
- `Idle`: the timeout passed without input. This is what makes render
  loops tick.
- `Closed`: stdin ended.

A negative timeout blocks until something arrives.

In raw mode nothing is echoed and nothing is line buffered. Ctrl+C arrives
as `Ctrl("c")` like any other key: quitting is your program's decision.

Handling a paste looks like handling any other event:

```rust
t.paste_on()
match t.read_event(100) {
    case Paste(text) {
        // One event for the whole paste. text may span many lines; a
        // pasted newline is content here, never an Enter keypress.
        editor.insert(text)
    }
    case KeyPress(k) { handle_key(k) }
    case _ {}
}
```

## The Terminal type

- `raw_on() / raw_off()`: enter and leave raw mode. `raw_on` returns false
  when stdin is not a terminal, so piped runs can bail out cleanly.
- `alt_on() / alt_off()`: the alternate screen; scrollback survives.
- `mouse_on() / mouse_off()`: SGR mouse reporting.
- `paste_on() / paste_off()`: bracketed paste, delivered as `Paste` events.
- `size() -> Size`: terminal width and height in cells, 80x24 fallback.
- `write(s) / flush()`: straight to the terminal, no line buffering.
- `read_event(timeout_ms) -> InputEvent`: see above.
- `setup() / restore()`: the full-screen bundle. `setup` is raw mode plus
  alternate screen, hidden cursor, cleared page; `restore` undoes exactly
  what was switched on, in the right order.

`perch/ansi` has the escape builders (`move_to`, `clear_screen`,
`cursor_hide`, `sgr`, and friends) when you want to compose output by hand.

## Module map

Everything is importable on its own; `Terminal` is just the convenient
bundle on top.

- `perch` (lib.rv): `Terminal` and `Size`, the reading loop, mode tracking.
- `perch/keys`: the event and key types (`InputEvent`, `Key`, `MouseEvent`,
  `MouseKind`). Plain data, no I/O.
- `perch/input`: `Parser`, the incremental byte decoder behind
  `read_event`. Feed it bytes yourself to decode input from somewhere other
  than stdin, or to unit-test key handling; the whole decoder test suite
  works this way, no terminal involved.
- `perch/ansi`: escape sequence builders, pure string construction.
- `perch/sys`: the thin wrappers over the C shim (raw mode, size, byte
  reads, writes). You normally never import this directly.

## Demo

The package doubles as a runnable demo. In a real terminal:

```
rvpm run
```

It prints every event it decodes until q, Esc, or Ctrl+C.

## How it works

`c/shim.c` is the only native code: raw mode switching, size queries, a
single byte read with a timeout, and unbuffered writes. Everything above
that, the escape sequence parser included, is plain Raven. On Windows the
shim turns on virtual terminal processing, so the same ANSI sequences work
in Windows Terminal and modern conhost.

## Rough edges, by design

- Modifier prefixes on arrows and navigation keys (Ctrl+Up and similar) are
  accepted and stripped to the base key.
- Mouse reporting depends on the emulator. Windows Terminal, and every
  mainstream Linux and macOS emulator, speak the SGR protocol perch uses.
- Non-ASCII keyboard input on Windows depends on the console honoring the
  UTF-8 code page the shim sets.

## Development

```
rvpm build       # type-checks the library, builds the demo
rvpm test        # decoder and ANSI tests, no terminal needed
rvpm fmt
```

## License

MIT
