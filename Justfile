
configure:
  cmake -B build .

test coverage="OFF":
  cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE={{coverage}} .
  cmake --build ./build --target allocator_test
  ctest --output-on-failure --test-dir build/test/ || true
  
coverage: (test "ON")
  ctest --test-dir build -T Coverage
  mkdir -p build/cover
  gcovr --html-details --output build/cover/report.html

benchmark:
  cmake -B build .
  cmake --build ./build --target allocator_benchmark
  ./build/bench/allocator_benchmark
 
fuzz:
  just clean
  CXX=clang CC=clang cmake -B build -DCMAKE_BUILD_TYPE=Debug .
  cmake --build ./build --target allocator_fuzz

clean:
  rm -rf ./build/
