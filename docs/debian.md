# Northstar in Debian

Status and playbook for getting Northstar into Debian sid (unstable).

## Status (2026-07-23)

- **ITP filed: [#1142659](https://bugs.debian.org/1142659)**
  ("ITP: northstar -- new web browser with an original engine written
  in C"), submitted to `wnpp`, CC'd to debian-devel. Owner:
  Andreas Røsdal.
- **Packaging is complete and validated** in `debian/`:
  - Builds cleanly in Debian **sid** and **forky** chroots
    (podman containers, `dpkg-buildpackage`).
  - **Lintian-clean: zero warnings** as of the `1.0.4-1` changelog.
  - `debian/rules` installs with `meson install --skip-subprojects` so
    the vendored subprojects' artifacts (`qjs`, `qjsc`, headers,
    static libs) stay out of the package — `/usr/bin/qjs` would
    file-conflict with Debian's `quickjs-ng`.
  - `debian/copyright` (DEP-5) covers the vendored components
    (lexbor Apache-2.0, quickjs-ng Expat, wuffs Apache-2.0,
    pl_mpeg Expat, minimp3 CC0-1.0, WAMR Apache-2.0-with-LLVM-exception).
  - `northstar(1)` manpage installed via `debian/manpages`.
  - `debian/changelog` targets `unstable` and closes the ITP.
- **System lexbor supported**: `meson.build` tries pkg-config
  `lexbor >= 3.0.0` before falling back to the wrap-git cmake
  subproject. Debian ships `liblexbor-dev` 3.0.0-1 — the exact version
  Northstar pins — and the container build against it links
  `liblexbor.so.3`. Note this landed after the 1.0.4 tag, so the
  1.0.4-1 Debian upload still builds the vendored copy; the next
  upstream release can switch by adding `liblexbor-dev` to
  Build-Depends.
- **quickjs-ng stays vendored**: Debian's `quickjs-ng` source package
  ships no shared library or `-dev` package, so Northstar statically
  links its pinned copy (documented in `debian/README.source`).

## Orig tarball

The GitHub tag tarball alone does not build offline — the lexbor and
quickjs-ng subprojects are wrap-git and must be embedded. Recipe (also
in `debian/README.source`):

```sh
meson subprojects download lexbor quickjs-ng
tar --exclude-vcs --exclude='subprojects/*/.git' \
    --exclude='builddir' --exclude='debian' \
    -czf ../northstar_<version>.orig.tar.gz .
```

Consider attaching such a complete tarball to future GitHub releases
so `uscan`/`debian/watch` can fetch a buildable orig directly.

## Validation loop

From a checkout with the orig tarball prepared, in a sid (or forky)
container:

```sh
apt-get build-dep .            # or install Build-Depends by hand
dpkg-buildpackage -us -uc
lintian -i ../northstar_*.changes
```

Heads-up: sid was mid perl-5.42 transition on 2026-07-23, which
temporarily made `lintian`/`devscripts`/`libenchant-2-dev`
uninstallable there. Transient archive state — retry, or run lintian
from forky.

## Next steps

1. Subscribe to the ITP bug: empty mail to
   `1142659-subscribe@bugs.debian.org`. Answer any replies on the bug
   or debian-devel.
2. One-time accounts (prerequisites for sponsorship):
   - GPG key, if not already present.
   - [mentors.debian.net](https://mentors.debian.net) account with the
     GPG key attached.
   - [Salsa](https://signup.salsa.debian.org) account; push the
     packaging repo there and point `Vcs-Git`/`Vcs-Browser` in
     `debian/control` at it.
3. Source-only signed upload to mentors:
   `dpkg-buildpackage -S` then
   `dput mentors northstar_1.0.4-1_source.changes`.
4. File an RFS bug against `sponsorship-requests`
   (mentors.debian.net generates the template) and mail
   `debian-mentors@lists.debian.org`.
5. Sponsor reviews and uploads → package enters the **NEW queue** →
   ftpmaster license/copyright review (weeks, watch
   [ftp-master.debian.org/new.html](https://ftp-master.debian.org/new.html))
   → ACCEPT into sid → auto-migration to testing after ~2–10 days
   without RC bugs.
6. Ongoing: maintain via the BTS, upload new upstream releases through
   the sponsor, and after a few uploads consider applying for Debian
   Maintainer (DM) status to upload independently.
