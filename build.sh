cd demo/build
rm -rf CMakeCache.txt  CMakeFiles  cmake_install.cmake  Makefile
cmake ..
make clean
make
