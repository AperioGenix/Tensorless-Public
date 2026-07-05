# Diagnostic tools

These executables inspect or profile Tensorless behavior but are not part of the
core library. `rg_closure_diagnostic` intentionally remains outside CTest
because it measures a nonzero closure residual rather than asserting exact
closure.

`shadow_oracle` diagnoses whether reduced coarse-state keys alias different
future flux outcomes. Its input load is supplied by the caller:

```bash
./build/shadow_oracle INJECTED_LOAD [OUTPUT_JSON] [TICKS]
```

`INJECTED_LOAD` must be a positive `uint64_t` integer. The tool has no default
load because the appropriate stress fixture belongs to the experiment, not
the domain-blind diagnostic. `TICKS` defaults to 34 and must be a positive
`uint32_t` integer when supplied. `shadow_oracle` is not registered with
CTest; its fixed diagnostic topology processes millions of flux events and
reports aliasing measurements rather than a regression-test verdict.
