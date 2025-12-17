# 📦 Publishing pyzgc to PyPI

I have prepared the repository with the necessary configuration files (`pyproject.toml`, `setup.py`, `MANIFEST.in`, `LICENSE`).

## 1. Prerequisites
You need `build` and `twine` installed:
```bash
pip install build twine
```

## 2. Build the Package
Run this command to create the Source Distribution (`.tar.gz`) and Wheel (`.whl`):
```bash
python3 -m build
```
This will create a `dist/` directory containing your artifacts.

## 3. Test the Package (Optional but Recommended)
Upload to TestPyPI first to verify everything looks good:
```bash
python3 -m twine upload --repository testpypi dist/*
```

## 4. Publish to PyPI
When you are ready to release to the world:
```bash
python3 -m twine upload dist/*
```

## 5. CI/CD (GitHub Actions)
To automate this, you can add a GitHub Action.
Create `.github/workflows/publish.yml`:

```yaml
name: Publish to PyPI

on:
  release:
    types: [published]

jobs:
  build-and-publish:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v3
    - name: Set up Python
      uses: actions/setup-python@v4
      with:
        python-version: '3.9'
    - name: Install dependencies
      run: |
        python -m pip install --upgrade pip
        pip install build
    - name: Build package
      run: python -m build
    - name: Publish to PyPI
      uses: pypa/gh-action-pypi-publish@release/v1
      with:
        password: ${{ secrets.PYPI_API_TOKEN }}
```
