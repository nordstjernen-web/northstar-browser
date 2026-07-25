---
name: audit-browser-security-boundaries
description: Audit Northstar changes that touch untrusted web input, native memory, URL or origin handling, networking, cookies, cache, storage, CSP, extensions, service workers, decoders, sandboxing, or platform mitigations. Use for security reviews, threat analysis, or fixes where the single-process browser boundary could regress.
---

# Audit Browser Security Boundaries

Read `AGENTS.md`, `SECURITY.md`, the changed code, and its callers. Treat fetched HTML, CSS, JavaScript, images, fonts, and audio as attacker-controlled. Preserve unrelated working-tree changes.

## Trace the boundary

Follow each changed value from attacker input to its final use. Check:

- Length conversion, multiplication, allocation caps, integer overflow, recursion depth, and parser cleanup
- Ownership, callback lifetime, cancellation, worker-thread synchronization, and DOM/QuickJS pointer validation
- WHATWG URL parsing, canonicalization, same-origin checks, top-site partitioning, redirects, mixed content, CORS, CSP, and SRI
- Private-mode persistence, path canonicalization, file permissions, and extension or service-worker scope
- Linux Landlock/seccomp, macOS Seatbelt, Windows mitigations, and the absence of renderer-process isolation

Do not assume a JS-runtime boundary contains native memory corruption. Do not weaken TLS verification, resource limits, sandbox allow-lists, or origin checks to make a page work.

## Report or fix

For a review request, report only actionable findings. For each finding give severity, exact code evidence, attacker path, consequence, and the smallest safe correction. Do not modify code unless the user requested a fix.

For an implementation request, prefer fail-closed behavior and existing security helpers. Keep platform branches equivalent where their capabilities overlap. Compile and exercise the affected path with `$build-northstar`; run `./scripts/dev.sh smoke` from a POSIX shell when available. Update `SECURITY.md` only when the documented threat model or guarantees actually change.
