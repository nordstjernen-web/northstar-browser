# Native Win32 shell

Northstar uses a native Win32 desktop shell on Windows. It presents the same
single-window, single-page browser as the GTK 4 shell on Linux and macOS, but
does not load GTK, GDK, or a GTK icon theme. The browser engine, configuration,
networking, storage, renderer protocol, audio mixer, and headless driver are
shared between both shells.

## Building and running

Use an MSYS2 MINGW64 shell and install the packages listed in
`.github/workflows/windows.yml`. The normal build selects the Win32 shell
automatically:

```sh
export CC="ccache clang"
export CC_LD=lld
meson setup builddir --werror -Db_lto=true
meson compile -C builddir
./builddir/src/win32/northstar.exe
```

`-Dwin32=disabled` disables the native shell. `-Dgtk=enabled` additionally
builds the GTK shell as `builddir/src/gtk/northstar-gtk.exe`, which is useful
for side-by-side shell comparisons. The native executable remains
`builddir/src/win32/northstar.exe` and is the binary packaged by
`scripts/pack-windows.sh`.

Headless operation uses the same executable and bypasses the desktop shell:

```sh
./builddir/src/win32/northstar.exe --headless --dump=text about:start
```

## Feature parity

The Win32 shell exposes the complete desktop feature set of the GTK shell:

| Area | Win32 behavior |
|------|----------------|
| Window chrome | Back, Forward, Reload, Home, address entry, security state, loading state, bookmarks, application menu, page status, and site shortcut. |
| Navigation | Address/search activation, link navigation, redirects, session history, reload/recovery, browser mouse buttons, and drag-and-drop file delivery. |
| Page interaction | Engine framebuffer presentation, resize and scroll synchronization, mouse hover/click/release, middle-click links, nested scrolling, CSS scrollbars, keyboard input, clipboard, and text selection. |
| Find and zoom | Find bar with previous/next match and match count; zoom in, out, and reset. |
| Context actions | Back, Forward, Reload, copy page/link/selection, open link, select all, save PDF, and save PNG. |
| Permissions and media | Camera/microphone consent bar, permission response delivery, and the shared asynchronous audio side channel. |
| Browser data | Bookmarks menu, add/remove bookmark actions, downloads window, recent downloads, open file, and open containing folder. |
| Developer tools | Console evaluation, Network, Performance, Layout, and Elements views, inspection mode, refresh, and clear. |
| Diagnostics | Task Manager with CPU, memory, thread, and renderer state; end-task action; and thread dump viewer. |
| Application behavior | Settings and About pages, private mode, session recovery, fullscreen, shortcuts, translated UI strings, Windows theme/media preference propagation, and DPI-aware sizing. |

The shell deliberately preserves the edition's one-page design. It does not
add tabs, renderer processes, or any platform-specific web API.

## Implementation

`src/appmain.c` owns command-line, watchdog, runtime-data, headless, and common
startup behavior. At build time it dispatches graphical startup to either the
GTK or Win32 shell.

`src/win32/winwindow.c` owns the top-level window, native controls and menus,
accelerators, bookmarks, downloads, Task Manager, fullscreen, session
recovery, and application lifecycle. Its message loop also drains the GLib
main context so libcurl and the shared asynchronous services continue to make
progress without a GTK main loop.

`src/win32/winview.c` owns the page control. Renderer work runs on its worker
queue through the existing in-process `rproc_http` protocol. Results return to
the UI thread through a private window message. The control paints renderer
pixels with GDI and translates Win32 pointer, keyboard, scrolling, clipboard,
drop, permission, audio, find, export, and developer-tool actions into the
same renderer requests used by the GTK view.

The custom view never calls engine APIs from the UI thread while its worker is
active. Window destruction marks the view closed, resolves any outstanding
permission prompt, sends the worker a quit request, joins it, and only then
releases renderer and audio state.

## Packaging and verification

The Windows bundle contains the small native launcher at its root and the
native shell plus its transitively imported MinGW DLLs under `app/`. Because
the shell is native, GTK settings schemas, GDK-Pixbuf loaders, and GTK icon
themes are not bundled.

Before shipping a Windows change, run:

```sh
./scripts/dev.sh smoke
./builddir/src/win32/northstar.exe --print-config
./builddir/src/win32/northstar.exe --headless --dump=text about:start
```

Then launch the browser normally and exercise navigation, find, Developer
Tools, Downloads, Task Manager, context actions, and window resizing. The
Windows CI workflow repeats the warnings-as-errors LTO build and headless
checks, then assembles and validates the redistributable bundle.
