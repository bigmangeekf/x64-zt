# Asset-free native tests

Run from the repository root with Visual Studio 2022 C++ tools available:

```powershell
msbuild tests/filesystem_ownership_tests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64
./build/tests/filesystem_ownership_tests.exe
msbuild tests/sound_bank_subset_tests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64
./build/tests/sound_bank_subset_tests.exe
```

The filesystem target compiles the production `filesystem.cpp` with a minimal
test precompiled-header substitute and directory-helper definitions. It checks
close/exists/destruction, copy and move ownership, successful and failed reopen,
and empty-mode rejection. Exclusive Windows file opens detect leaked streams;
no game assets or game process are used. The corrected `close()` clears its
owned pointer even if `fclose` fails and treats an already closed object as a
successful no-op. `open()` releases the previous stream before replacing it.

The soundbank target requires the JSON submodule. It checks schema ranges,
synthetic alias-index lookups and verified report publication. These tests do
not establish retail dependency closure, package loading or audio playback.

Each executable creates a unique temporary test directory and removes only its
own known files on success. On failure, the directory remains for diagnosis.
