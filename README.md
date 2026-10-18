## build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPython3_EXECUTABLE="C:/Users/user/AppData/Local/Programs/Python/Python311/python.exe"

## run and test
./build/frontends/windows/wisp-windows.exe -v > log.txt 2>&1
