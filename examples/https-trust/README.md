# Explicit platform certificate import

**Console limitation (6.02):** explicit imports succeed, but they do not repair
elevated `sceHttp` requests to prosperostore.net. The native known-CA lookup still
reads its sandbox-relative bundle even when that root was explicitly imported.
The request fails with `0x8095f00c`; this helper is not an elevated-HTTPS fix.
Use a separately validated transport rather than disabling certificate checks.

The SSL library's normal sandbox-relative certificate path can become
unreachable after elevation. Keep certificate verification enabled. Call
`https_trust::read_builtin_roots(ssl_context, pem)` before elevation, then
`load_pem_roots(http_context, pem)` on a worker after `sceHttpInit`, before
making templates or requests. The read obtains the platform's
roots through `sceSslGetCaCerts`, preserves PEM or encodes DER as PEM, releases
the platform allocation. The import uses `sceHttpsLoadCert`.
The root query itself can depend on sandbox paths, so it must precede elevation.
`load_builtin_roots` combines both steps for callers whose filesystem view is unchanged.
`load_pem_roots` also accepts an existing trusted PEM bundle. Never substitute
a network-supplied CA file. The on-disk `CA_LIST.cer` may contain a different
set of roots from the library's built-in store.

The helper bounds input to 512 KiB, 256 roots, and 16 KiB per PEM certificate
(12 KiB per built-in DER certificate);
the platform validates each certificate and copies its data during the call.
Use a sufficiently sized SSL pool for the explicit imports (2 MiB in the
ProsperoStore adopter). All hostname, validity, CA and signature checks remain
enabled. A parsing or platform error must stop the request.

`make test-https-trust` checks the API layout and input limits under sanitizers.
The public five-argument API and its pointer/size certificate records were
cross-checked against the local module's export and argument handling. Native
validation belongs to each adopter's console evidence.
