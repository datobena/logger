## LoggerPass: log function calls, arguments, and returns

### Build

```bash
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --parallel
```

The build produces `build/passes/LoggerPass.so`.

### Quick test

```bash
cat > /tmp/t.c <<'EOF'
#include <stdio.h>
int add(int a, int b) { return a + b; }
int main() { int x = add(2, 5); printf("x=%d\n", x); return 0; }
EOF
clang -fpass-plugin=$(pwd)/passes/LoggerPass.so /tmp/t.c -o /tmp/t && /tmp/t
```

### Makefile integration (large projects)

- Export wrapper compilers that automatically inject the plugin:

```bash
chmod +x scripts/logger-clang scripts/logger-clang++
export CC=$(pwd)/scripts/logger-clang
export CXX=$(pwd)/scripts/logger-clang++
export LOGGER_PASS_SO=$(pwd)/build/passes/LoggerPass.so
make -e clean all
```

Most Makefiles respect `CC`/`CXX`. The wrappers forward all flags and add `-fpass-plugin=...`.

Alternatively, append the flag directly to your `CFLAGS`/`CXXFLAGS`:

```bash
export CFLAGS+=" -fpass-plugin=$(pwd)/build/passes/LoggerPass.so"
export CXXFLAGS+=" -fpass-plugin=$(pwd)/build/passes/LoggerPass.so"
make -e
```

### What gets logged

- On every function with a body (excluding intrinsics and `printf`):
  - Entry line: `>> func`
  - Each argument on entry as one line (ints as `%lld`, floats as `%f`, pointers as `%p`, aggregates noted as `(aggregate)`).
  - Return just before each `ret`: `<< func returns ...` (or `returns void`).

### Notes and limitations

- The pass inserts calls to `printf`, so output ordering follows standard I/O behavior.
- Integer signedness is not tracked; values are printed zero-extended as `%lld`.
- Floating-point values are printed as `double` (`%f`).
- Aggregates (structs/arrays) are not expanded; they are marked `(aggregate)`.
- To avoid recursion, functions named `printf` and any starting with `__logger` are not instrumented.

### Using with `opt`

```bash
opt -load-pass-plugin ./build/passes/LoggerPass.so -passes=logger-fn -S input.ll -o output.ll
```


