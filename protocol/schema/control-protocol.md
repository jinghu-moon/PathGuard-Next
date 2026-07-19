# Control Protocol

This directory reserves the versioned wire contract shared by `pathguardd`,
`pathguardctl`, and the Manager App.

The first implementation uses length-prefixed local messages. Payloads must be
bounded, versioned, and independent from the user-facing `rules.ini` syntax.
