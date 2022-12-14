CXX=clang++
CXXFLAGS=-std=c++20 -stdlib=libc++ -fmodules -fbuiltin-module-map -fprebuilt-module-path=build \
  -O3 -mavx -march=native -Wall -Wextra -Wno-sign-compare -Wno-char-subscripts

UTIL_PCMS=build/core.pcm build/util.pcm
UTIL_OBJS=build/util.o

# Doesn't compile without AVX512: problem1 problem5 problem8
# Not tried to port yet: problem12
PROBLEMS=problem6 problem7 problem9 problem13


all: $(addprefix output/,$(PROBLEMS))

PRECOMPILE=$(CXX) $(CXXFLAGS) --precompile -xc++-module
COMPILE_MODULE=$(CXX) $(CXXFLAGS) $(OPTFLAGS) -c
LINK=$(CXX) $(CXXFLAGS)

build/core.pcm: Util/core.ixx; $(PRECOMPILE) $< -o $@
build/util.pcm: Util/util.ixx Util/util.cpp; $(PRECOMPILE) $< -o $@
build/util.o: Util/util.cpp $(UTIL_PCMS); $(COMPILE_MODULE) $< -fmodule-file=build/util.pcm -o $@

build/Problem1.o: Problem1/Problem1.cpp $(UTIL_PCMS); $(COMPILE_MODULE) $< -o $@
build/Problem5.o: Problem5/Problem5.cpp $(UTIL_PCMS); $(COMPILE_MODULE) $< -o $@
build/Problem6.o: Problem6/Problem6.cpp $(UTIL_PCMS); $(COMPILE_MODULE) $< -o $@
build/Problem7.o: Problem7/Problem7.cpp $(UTIL_PCMS); $(COMPILE_MODULE) $< -o $@
build/Problem8.o: Problem8/Problem8.cpp $(UTIL_PCMS); $(COMPILE_MODULE) $< -o $@
build/Problem9.o: Problem9/Problem9.cpp $(UTIL_PCMS); $(COMPILE_MODULE) $< -o $@
build/Problem13.o: Problem13/Problem13.cpp $(UTIL_PCMS); $(COMPILE_MODULE) $< -o $@

output/problem%: build/Problem%.o $(UTIL_OBJS); $(LINK) $< $(UTIL_OBJS) -o $@

clean:
	rm -f build/*

distclean: clean
	rm -f output/*

.PHONY: all clean distclean
