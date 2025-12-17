from setuptools import setup, Extension

module = Extension(
    'pyzgc',
    sources=[
        'src/pyzgcmodule.c',
        'src/zheap.c',
        'src/zobject.c',
        'src/zgc.c',
        'src/zbarrier.c',
        'src/zmarkstack.c',
    ],
    include_dirs=['src'],
    extra_compile_args=['-std=c11', '-O3', '-pthread'],
)

setup(
    ext_modules=[module],
)
