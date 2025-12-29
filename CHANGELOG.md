# Changelog

All notable changes to this project will be documented in this file.

## [0.3.0b1] - 2025-12-29

### Added
- **Automatic Root Scanning**: Implemented stack walking to automatically find and mark `ZObject` roots on the stack, simplifying user API.
- **Full Generational ZGC**: Implemented Minor GC for Young Generation, Remembered Sets for Old->Young references, and promotion logic.
- **macOS Support**: Fixed signal barrier issues on macOS by implementing a fallback to software barriers (disabled signal barriers on macOS by default).
- **Leak Fixes**: Fixed multiple memory leaks including page reclamation, padding leaks, and ZObject cycle leaks.
- **Stability**: Fixed critical segmentation faults related to TLAB retirement and cycle counting.

### Changed
- **TLAB Logic**: Added cycle count check to TLAB retirement to prevent access to reclaimed pages.
- **Project Structure**: Cleaned up benchmarks and tests.

## [0.2.0] - 2025-12-28

### Added
- **JIT Integration**: New `src/zjit_interface.h` header and `zbarrier_fix_pointer_jit` helper to allow external JIT compilers (like PyPy, Cinder) to inline Load Barriers.
- **JIT Verification**: Added `tests/test_jit_interface.py` to verify the JIT interface contract.
- **Signal-Based Barriers**: Implemented experimental support for using OS signals (`SIGSEGV`) to eliminate read barrier overhead. Enabled by default on Linux, opt-in on macOS.

### Changed
- **Performance Optimization**: Removed redundant "Result Barriers" in `ZObject.load`, `ZObject.getitem`, and `ZObject.store`.
    - **Allocation Speed**: Improved to **~0.43s** for 10M objects (7.6x faster than CPython).
    - **Read Overhead**: Stabilized at ~35% overhead for pure graph traversal, negligible for mixed workloads.
- **Code Cleanup**: Refactored barrier logic in `src/zbarrier.c` to be more modular.

### Fixed
- Fixed inconsistencies between `zheap.h` and `zjit_interface.h` bitmask definitions.

## [0.1.0] - 2025-12-01

### Added
- Initial release of `pyzgc`.
- Core ZGC features: Colored Pointers, Load Barriers, Relocation.
- Thread-Local Allocation Buffers (TLABs).
- Basic benchmark suite.
