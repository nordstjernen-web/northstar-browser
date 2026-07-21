# Northstar documentation

Developer documentation for the **Northstar** web browser (open-source GPL
edition) — a minimalist browser written from scratch in C with GTK 4 and
libcurl, carrying no upstream browser engine.

## Contents

- **[architecture.md](architecture.md)** — the process model (single-process
  in this edition), the page-load pipeline, and a map of which source file
  owns which job.
- **[building.md](building.md)** — dependencies per distro, the meson/ninja
  build, meson options, headless mode, and the render-test fixtures.

## Related documents at the repository root

- **[../README.md](../README.md)** — product overview and feature list.
- **[../SECURITY.md](../SECURITY.md)** — threat model, sandbox posture
  (per run mode), origin isolation, and the reporting contact.
- **[../CLAUDE.md](../CLAUDE.md)** — contributor operating guide, project
  scope (what this minimalist edition deliberately omits), and the
  comments/code-style policy.
- **[../THIRD-PARTY-LICENSES.md](../THIRD-PARTY-LICENSES.md)** — notices for
  the bundled and linked third-party libraries.

## Scope of this edition

This is the minimalist GPL desktop edition. It is single-process (the
engine runs in the shell process), targets Linux (primary), macOS and Windows, and
deliberately omits tabs-as-processes, WebGL/WebGPU, inline video decoding,
the PDF viewer, local-AI features, and the embeddable-library API. See
[../CLAUDE.md](../CLAUDE.md) for the authoritative scope statement.
