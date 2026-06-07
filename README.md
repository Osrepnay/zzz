# zzz

## REWRITE IN PROGRESS, LIKELY NONFUNCTIONAL

A clipboard manager that supports customizable mimetype selection (e.g. prefer copying images to html)

To build: `make build`

`build/zzz`: main daemon; listens for clipboard entries and stores them (numbered sequentially) in `$XDG_STATE_HOME/zzz_clip/<number>`. See `-h` for more information

`build/zzz_get`: inserts the clipboard entry given as an integer argument into the selection

Configuration is required at `$XDG_CONFIG_HOME/zzz_mimes`. Each line in `zzz_mimes` is either a PCRE2 regex or the string UNKNOWN. The mimetype that matches earliest will be selected. If a mimetype does not match any regexes, it will be treated as having "matched" on the UNKNOWN line. If no UNKNOWN is provided, it will be treated as being at the end of the file.

## dependencies

- wayland client libraries (dev?)
- libpcre2
- a compositor that supports the wlr-data-control protocol

## todo

- user provided mimetype selection script
- better error handling/cleanup
- support ext-data-control in addition to wlr-data-control
- deduplicating
- posix regex
- clean up init situation
- cli getting/deleting
- fix cli parsing and help
- fallback if entry too large
- human readable sizes
- getting whole sets instead of single mime
- multi-select support
- make deleting play nice with daemon

## warning

I don't know what I'm doing w.r.t. Wayland, use at your own risk!
