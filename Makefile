
# NOTE: This makefile is for Linux and MacOS (Apple silicon) builds.
#	Use Visual Studio project for Windows build.
#	Use "make" or "make all" to do a normal build.
#       Use "make NO_OPENCL=1" or "make all NO_OPENCL=1" to do a build without OpenCL support.
#       use "make TESTS=1" to build also itl2tests project.
#		Use "make BOUNDS_CHECK=1" to build a version with image access bounds checking. Usually
#			bounds checking is not necessary unless tracking bugs etc.

CFLAGS := -O3

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # MacOS, assuming gcc compiler installed with homebrew
    $(info Detected MacOS)
    CXXFLAGS := -fopenmp -O3 -std=c++17 -fvisibility=hidden -I/opt/homebrew/include -I/opt/homebrew/opt/opencl-clhpp-headers/include
    LDFLAGS := -fopenmp -lblosc -L/opt/homebrew/lib/
	PLATFORM := macos
else
    # Linux
    $(info Detected Linux)
    CXXFLAGS := -fopenmp -O3 -std=c++17 -fvisibility=hidden
    LDFLAGS := -fopenmp -lblosc
	PLATFORM := linux64
endif

OPENCL_LIB := -lOpenCL
CONFIG := release

# Include Makefile.local and if it does not exist, build with native arch.
sinclude Makefile.local
ifeq ("$(wildcard Makefile.local)","")
    $(info No Makefile.local found, using native arch.)
    CXXFLAGS += -march=native
endif
    

ifdef NO_OPENCL
    CXXFLAGS += -DNO_OPENCL
    CONFIG = release-nocl
    OPENCL_LIB=
endif

ifdef OLD_OPENCL
    CXXFLAGS += -DOLD_OPENCL
endif

ifdef DDEBUG
    CXXFLAGS += -DDEBUG -g
    CCFLAGS += -DDEBUG -g
endif


ifdef BOUNDS_CHECK
	CXXFLAGS += -DBOUNDS_CHECK
endif


TEMP_DIR := $(shell pwd)/intermediate
BUILD_ROOT = $(TEMP_DIR)/$(CONFIG)/$@

export CFLAGS
export CXXFLAGS
export LDFLAGS
export OPENCL_LIB
export CONFIG
export TEMP_DIR 
export BUILD_ROOT


ifeq ($(CONFIG), release)
   	CS_CONFIG = Release
else ifeq ($(CONFIG), release-nocl)
   	CS_CONFIG = Release no OpenCL
endif


.PHONY: all clean itl2 pilib pi2 itl2tests

all: itl2tests itl2 pilib pi2
	
	# Construct full distribution to bin-$(PLATFORM)/$(CONFIG) folder
	mkdir -p bin-$(PLATFORM)/$(CONFIG)
	cp ./intermediate/$(CONFIG)/pilib/libpi.so ./bin-$(PLATFORM)/$(CONFIG)/
	cp ./intermediate/$(CONFIG)/pi2/pi2 ./bin-$(PLATFORM)/$(CONFIG)/
	cp ./x64/$(CONFIG)/*.exe ./bin-$(PLATFORM)/$(CONFIG)/ | true
	cp ./x64/$(CONFIG)/*.dll ./bin-$(PLATFORM)/$(CONFIG)/ | true
	cp ./x64/$(CONFIG)/*.exe.config ./bin-$(PLATFORM)/$(CONFIG)/ | true
	cp ./x64/$(CONFIG)/*.xml ./bin-$(PLATFORM)/$(CONFIG)/ | true
	cp ./python_scripts/*.py ./bin-$(PLATFORM)/$(CONFIG)/
	chmod +x ./bin-$(PLATFORM)/$(CONFIG)/*.py
	cp ./example_config/*.txt ./bin-$(PLATFORM)/$(CONFIG)/
	cp ./example_config/*.sh ./bin-$(PLATFORM)/$(CONFIG)/
	chmod +x ./bin-$(PLATFORM)/$(CONFIG)/*.sh
	cp ./example_config/*.cmd ./bin-$(PLATFORM)/$(CONFIG)/
	cp ./LICENSE.txt ./bin-$(PLATFORM)/$(CONFIG)/

	# TODO: Remove this if it is not necessary.
	# For ease of use of .NET builds using pi2, construct full distribution also to
	# "x64/$(CS_CONFIG)" folder. That way the .NET builds can be done with msbuild as usual,
	# without worrying about final platform-specific output folder name.
	mkdir -p "./x64/$(CS_CONFIG)/"
	cp ./bin-$(PLATFORM)/$(CONFIG)/*.so "./x64/$(CS_CONFIG)/"
	cp ./bin-$(PLATFORM)/$(CONFIG)/pi2 "./x64/$(CS_CONFIG)/"
	cp ./example_config/*.txt "./x64/$(CS_CONFIG)/"

clean: itl2tests itl2 pilib pi2

itl2:
	$(MAKE) -C $@ $(MAKECMDGOALS)

pilib: itl2
	-./pilib/create_commit_info.sh >/dev/null 2>/dev/null
	$(MAKE) -C $@ $(MAKECMDGOALS)

pi2: pilib
	$(MAKE) -C $@ $(MAKECMDGOALS)

ifdef TESTS
itl2tests: itl2
	$(MAKE) -C $@ $(MAKECMDGOALS)
	cp ./intermediate/$(CONFIG)/itl2tests/itl2testsmain ./bin-$(PLATFORM)/$(CONFIG)/
else
itl2tests: ;
endif
