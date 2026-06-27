# zzzclip

## PROBABLY SLIGHTLY BROKEN, USE AT YOUR OWN RISK

A clipboard manager that supports customizable mimetype selection (e.g. prefer copying images over or in addition to html) and clipboard persisting (replacing the clipboard after program exit).

To build, run `make build`. Executable at `build/zzz`.

To run the main daemon, run `build/zzz`.

To run a selector on the clipboard history, run `build/zzz get -- <program>`.
`get` expects a dmenu-style program which takes in newline-separated entries and returns the **index** of the selected entry.
For example, to use zzzclip with fuzzel, run `build/zzz get -- fuzzel --dmenu --index`

Configuration is located at `$XDG_CONFIG_HOME/zzzclip`.
I'll make proper documentation later, but for now, here's my config file:
```
# maximum number of entries in history
max-entries = 100

# maximum bytes for a clipboard item (per-mimetype, not per-copy)
max-item-bytes = 10000000

# maximum characters to show to the `zzz get` program
max-preview = 1000

# whether to replace the clipboard contents when it's cleared (like if a program exits)
replace-clipboard-on-clear = true

# determines which mimetypes zzzclip will request and store
# [] stores every mimetype matched inside
# () stores the first mimetype matched inside
# regexes behave differently based on whether its parent is [] or (): every match for [], first match for ()
mime-pref = [
    (image/png image/webp image/jpeg image/.*)
    (UTF8_STRING text/plain;charset=utf-8 TEXT text/plain text/.*)
]
```

## dependencies

- wayland client libraries (dev?)
- a compositor that supports the wlr-data-control protocol

## todo (mostly for me)

- better error handling
- deduplicating
- fix cli parsing and help
- fallback if entry too large (?)
- human readable sizes
- FUSE
- non-index-selector support

## warning

I don't know what I'm doing w.r.t. Wayland, use at your own risk!
