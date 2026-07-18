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
"github.com/martian56/perch" = "v0.1.0"
```

The package bundles a small C shim (termios and poll on POSIX, the console
API on Windows), so building a program that uses perch needs a C toolchain,
the same one Raven already needs for linking.

## Usage

```rust
import "github.com/martian56/perch" { Terminal }
import "github.com/martian56/perch/keys" { InputEvent, Key }

fun main() {
    let t = Terminal.new()
    if !t.raw_on() {
        print("stdin is not a terminal")
        return
    }
    t.write("press keys, q quits\r\n")
    let running = true
    while running {
        match t.read_event(100) {
            KeyPress(k) -> match k {
                Char(c) -> {
                    t.write("you typed ${c}\r\n")
                    if c == "q" {
                        running = false
                    }
                },
                Up -> t.write("up\r\n"),
                _ -> {},
            },
            Closed -> {
                running = false
            },
            _ -> {},
        }
    }
    t.raw_off()
}
```

## The event model

`read_event(timeout_ms)` returns an `InputEvent`:

- `KeyPress(Key)`: one key. `Key` covers printable characters
  (`Char(String)`, UTF-8 aware), `Enter`, `Tab`, `Backspace`, `Esc`, the
  arrows, `Home`, `End`, `PageUp`, `PageDown`, `Insert`, `Delete`,
  function keys as `F(Int)`, and `Ctrl(String)` combinations.
- `Mouse(MouseEvent)`: kind (`Down`, `Up`, `Drag`, `Move`, `ScrollUp`,
  `ScrollDown`), button, and a 1-based cell position. Call `mouse_on()`
  first.
- `Paste(String)`: one whole bracketed paste, newlines normalized to
  `\n`, so pasted text arrives as a single event instead of a stream of
  keystrokes (and a pasted newline cannot act as Enter). Call
  `paste_on()` first.
- `Idle`: the timeout passed without input. This is what makes render
  loops tick.
- `Closed`: stdin ended.

A negative timeout blocks until something arrives.

In raw mode nothing is echoed and nothing is line buffered. Ctrl+C arrives
as `Ctrl("c")` like any other key: quitting is your program's decision.

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

- Alt+key currently decodes as the plain key; Shift+Tab decodes as `Tab`.
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
