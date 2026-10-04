# IW7 soundbank subsets

Discover candidate aliases without exporting audio samples:
`ZoneTool.exe -headless -soundbank-catalog <Zombies-map> <bank> <safe-output-name>`.
The map is one of `cp_zmb`, `cp_rave`, `cp_disco`, `cp_town`, or `cp_final`.
The command writes an exclusive, readback-verified CSV under `enemy-catalog/`,
listing every alias head's IDs, secondary/stop references, duck, load type and
channel. The output name accepts 1–128 lowercase letters, digits or underscores;
existing outputs and a linked output directory are refused.

A source file `soundbank/<unique-bank>.subset.json` selects aliases from an
already loaded retail bank. Add `soundbank,<unique-bank>` to the build recipe
after requiring its donor zone. The ordinary `<unique-bank>.json` source must
not also exist.

```json
{
  "version": 1,
  "donor": "donor-bank.all",
  "prefixes": ["enemy_"],
  "aliases": ["another_explicit_root"],
  "requiredAliases": ["enemy_attack", "enemy_death"],
  "allowedDependencies": ["shared_footstep"],
  "externalAliases": [],
  "externalDuckIds": []
}
```

`prefixes` and `aliases` are root selectors. Every supplied prefix and root must
exist. `requiredAliases` must be present in the resulting closure. Secondary,
stop, and referenced duck aliases are followed recursively. A dependency outside
the root selectors requires an exact `allowedDependencies` entry. An alias
outside the donor requires an exact `externalAliases` entry; it remains an
external reference and must exist in the recipient's loaded banks. Missing
external ducks require explicitly reviewed numeric `externalDuckIds`.

The clone keeps the donor's zone, languages, asset IDs and sample addressing.
It does not export or repack audio samples: the legal stock SAB files must still
be present at runtime. Only selected lists, their heads and referenced donor
ducks are copied. The alias hash index is rebuilt and every lookup checked.
Donor ambient, zone, mix, reverb, send-effect and music tables are removed. The
database donor is never modified.

Each build writes a local `soundbank-subsets/<unique-bank>.json` report with the
selected names, sample asset IDs, external references and index verification.
Keep generated reports and retail dumps out of source control. Inspect the
closure for unwanted dialogue and validate sample resolution and playback in
the recipient: a successful package build proves neither audibility nor
recipient audio compatibility. Unknown fields, ambiguous hashes, incomplete
donors and unreviewed dependencies fail the build.
