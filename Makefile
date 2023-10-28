.DEFAULT_GOAL := build/main
CXX_FLAGS := -O3 -Wall -Werror -std=c++20

build:
	mkdir -p build

build/main: build main.cpp ring_buffer.h
	${CXX} ${CXX_FLAGS} main.cpp -o build/main

run: build/main
	./build/main

.PHONY: clean
clean:
	rm -r build
