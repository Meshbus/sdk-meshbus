# Meshbus CLI

`meshbus-cli` builds native Meshbus LLEXT application (`.mba`) and boot-service
(`.mbs`) packages from a released Meshbus EDK.

Install the package into an isolated environment:

```sh
pipx install meshbus-cli
```

Build one extension package:

```sh
meshbus llext --llext-sdk /path/to/llext-edk \
  --output-dir build/llext /path/to/package-source
```

The CLI validates the EDK manifest and SDK digest, compiles and normalizes the
extension ELF, estimates loader heap requirements, and injects Meshbus metadata
and app icon data. It does not generate firmware EDKs; that remains a firmware
workspace release operation.

## Build the distribution

Run from this directory, or pass `scripts/llext` as the project directory:

```sh
python -m build
python -m twine check dist/*
```

The wheel and source distribution contain only the EDK consumer modules. West
EDK production, EDK qualification, and DFOTA implementation files are
deliberately excluded. Inspect both artifacts before publication:

```sh
unzip -l dist/*.whl
tar -tf dist/*.tar.gz
```

Publish to TestPyPI before the production index:

```sh
python -m twine upload --repository testpypi dist/*
python -m twine upload dist/*
```
