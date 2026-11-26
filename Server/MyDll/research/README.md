# Native fidelity evidence

This work is an educational reverse-engineering reconstruction of the supplied
1.02i library. The census of the original is an inventory, not proof that a
function is implemented. The port ledger is separate:

- [native-functions.tsv](native-functions.tsv): one scoped function per row.
- [native-coverage.tsv](native-coverage.tsv): original body sizes consumed by
  the instruction comparator.
- [native-symbols.tsv](native-symbols.tsv): reconstructed COFF symbols mapped
  to original VAs; this is not the original export table.

`exact` in the object column means `IDENTICA`, not `IDENTICA_CON_RESIDUAL`, and
an independent byte comparison passes. `linked_exact` additionally means the
same body appears in the linked DLL. `pending` and `not_linked` remain open.

Current exact object coverage includes three linked byte-order helpers,
`cmd_is_slash`, and `f60`. The three linked helpers total 275 bytes. The other
two exact object bodies remain outside the DLL. This is not whole-file or
whole-feature parity.

From the repository root:

```sh
python3 -B Server/MyDll/tools/verify_native_fidelity.py \
  --original "$ORIGINAL_MYDLL" \
  --installed "$INSTALLED_MYDLL" \
  --dll build/mydll-vc6/MyDll.dll \
  --map build/mydll-vc6/MyDll.map \
  --ledger Server/MyDll/research/native-functions.tsv
```

The verifier requires `pefile==2024.8.26` and the two local, pinned reference
artifacts. It never launches either one. Build and behavior tests run under
VC6/Wine, not under the original protected library.
