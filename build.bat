@echo off
echo [BUILD] Creating build folder...
if not exist build mkdir build

echo [BUILD] Compiling source files...
gcc -c src/UselessConfigC/usec.c -Iinclude -Isrc/UselessConfigC -o build/usec.o
gcc -c src/UselessConfigC/parser.c -Iinclude -Isrc/UselessConfigC -o build/parser.o
gcc -c src/UselessConfigC/tokenizer.c -Iinclude -Isrc/UselessConfigC -o build/tokenizer.o
gcc -c src/UselessConfigC/hashtable.c -Iinclude -Isrc/UselessConfigC -o build/hashtable.o
gcc -c src/UselessConfigC/utils.c -Iinclude -Isrc/UselessConfigC -o build/utils.o

echo [BUILD] Archiving libusec.a...
ar rcs build/libusec.a build/*.o

echo [BUILD] Compiling test executable...
gcc -Iinclude test/test.c build/libusec.a -o build/test

echo.
echo [SUCCESS] Built test executable: build/test
echo Run with: .\build\test test\test.usec