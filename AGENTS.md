# Repository Guidelines

MTPNDD adds Lace-backed parallelism on top of Sylvan; most contributions should improve parallel throughput, scalability, and contention behavior.

## Project Structure & Module Organization

- `sylvan/`: vendored Sylvan + the MTPNDD C implementation.
  - Core MTPNDD sources: `sylvan/src/sylvan/mtpndd/` (`mtpndd_*.c/.h`, public API in `mtpndd.h`).
  - C tests: `sylvan/test/` (run via CTest).
- `jni/`: Java API + JNI native shim.
  - Java sources: `jni/src/main/java/org/ants/mtpndd/`
  - JNI C/C++ sources: `jni/src/main/native/`
  - Java tests: `jni/src/test/java/`
- `docs/`: architecture/performance notes and diagrams.
- `lib/`: bundled third-party Java artifacts.

## Build, Test, and Development Commands

- Native (C):  
  `cd sylvan && cmake -B build -DMTPNDD_LOG_LEVEL=2 && cmake --build build`
- Run the n-queens smoke test:  
  `./sylvan/build/src/sylvan/mtpndd/mtpndd_nqueens_test 8`
- Run C tests (if built):  
  `ctest --test-dir sylvan/build`
- JNI build + unit tests:  
  `cd jni && cmake -B build -DMTPNDD_LOG_LEVEL=2 && cmake --build build`  
  `mvn -DskipTests package`  
  `mvn -Dorg.ants.mtpndd.library.path="$PWD/build/libmtpnddjni.so" test`

## Coding Style & Naming Conventions

- C/C++: 4-space indentation; K&R braces; prefer `mtpndd_*` for functions, `mtpndd_*_t` for types, `MTPNDD_*` for macros/constants.
- Java: standard 4-space indentation; public classes in `org.ants.mtpndd` use PascalCase; methods/fields use lowerCamelCase.
- Keep API changes paired: update `sylvan/src/sylvan/mtpndd/mtpndd.h` and the Java wrappers as needed.

## Parallel Performance Workflow

- Always include a reproducible baseline and report before/after numbers.
- Preferred micro/meso benchmark: `mtpndd_nqueens_test` (vary `N`, repeat runs, record median).
- If changing parallel behavior, record key knobs: worker count and Lace deque size (see `mtpndd_pal_config_t` / `MTPNDDConfig`), plus any cache/table sizes you touched.
- When optimizing contention, call out the shared structure (e.g., operation cache, nodetable, edge map) and the concurrency strategy (lock-free, sharding, per-worker, batching).

## Testing Guidelines

- C: add tests under `sylvan/test/` and register via `sylvan/test/CMakeLists.txt` (`add_test(...)`).
- Java: JUnit 5 tests under `jni/src/test/java/`, typically `*Test.java`.
- For performance-related changes, include a brief benchmark note (e.g., n-queens size, worker count, before/after).

## Commit & Pull Request Guidelines

- Commits follow a Conventional Commits-style pattern (often with optional emoji): `feat(mtpndd): ...`, `fix: ...`, `perf: ...`, `refactor: ...`, `docs: ...`.
- PRs should include: what changed, how to build/test (commands), and any relevant benchmark table (N, workers, dqsize, machine) plus DOT/diagram outputs if applicable.

## Agent-Specific Notes

- If using Codex "superpowers", bootstrap first: `~/.codex/superpowers/.codex/superpowers-codex bootstrap`.
- Keep generated artifacts out of diffs: build outputs live in `**/build/` and Maven outputs in `target/` (both are gitignored).
