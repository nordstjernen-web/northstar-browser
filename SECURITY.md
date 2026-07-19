# Security policy

Northstar is a small independent web browser. Security fixes ship from
`main`; only the latest tagged release is supported.

## Reporting
Report security issues by e-mail to:  andreas.rosdal@hotmail.com

Please include the version (shown in the About Northstar dialog),
your OS, a minimal reproducer (URL or self-contained HTML), and your
assessment of the impact.

## Threat model

Northstar treats the Internet with the outmost suspicion. The attacker controls fetched
HTML, CSS, JavaScript, images, fonts, and media. The user, the
kernel, and the local filesystem outside the sandbox allow-list are
trusted.

**In scope**

- Memory-safety bugs in our C code: out-of-bounds reads/writes,
  use-after-free, integer overflow, format-string.
- Linux sandbox escapes — both layers: the Landlock filesystem
  allow-list and the seccomp-bpf syscall allow-list.
- Windows process-mitigation bypass — the policies set via
  `SetProcessMitigationPolicy` at startup (ASLR, strict handle
  checks, extension-point disable, image-load restrictions,
  dynamic-code prohibition, child-process block).
- Same-origin, cookie, and HTTP-cache partitioning bypass.
- HSTS, mixed-content, and CSP enforcement bypass.
- URL-bar spoofing (IDN homograph, scheme confusion, etc.).

**Out of scope**

- Bugs in third-party libraries (libcurl, GTK 4, GLib, lexbor, QuickJS,
  Wuffs, librsvg, …). Report upstream; we update when fixes ship.
- Features we deliberately don't implement: WebGL, WebGPU, WebRTC,
  MSE/EME/DRM, service workers, browser extensions, JIT, "AI" web APIs.
- CPU-level side channels (Spectre-class).
- Attacks that already require local code execution as the same user.

## Defenses

This minimalist edition runs **single-process**: the HTML/CSS/JS/layout
engine parses and renders untrusted content in the GTK shell process
itself (`ns_rproc_single_process_enable`, `src/gtk/appmain.c`). There is
no separate `northstar-renderer` executable and no per-tab renderer
process — every page shares one OS process and one address space. The
only helper the browser spawns is the unsandboxed-at-launch,
self-sandboxing `northstar-audio` decoder (see *Media*, below).

Because there is no privilege boundary between pages, the layered
defenses below are **hardening and containment for the whole process**,
not an inter-process sandbox around a compromised renderer: they shrink
what a memory-safety bug in the engine can reach (filesystem, syscalls,
executable memory, network protocols), but a bug in the engine is not
confined to a subordinate process the way it would be in a multi-process
browser. A true per-page/per-origin process sandbox is not part of this
edition; treat process isolation as **absent**, and the defenses here as
defence-in-depth around a single trusted-code / untrusted-data boundary.

### Compile-time hardening (`meson.build`)

PIE, full RELRO, non-executable stack, separate-code segments,
`-fstack-protector-strong`, `-fstack-clash-protection`,
`-fcf-protection=full` (Intel CET / AMD IBT), `_FORTIFY_SOURCE=3`
(`=2` fallback), `-Wformat=2 -Wformat-security`. No JIT is used or
linked — W^X holds for the whole process, so any RCE primitive has to
work without writable executable pages.

### Privilege drop (`src/security.c`)

- Linux/macOS: refuses to start as root — prints a diagnostic and exits
  before any page is loaded.
- Windows: when launched elevated it *drops* Administrator rights by
  relaunching itself with the desktop shell's medium-integrity token
  (`CreateProcessWithTokenW`) and exiting the elevated instance, so the
  browsing session runs unprivileged. It verifies the shell token is not
  itself elevated first, so a fully elevated session cannot loop. When
  de-elevation is impossible it falls back to a plain-language dialog
  offering to quit (default) or run as Administrator anyway.
- `NS_ALLOW_ROOT=1` overrides on every platform — keep the elevated
  process and skip the de-elevation and the prompt.
- Sets `PR_SET_NO_NEW_PRIVS` before installing the seccomp filter, so
  setuid binaries cannot be used to gain privileges after a compromise.

### Linux sandbox

Two syscall/filesystem confinement layers exist, both default-deny, both
installed from `src/security.c` before any HTML is parsed. **How much of
the pair is applied depends on the run mode**, and the interactive GUI
does not get both — read this carefully, because it is the single most
important caveat in this document:

| Run mode | Landlock | seccomp-bpf | `PR_SET_NO_NEW_PRIVS` |
|----------|:--------:|:-----------:|:---------------------:|
| **Interactive GUI** (the normal browser) | ✅ | ❌ **not applied** | ✅ |
| Headless / `--dump` / `--eval` / WPT tooling | ✅ | ✅ | ✅ |
| `northstar-audio` helper | ✅ | ✅ | ✅ |

The interactive GUI runs single-process (the engine is in the shell), and
its startup path (`proc_mode` in `src/gtk/appmain.c`) applies **Landlock
only** — it deliberately skips `ns_security_seccomp_init()` because the
shell must be able to `fork`/`execve` the `northstar-audio` helper, which
the no-`execve` seccomp filter would block. So on the normal browsing
path the untrusted-content engine is confined by the filesystem allow-list
and `PR_SET_NO_NEW_PRIVS`, **but not by the syscall allow-list**. The
seccomp filter is real and is exercised by the headless/tooling entry
point and by the audio helper (which re-imposes both layers on itself once
its device and network are up); closing this gap for the GUI — e.g. via a
pre-`execve` broker for the audio helper so the engine thread can take the
full filter — is tracked as future work and called out again under *Known
gaps*.

- **Landlock (filesystem) — applied in every mode.** Read-only access to
  system libraries (`/usr`, `/lib`, `/lib64`), `/etc`, the CA bundle, font
  caches, `/dev/urandom`, and the X11 / Wayland sockets. Read+write access
  to the per-user XDG config, data, and cache directories under
  `~/.config/northstar`, `~/.local/share/northstar`,
  `~/.cache/northstar`. The rest of `$HOME` — `~/.ssh`, `~/.aws`,
  `~/.netrc`, other browsers' state, shell history — is **not**
  reachable. No directory the process can write to is also executable, so
  a bug cannot drop a payload and then map it executable from a writable
  path. `PR_SET_NO_NEW_PRIVS` is set here too, so a setuid binary cannot
  be used to regain privileges after a compromise.
- **seccomp-bpf (syscalls) — applied to headless/tooling and the audio
  helper.** Default-deny allow-list: the filter is built with
  `SCMP_ACT_ERRNO(EPERM)` as the default action and then permits only the
  ~266 syscalls the browser actually needs (`ns_seccomp_allowed_names[]`
  in `src/security.c`); every other syscall returns `EPERM`. `execve` /
  `execveat` are not on the list, so a confined process cannot pivot to
  another interpreter or binary even if Landlock would have allowed
  reading it. `ptrace`, `bpf`, `keyctl`, `mount`, `unshare`,
  `userfaultfd`, the `io_uring_*` family, `perf_event_open`, `kexec_load`,
  and the module syscalls are likewise absent from the allow-list. TSYNC
  propagates the filter to every thread.
- **Media / audio.** Northstar decodes audio **in-tree** in a separate
  `northstar-audio` helper process (`src/audio/main.c`), not via an
  external player. When a page plays an `<audio>` element the engine emits
  `open`/`play`/`pause`/`seek`/`stop`/`volume` commands that ride the
  render-response `X-Audio` side-channel to the shell (`src/gtk/procview.c`),
  which spawns the helper from the browser's own install directory
  (`ns_proc_audio_helper_path`) with `g_subprocess_launcher_spawn` and
  drives it over stdin/stdout — the media URL is fetched and decoded by the
  helper, never handed to a shell or an arbitrary binary. The helper
  decodes MP3 (vendored minimp3), MP2 (vendored pl_mpeg) and, when
  `opusfile`/`vorbisfile` are present, Ogg Opus/Vorbis, and outputs through
  SDL2; on Linux it re-imposes the same Landlock + seccomp profile on
  itself before touching codec bytes. `<video>` lays out but is **not**
  decoded in this edition, so there is no video codec attack surface.

The sandbox can be disabled for debugging with `NS_NO_SANDBOX=1` (Landlock)
/ `NS_NO_SECCOMP=1` (seccomp). Don't use those in normal operation.

### macOS

macOS is **not a supported target** in this minimalist edition — the build
targets Linux (primary) and Windows only. A macOS Seatbelt
(`sandbox_init(3)`) path still exists in the `__APPLE__` arm of
`ns_security_sandbox_init` (`src/security.c`), but it is not compiled or
exercised by either shipped build, so it is not part of this edition's
security posture. If macOS support is ever restored, that code (and this
section) must be reviewed against the single-process model described above
before being relied on.

### Windows process mitigations

Windows has no direct Landlock or seccomp-bpf equivalent that a
user-space GTK process can apply to itself. Instead the browser
hardens itself at startup via `SetProcessMitigationPolicy`, called
from `ns_security_win32_mitigations_init` in `src/security.c`
**before** any DLL we don't statically link is touched. Six
policies, all best-effort (an unsupported policy on an older
Windows just returns `FALSE` and is skipped). This edition is
single-process, so the one browser process applies **the first
five**; it skips the sixth (ChildProcess block) because it must
spawn the `northstar-audio` helper. The full six-policy set is still
applied by any process that spawns nothing — e.g. the headless
tooling entry point:

- **ASLR** (`ProcessASLRPolicy`, flags `0x0F`) — force relocate
  images, force bottom-up randomization, high-entropy 64-bit
  layout, disallow stripped images. Belt-and-braces on top of the
  PE header's `IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE`.
- **StrictHandleCheck** (`ProcessStrictHandleCheckPolicy`, flags
  `0x03`) — raise an exception on invalid handle use and lock the
  setting permanently. Catches double-close, UAF-of-handle bugs.
- **DisableDynamicCode** (`ProcessDynamicCodePolicy`, flags
  `0x01`) — refuse `VirtualAlloc`/`VirtualProtect` with
  `PAGE_EXECUTE_*`. Pairs with the no-JIT QuickJS guarantee: the
  process has no legitimate need for writable-executable memory,
  so any RCE that depends on allocating one is denied at the
  kernel boundary.
- **DisableExtensionPoints**
  (`ProcessExtensionPointDisablePolicy`, flags `0x01`) — block
  `AppInit_DLLs`, WinSock Layered Service Providers, Image File
  Execution Options debuggers, and a few other legacy injection
  vectors that load DLLs into every process on the box.
- **ImageLoad restrictions** (`ProcessImageLoadPolicy`, flags
  `0x07`) — `NoRemoteImages` (no DLL loads from UNC/mapped
  network drives), `NoLowMandatoryLabelImages` (no DLL loads from
  Low-IL filesystem locations), `PreferSystem32Images` (resolve
  ambiguous DLL names against system32 first). Mitigates DLL
  planting and search-order hijacks.
- **ChildProcess block** (`ProcessChildProcessPolicy`, flags
  `0x01`) — no `CreateProcess` from this process, so a compromised
  process cannot directly pivot to `cmd.exe` / `powershell` / another
  binary. The single-process GUI passes `allow_child_processes` to
  **skip** this one policy (keeping the other five) because it must
  launch the in-tree `northstar-audio.exe` decoder for `<audio>`
  playback; audio bytes are fetched and decoded inside that helper, not
  by an external player. A process that never spawns a child (the
  headless tooling path) takes this policy too.

There is no per-path filesystem sandbox; Windows AppContainer
would provide one but requires a manifest and code-signing
integration we don't have yet. The closest equivalents — Low
Integrity Level drop and AppContainer — are tracked as future
work.

Plus `ns_security_refuse_root`: elevation is detected with
`CheckTokenMembership` against the Built-in Administrators SID.
Unlike the Linux path it does not merely refuse — it relaunches the
browser under the desktop shell's token (`CreateProcessWithTokenW`)
and exits the elevated instance, so the session ends up unprivileged.
A `MessageBox` with a run-anyway option and a `stderr` diagnostic are
the fallback when de-elevation cannot be done; `NS_ALLOW_ROOT=1` skips
both.

The whole mitigation suite can be disabled for debugging with
`NS_NO_WIN32_MITIGATIONS=1`. Don't use that in normal operation.

### Network

libcurl drives every fetch with TLS verification enabled
(`CURLOPT_SSL_VERIFYPEER=1`, `CURLOPT_SSL_VERIFYHOST=2`), `http,https`
as the only allowed protocols, redirects clamped to HTTPS once the
initial scheme is HTTPS, max ten redirects, an explicit response-size
cap, and `CURLOPT_NOSIGNAL`. HSTS state is loaded and persisted via
`CURLOPT_HSTS`; Alt-Svc is honoured. Mixed-content sub-resources
(http inside an https document) are blocked.

### Origin isolation

- Cookies and the HTTP cache are partitioned per top-level **site**
  (scheme + registrable domain + port), where the registrable domain
  comes from the Public Suffix List via libpsl. All subdomains within
  the same registrable domain share one cookie jar and one cache
  partition; everything else is isolated. Third-party cookies are
  blocked by default.
- CSP (`default-src`, `script-src`, `style-src`, `img-src`,
  `media-src`, `connect-src`, `font-src`, `frame-src`,
  `frame-ancestors`) is parsed and enforced for both inline and
  external resources, including nonce and hash matches. Host source
  expressions match scheme, host (with `*.` wildcard), port (defaulting
  to the scheme's default), and path (left-anchored if the source ends
  in `/`, exact otherwise). `*` follows CSP3 semantics — it matches
  network schemes only, never `data:`, `blob:`, `filesystem:`, or
  `javascript:`.
- Subresource Integrity (`integrity="sha256-…"` / `sha384-` / `sha512-`)
  is verified against the response body before scripts or stylesheets
  are applied.
- IDN labels are accepted for display only under a Unicode TR-39
  "Highly Restricted"–style profile: each label must be either pure
  ASCII, a single non-Latin script, or one of the three standard CJK
  combinations (Japanese / Traditional Chinese / Korean). Anything
  else is shown as punycode in the URL bar, defeating most
  Latin-look-alike homograph attacks.

### On-disk state

Config, cookies, cache, HSTS, Alt-Svc, and bookmarks live under the
XDG dirs above with owner-only permissions (`0700` directories, `0600`
files on Unix; ACL-tightened on Windows). The HTTP cache is keyed on
`SHA-256(URL || partition)`, so cache filenames never embed
attacker-controlled bytes and no path-traversal is possible.

### Parsers

- HTML is parsed exclusively by [lexbor](https://github.com/lexbor/lexbor);
  there is no hand-rolled HTML tokenizer.
- URL parsing routes through lexbor's WHATWG URL module.
- PNG, GIF, BMP, and JPEG bytes are decoded by
  [Wuffs](https://github.com/google/wuffs) (memory-safe,
  transpiled-to-C). GdkPixbuf and librsvg handle the remaining formats
  inside the same sandbox.
- Charset sniffing is delegated to uchardet, not hand-rolled.
- The engine's own parsers bound attacker-controlled nesting and sizes.
  The recursive CSS parsers — selectors, `@supports`, `@media` queries,
  `var()` fallbacks, and `color-mix()` — all carry depth caps, the
  background-layer list is torn down iteratively, and layout's box-tree
  walkers stop at a fixed depth, so a pathologically nested stylesheet or
  DOM cannot exhaust the stack. Sizes from untrusted sources — decoded
  image dimensions and the render layer's `X-W`/`X-H`/`X-Stride` reply
  headers — are clamped before any `width × height`/`stride × height`
  multiplication, so a crafted dimension cannot integer-overflow a
  bounds check or allocation. `filter: blur()` likewise clamps its
  radius before building the convolution window.

### JavaScript

JavaScript runs in [QuickJS](https://github.com/quickjs-ng/quickjs), an
interpreter — no JIT, no machine-code generation. The DOM/JS bridge
invalidates opaque pointers on node free and re-validates on every
call, so DOM mutation cannot dangle a JS-held handle.

All pages run in **one OS process** (single-process edition) — there is
no per-page process or address-space isolation, so this is a
JavaScript-state boundary, not a memory boundary. Each page/tab gets its
own QuickJS runtime and context, and navigating across origins (e.g. from
`news.example.com` to `evil.com`) tears down the runtime and starts a
fresh one, so attacker-controlled globals (`window.foo = secret;`),
prototype pollution, leftover module state, and any other in-memory
JS residue from the previous origin cannot reach the new origin's
scripts. Same-origin navigation reuses the existing runtime so
sessionStorage and history work as expected. Because everything shares
one process, a memory-safety bug in the engine is **not** contained
between pages the way it would be with per-tab processes; the runtime
teardown defends against JS-level state leakage, not against native
memory corruption.

Iframes are rendered. An `<iframe src>` is fetched through the same
hardened network pipeline as any other resource (TLS verification,
`http`/`https` only, mixed-content blocking, redirect clamp, response-size
cap, CSP `frame-src`); `srcdoc` is parsed inline. The content document is
parsed by lexbor and laid out in place.

A loaded frame gets a JavaScript realm, but **within the parent tab's
single QuickJS runtime** — there is no separate runtime, context, or OS
process per frame. The realm is a synthetic scope: the frame sees its own
`window`, `document`, `location`, and `history`, and `top`/`parent`/`self`/
`frames` are redirected to the frame itself rather than exposing the parent's
real global. This is a best-effort JavaScript-level boundary for ordinary
content, **not** a hard security boundary the way the cross-origin
top-level navigation runtime teardown (above) is: a memory-safety bug
or a Proxy escape in one frame is not contained from the rest of the
document's origin. Treat frame isolation as defence-in-depth, not as an
origin sandbox.

The `sandbox` attribute is parsed and enforced, with nested frames
inheriting the intersection of their ancestors' sandboxes:

- No `allow-scripts` (or no `sandbox` allowing it) blocks the frame's
  scripts from running at all.
- No `allow-same-origin` makes the frame opaque-origin: `localStorage`
  and `sessionStorage` throw `SecurityError`, and `document.cookie` reads
  empty and ignores writes.
- No `allow-forms` blocks form submission; no `allow-modals` neutralises
  `alert`/`confirm`/`prompt`/`print`; no `allow-popups` makes
  `window.open` return `null`.

A plain `<iframe>` with no `sandbox` attribute runs its scripts. Cross-
document `postMessage` between a frame and its parent is currently limited
(see Known gaps).

### Cookies and the `document.cookie` surface

Network cookies live in libcurl's per-site cookie jar on disk. They
are parsed, scoped, and re-sent by libcurl, honouring `HttpOnly`,
`Secure`, `SameSite`, `Path`, `Domain`, and expiry. At navigation the
non-`HttpOnly` cookies for the document's origin are read back out of
the jar (`ns_net_cookies_for_js`) and seeded into `document.cookie`,
and the `document.cookie` setter writes back into that same per-site
jar (`ns_net_cookie_store_from_js`) — so a cookie set from JS is sent
on the next request, and a cookie set over the network is visible to a
later `document.cookie` read. `HttpOnly` cookies are written by libcurl
with a `#HttpOnly_` line prefix that the JS read path skips, so they
stay invisible to script.

The `document.cookie` setter:

- Caps input length at 4 KiB.
- Requires a non-empty `name`.
- Maintains a per-tab in-memory mirror for synchronous read-back, then
  persists to the network jar.
- Parses attributes after the first `;`. `Max-Age` (seconds) and
  `Expires` (HTTP-date, via `curl_getdate`) set the jar expiry;
  `Max-Age<=0` or a past `Expires` deletes the named cookie. `Secure`
  is rejected outright from non-HTTPS origins. `Domain` is range-checked
  against the document host before it widens scope; absent, the cookie
  is stored host-only. `Path` defaults to `/`.

## Known gaps

- **No process isolation between pages — single-process edition.** The
  engine parses and renders all untrusted content in the one browser
  process; there is no per-tab, per-page, or per-origin renderer process.
  A memory-safety bug in the engine is therefore not contained to a
  subordinate process — the origin/cookie/CSP/cache boundaries are
  enforced in-process by the engine's own logic, and the sandbox
  (Landlock, and seccomp where applied) limits what the whole process can
  reach, but neither is a substitute for the address-space isolation a
  multi-process browser gives you. A per-page process sandbox is the
  largest single hardening this edition does not have.
- **seccomp is not applied to the interactive GUI.** As detailed under
  *Linux sandbox*, the normal browsing process runs under Landlock and
  `PR_SET_NO_NEW_PRIVS` but **without** the seccomp syscall allow-list,
  because it must `execve` the `northstar-audio` helper and the filter
  blocks `execve`. A memory-safety bug in the engine can therefore issue
  syscalls the headless/helper processes would be denied. Applying the
  filter to the GUI (e.g. via a pre-`execve` audio broker, or an
  `execve`-permitting variant of the filter) is tracked as future work.
- **Iframe isolation is JS-level, not a runtime or process boundary.**
  A loaded frame shares the parent page's QuickJS runtime and global
  prototypes; its separate `window`/`document`/`location` and redirected
  `top`/`parent` are a synthetic scope, not a true cross-origin sandbox.
  Cross-document `postMessage` is correspondingly limited. A
  per-origin/per-frame runtime would close this and is tracked as future
  work; until then, do not rely on a cross-origin frame being contained
  from the embedding origin.
- **No per-path filesystem sandbox on Windows.** The mitigation
  suite restricts the *process* (no remote DLL loads, no dynamic
  code, etc.) but does not allow-list the files the process can
  read or write the way Landlock does on
  Linux. AppContainer or Low-Integrity-Level drop would close
  this; both require additional integration work (manifest /
  capability declarations / re-routed config paths) and are
  tracked as future work.
- **`document.cookie` writes to the jar without file locking.** A
  JS cookie write and a concurrent libcurl jar flush from an
  in-flight transfer are not serialised against each other, so a
  write can occasionally be lost to a racing flush. A shared,
  locked cookie store is the long-term fix; in practice script
  cookie writes happen between transfers, so the window is small.
- **`HttpOnly` name collisions from JS are not rejected.** Setting
  `document.cookie` with the same name as an existing `HttpOnly`
  cookie adds a second entry rather than being refused; the
  `HttpOnly` line is preserved untouched.
