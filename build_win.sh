set -e

if [ ! -d "glfw" ]; then
  git clone --depth=1 https://github.com/glfw/glfw
  mkdir glfw/build
  cd glfw/build
  cmake -DCMAKE_TOOLCHAIN_FILE=../../windows/mingw64.cmake -DGLFW_BUILD_EXAMPLES=0 -DGLFW_BUILD_TESTS=0 -DGLFW_BUILD_DOCS=0 ..
  make -j4
  cd ../../
fi

if [ ! -d "curl" ]; then
  CC=x86_64-w64-mingw32-gcc
  export CC
  git clone --depth=1 https://github.com/curl/curl
  cd curl
  ./buildconf
  mkdir build
  cd build
  ../configure --host=x86_64-w64-mingw32 --disable-shared --disable-ldap --with-winssl
  make -j4
  cd ../../
fi

x86_64-w64-mingw32-gcc -static -O2 -std=gnu99 -DHAVE_CURL -DCURL_STATICLIB -D_DEBUG -D_POSIX_C_SOURCE=200809 *.c jfes/*.c -o toy.exe \
-Lglfw/build/src -Iglfw/include -Lcurl/build/lib/.libs/ -Icurl/include \
-lcurl -lglfw3 -lpthread -lm -lgdi32 -lws2_32 -lcrypt32
