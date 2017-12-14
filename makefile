#
# Copyright (c) 2015 Santiago Bock
#
# See the file LICENSE.txt for copying permission.
#

##############################################################
#
# Customize build
#
##############################################################

# Debug
DEBUG=0

# Print debug output
DEBUG_OUTPUT = 1

# To use custom compiler
CXXHOME = /usr

#Home of TC malloc
TC_MALLOC = ~/opt/tcmalloc

# Where PIN is installed
PIN_ROOT ?= ~/opt/pin

# Actual compiler to use
#CXX = $(CXXHOME)/bin/g++-4.8
#CXX = $(CXXHOME)/bin/g++-5
CXX = $(CXXHOME)/bin/g++-6

##############################################################
#
# set up and include *.config files
#
##############################################################

KIT=1

CONFIG_ROOT := $(PIN_ROOT)/source/tools/Config

PINPLAY_HOME=$(PIN_ROOT)/extras/pinplay
PINPLAY_INCLUDE_HOME=$(PINPLAY_HOME)/include
PINPLAY_LIB_HOME=$(PINPLAY_HOME)/lib/$(TARGET)
EXT_LIB_HOME=$(PINPLAY_HOME)/lib-ext/$(TARGET)

include $(CONFIG_ROOT)/makefile.config
include $(TOOLS_ROOT)/Config/makefile.default.rules

##############################################################
#
# Variable definition
#
##############################################################



# Flags
CUSTOM_CXXFLAGS += -MMD -DDEBUG=$(DEBUG_OUTPUT) -D_FILE_OFFSET_BITS=64 -std=c++11 -Wall -Werror -iquoteinclude
#CUSTOM_FLAGS += -D_GLIBCXX_DEBUG

#For profiling with oprofile
CUSTOM_CXXFLAGS += -g -fno-omit-frame-pointer

#For profiling with gprof
#CUSTOM_CXXFLAGS += -pg -fno-omit-frame-pointer
#APP_LDFLAGS += -pg

#For shared libraries
CUSTOM_LIBS += -lbz2 -lz -Wl,-rpath -Wl,$(CXXHOME)/lib64

#For static libraries
#CUSTOM_LIBS += -lbz2 -lz -static-libstdc++ -static-libgcc

#For using TCmalloc
CUSTOM_LIBS += -ltcmalloc_minimal
CUSTOM_LPATHS += -L$(TC_MALLOC)/lib


#App flags
APP_CXXFLAGS += $(CUSTOM_CXXFLAGS)
APP_LDFLAGS += $(CUSTOM_LDFLAGS)
APP_LPATHS += $(CUSTOM_LPATHS)
APP_LIBS += $(CUSTOM_LIBS)

#Tool flags
#TOOL_CXXFLAGS  += $(CUSTOM_CXXFLAGS) -I$(PINPLAY_INCLUDE_HOME)
TOOL_CXXFLAGS  += $(CUSTOM_CXXFLAGS) -fabi-version=2 -I$(PINPLAY_INCLUDE_HOME)
TOOL_LDFLAGS += $(CUSTOM_LDFLAGS)
TOOL_LPATHS += -L$(PINPLAY_LIB_HOME) $(CUSTOM_LPATHS)
TOOL_LIBS += $(CUSTOM_LIBS)

##############################################################
#
# Variable definition for build process
#
##############################################################

APP_ROOTS = analyze convert merge parse sim split texter

APPS = $(APP_ROOTS:%=$(OBJDIR)%)

TOOL_ROOTS = TracerPin

TOOLS = $(TOOL_ROOTS:%=$(OBJDIR)%$(PINTOOL_SUFFIX))

##############################################################
#
# build rules
#
##############################################################

# Build everything
all: apps tools

# Accelerate the make process and prevent errors
.PHONY: all

# Include generated dependencies
-include $(OBJDIR)*.d

# Build the applications
apps: $(OBJDIR) $(APPS)

# Build the tools
tools: $(OBJDIR) $(TOOLS)

# Create the output directory
$(OBJDIR):
	mkdir -p $(OBJDIR)

# Compile regular object files
$(OBJDIR)%.o: %.cpp
	$(CXX) $(APP_CXXFLAGS) $(COMP_OBJ)$@ $<

#Compile TracerPin, TraceHandler and Error with (TOOL_CXXFLAGS)
$(OBJDIR)TracerPin.o $(OBJDIR)TraceHandler.o $(OBJDIR)Error.o: $(OBJDIR)%.o : %.cpp
	$(CXX) $(TOOL_CXXFLAGS) $(COMP_OBJ)$@ $<

# Link tools
$(TOOLS): %$(PINTOOL_SUFFIX) : %.o $(PINPLAY_LIB_HOME)/libpinplay.a $(EXT_LIB_HOME)/libbz2.a $(EXT_LIB_HOME)/libz.a $(CONTROLLERLIB)
	$(LINKER) $(TOOL_LDFLAGS) $(LINK_EXE)$@ $^ $(TOOL_LPATHS) $(TOOL_LIBS)

# Link apps
$(APPS): % : %.o
	$(CXX) $(APP_LDFLAGS) $(LINK_EXE)$@  $^ $(APP_LPATHS) $(APP_LIBS)


# Dependencies for object linking
$(OBJDIR)TracerPin.so: $(OBJDIR)TraceHandler.o $(OBJDIR)Error.o
$(OBJDIR)analyze: $(OBJDIR)analyze.o $(OBJDIR)Arguments.o $(OBJDIR)Cache.o $(OBJDIR)Engine.o $(OBJDIR)Error.o $(OBJDIR)Statistics.o $(OBJDIR)TraceHandler.o
$(OBJDIR)convert: $(OBJDIR)convert.o $(OBJDIR)Arguments.o $(OBJDIR)Error.o $(OBJDIR)TraceHandler.o
$(OBJDIR)merge: $(OBJDIR)merge.o $(OBJDIR)Arguments.o $(OBJDIR)Error.o $(OBJDIR)TraceHandler.o
$(OBJDIR)parse: $(OBJDIR)parse.o $(OBJDIR)Arguments.o $(OBJDIR)Error.o $(OBJDIR)Counter.o
$(OBJDIR)sim: $(OBJDIR)sim.o $(OBJDIR)Arguments.o $(OBJDIR)Bank.o $(OBJDIR)Bus.o $(OBJDIR)Cache.o $(OBJDIR)Counter.o $(OBJDIR)CPU.o $(OBJDIR)Engine.o $(OBJDIR)Error.o $(OBJDIR)HybridMemory.o $(OBJDIR)Memory.o $(OBJDIR)MemoryManager.o $(OBJDIR)Migration.o $(OBJDIR)Partition.o $(OBJDIR)Statistics.o $(OBJDIR)TraceHandler.o
$(OBJDIR)split: $(OBJDIR)split.o $(OBJDIR)Arguments.o $(OBJDIR)Error.o $(OBJDIR)TraceHandler.o
$(OBJDIR)texter: $(OBJDIR)texter.o $(OBJDIR)Arguments.o $(OBJDIR)Error.o $(OBJDIR)TraceHandler.o


# Cleaning
#.PHONY: clean
#clean:
#	-rm -rf $(OBJDIR)

# Print variable for debugging the makefile
.PHONY: print
print:
	@echo $(APP_LIBS)
