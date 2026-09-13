# PathGuard LKM loader probe

`pathguard_probe.ko` is a module-loader smoke test for the SukiSU Ultra
`android16-6.12` DDK path. It does not install any hook, device, callback, or
hide policy.

The GitHub Actions build uses the same pinned DDK image family as SukiSU:

```text
ghcr.io/ylarod/ddk-min:android16-6.12-20260828
```

The workflow only builds and inspects the ELF. It never connects to or changes
an Android device. A successful build is not device-load evidence and does not
activate Hide 1.0.
