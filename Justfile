
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

arm:
  docker build -f arch/Dockerfile.arm -t dp-cross-arm:latest .
  docker run -itd -w /mnt/deadpool -v $PWD:/mnt/deadpool --name dp_cross_arm dp-cross-arm:latest
  docker exec -it dp_cross_arm bash -c "cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=arch/arm-toolchain.cmake"
  docker exec -it dp_cross_arm bash -c "cmake --build ./build --target allocator_test"
  docker exec -it dp_cross_arm bash -c "qemu-arm-static -L /usr/arm-linux-gnueabihf build/test/allocator_test"
  docker rm -f dp_cross_arm

neon-test:
  docker build -f arch/Dockerfile.arm -t dp-cross-arm:latest .
  docker run -itd -w /mnt/deadpool -v $PWD:/mnt/deadpool --name dp_cross_arm dp-cross-arm:latest
  docker exec -it dp_cross_arm bash -c "arm-linux-gnueabihf-gcc -static -march=armv7 -mtune=cortex-a8 -mfpu=neon clz_test.c -o clz_test"
  docker exec -it dp_cross_arm bash -c "qemu-arm-static clz_test"
  docker rm -f dp_cross_arm

clean:
  rm -rf ./build/
