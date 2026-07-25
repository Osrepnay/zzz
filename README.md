<img src="zzzbanner.png" height=50vh>

# zzzclip

A Wayland clipboard manager that supports:

- Clipboard history
- Clipboard persisting: restore the clipboard after the program providing it exits; this is lazy, meaning zzzclip won't replace the selection until it needs to
- Clipboard-as-files: makes the current clipboard contents accessible via file
- Customizable MIME type selection: choose which MIME types to keep (this means image support for all features!)

POSIX C11, minimal dependencies. You know the drill.

## requirements

To run, you will need:

- A Wayland compositor that supports either the ext-data-control or wlr-data-control protocols.  
  You can find a compatibility table [here](https://absurdlysuspicious.github.io/wayland-protocols-table/);
  basically all major compositors ([except Mutter](https://gitlab.gnome.org/GNOME/mutter/-/work_items/3941)) support at least one. 

## installation

If you use Arch, zzzclip is [on the AUR](https://aur.archlinux.org/packages/zzzclip).
Otherwise, static binaries are available from the [releases section](https://github.com/Osrepnay/zzzclip/releases/tag/v0.1.0).
Alternatively, follow the instructions below and build it yourself.

## building

To build, you will need:

- Wayland client libraries (`wayland` on Arch, `libwayland-client` on Debian/Ubuntu)
- `wayland-scanner` (also `wayland` on Arch, `libwayland-bin` on Debian/Ubuntu)
- The normal stuff (`make`, a C compiler, etc.)

To build, run `make build`.  
To install, run `make install` (might require `sudo`).  
To run tests, run `make check`.
(Heads up, this will wipe your clipboard and fill your history with junk if you have a manager running!)

## usage

To start the main process, run `zzzclip daemon`.
More detailed instructions can be found by using `-h` on a specific subcommand
(`zzzclip daemon -h`, `zzzclip list -h`, `zzzclip get -h`, `zzzclip delete -h`).  

To integrate zzzclip with a selector program like fuzzel or rofi:
```
zzzclip list | fuzzel --dmenu --with-nth=2 --accept-nth=1 | xargs zzzclip get
zzzclip list | rofi -dmenu -display-columns 2 -column 1 | cut -f1 | xargs zzzclip get
zzzclip list | wofi --dmenu --pre-display-cmd "echo '%s' | cut -f2" -k /dev/null | cut -f1 | xargs zzzclip get 
```

The clipboard-as-files feature will, by default, put the files in `$XDG_RUNTIME_DIR/zzzclip`.
On my system, this is located at `/run/user/1000/zzzclip`.
To make this more accessible, you can override `ZZZCLIP_SYMLINK_PATH` to change the directory,
create a symlink (e.g. `ln -s $XDG_RUNTIME_DIR/zzzclip ~/clipboard`),
or create a bookmark in your file manager.
If your file manager is GTK-based, this should carry over to other GTK-based file managers and file pickers as well,
like in your browser's upload window.

## configuration

zzzclip respects the environment variables `ZZZCLIP_CONFIG_PATH`, `ZZZCLIP_STORE_PATH`, and `ZZZCLIP_SYMLINK_PATH`
to override the config file location, history directory, and clipboard-as-file directory, respectively.
By default, configuration is located at `$XDG_CONFIG_HOME/zzzclip`,
history at `$XDG_STATE_HOME/zzzclip`,
and clipboard-as-files at `$XDG_RUNTIME_DIR/zzzclip`.

Configuration is located at `$XDG_CONFIG_HOME/zzzclip`.
I'll make proper documentation later, but for now, here's the default config file:
```ini
# maximum number of entries in history
max-entries = 100

# maximum bytes for a clipboard item (per-mimetype, not per-copy)
max-item-bytes = 10000000

# maximum characters to show in clipboard previews (`zzzclip list`, some parts of `zzzclip get`)
max-preview = 1000

# whether to replace the clipboard contents when it's cleared, like if a program exits
replace-clipboard-on-clear = true

# determines which MIME types zzzclip will request and store
# [] stores every MIME type matched inside
# () stores the first set of MIME types matched inside
# regexes behave differently based on whether its parent is [] or (): every match for [], first match for ()
# all regexes are prefixed with ^ and suffixed with $ and are case-insensitive
# example: if the current clipboard offers image/webp, image/jpeg, and text/html, zzzclip will store
# image/webp and text/html with this config
mime-pref = [
    (image/png image/webp image/jpeg image/.*)
    (text/plain;charset=utf-8 UTF8_STRING text/plain TEXT text/.*)
]

# makes the clipboard accessible via file in $XDG_RUNTIME_DIR/zzzclip
clipboard-as-files = true
```

## todo

- better error handling
- automatic deduplicating
- fallback in mime preferences if entry too large
- icon support

## thanks

wl-clipboard, fuzzel, cliphist, clipvault, fuzzel for inspiration/guidance
