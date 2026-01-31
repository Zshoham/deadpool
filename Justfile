
configure *FLAGS:
  cmake -B build -G Ninja {{FLAGS}} .

test *FLAGS: (configure "-DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON")
  cmake --build ./build --target tests
  ctest --output-on-failure --test-dir build/test/ {{FLAGS}} || true

coverage: (configure "-DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_COVERAGE=ON")
  ctest --test-dir build -T Coverage
  mkdir -p build/cover
  gcovr --html-details --output build/cover/report.html

benchmark *FLAGS: (configure "-DENABLE_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release")
  cmake --build ./build --target allocator_benchmark
  ./build/bench/allocator_benchmark {{FLAGS}}

# Run fuzz tests in fuzzing mode (requires clang). Pass --fuzz=TestSuite.TestName to run specific test.
fuzz *FLAGS: (configure "-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFUZZTEST_FUZZING_MODE=ON")
  cmake --build ./build --target allocator_fuzz
  ./build/test/allocator_fuzz {{FLAGS}}

format:
  clang-format --sort-includes --style=file --verbose -i $(fd -e h -e c -e cpp -e hpp -E external)

arm:
  docker build -f arch/Dockerfile.arm -t dp-cross-arm:latest .
  docker run -itd -w /mnt/deadpool -v $PWD:/mnt/deadpool --name dp_cross_arm dp-cross-arm:latest
  docker exec -it dp_cross_arm bash -c "cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=arch/arm-toolchain.cmake"
  docker exec -it dp_cross_arm bash -c "cmake --build ./build --target tests"
  docker exec -it dp_cross_arm bash -c "qemu-arm-static -L /usr/arm-linux-gnueabihf build/test/allocator_basic_test"
  docker rm -f dp_cross_arm

neon-test:
  docker build -f arch/Dockerfile.arm -t dp-cross-arm:latest .
  docker run -itd -w /mnt/deadpool -v $PWD:/mnt/deadpool --name dp_cross_arm dp-cross-arm:latest
  docker exec -it dp_cross_arm bash -c "arm-linux-gnueabihf-gcc -static -march=armv7 -mtune=cortex-a8 -mfpu=neon clz_test.c -o clz_test"
  docker exec -it dp_cross_arm bash -c "qemu-arm-static clz_test"
  docker rm -f dp_cross_arm

clean:
  rm -rf ./build/
