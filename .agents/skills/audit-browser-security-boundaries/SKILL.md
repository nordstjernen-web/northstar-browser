---
name: audit-browser-security-boundaries
description: Audit browser-engine changes that process untrusted content or cross native-memory, origin, network, storage, extension, decoder, sandbox, or operating-system boundaries. Use for security reviews, threat analysis, and security fixes where parsing, lifetime, isolation, or containment guarantees could regress.
---

# Audit Browser Security Boundaries

Read the repository instructions, threat model, changed code, and its callers. Preserve unrelated changes. Treat all fetched content and metadata as attacker-controlled unless the threat model says otherwise.

## Model the path

1. Identify the attacker-controlled entry point, trust transition, security invariant, sensitive operation, and possible impact.
2. Trace changed values through parsers, conversions, queues, callbacks, caches, persistence, and native APIs to their final use.
3. Distinguish prevention boundaries such as origin checks from containment boundaries such as a process sandbox. Do not claim one provides the other.

## Review the invariants

- Memory: length conversion, arithmetic overflow, allocation caps, bounds, nesting depth, cleanup, ownership, and use-after-free
- Lifetime and concurrency: cancellation, callbacks, worker threads, shared state, teardown order, and stale native handles
- Web security: URL canonicalization, same-origin checks, site partitioning, redirects, mixed content, CORS, CSP, SRI, scopes, and permissions
- Persistence: private mode, path containment, file permissions, cache keys, secret handling, and extension identity
- Containment: privilege drop, syscall and filesystem restrictions, executable-memory policy, child processes, and platform-specific mitigations

Do not weaken verification, limits, isolation, or sandbox rules to improve compatibility. Prefer fail-closed behavior at security boundaries.

## Report or fix

For a review, report only findings with a concrete attacker path and consequence. Include severity, exact evidence, violated invariant, impact, and the smallest safe correction. Do not edit code during a review-only request.

For a requested fix, reuse established security helpers and keep equivalent platform paths aligned where capabilities overlap. Compile and exercise the affected path, run deterministic smoke cases, and update the threat-model documentation only when its guarantees change.

## Northstar context

Read `SECURITY.md`. Northstar processes untrusted pages in a single native process, so JavaScript realm separation does not contain native memory corruption. Pay particular attention to `src/security.c`, `src/net.c`, `src/csp.c`, `src/cache.c`, `src/idb.c`, `src/ext.c`, `src/js.c`, image/audio decoders, and platform startup code.
