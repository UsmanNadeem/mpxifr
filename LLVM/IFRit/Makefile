# might need to change this
IFRITDIR := /root/IFRit

PLUGIN = InsertDRDCalls
PLUGIN_OUT = IFRIT
CXX := clang

IFRITPASSDIR := $(IFRITDIR)/LLVM/IFRit
IFRITRTDIR   := $(IFRITDIR)/Runtime 


CXXFLAGS := ${EXTRA_FLAGS}
# LLVM_DIR = /home/linux/Desktop/LLVM/llvm
# LLVM_BUILD_DIR = /home/linux/Desktop/LLVM/build
# CLANG_DIR = $(LLVM_DIR)/tools/clang
# CLANG = $(LLVM_BUILD_DIR)/bin/clang
FORCE_USE_ABI=0

# Compiler flags.
# CXXFLAGS  = -I$(LLVM_DIR)/include -I$(CLANG_DIR)/include -I$(LLVM_DIR)/llvm/include 
# CXXFLAGS += -I$(LLVM_BUILD_DIR)/include -I$(LLVM_BUILD_DIR)/tools/clang/include
# CXXFLAGS += -I/usr/local/include
#-stdlib=libc++ -D_GLIBCXX_USE_CXX11_ABI=0 -fpic
CXXFLAGS += -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS 
# -Wno-long-long
CXXFLAGS += -fPIC 
# -fvisibility-inlines-hidden
CXXFLAGS += -fno-exceptions -fno-rtti -std=c++11
CXXFLAGS += -Wall -D_GLIBCXX_USE_CXX11_ABI=${FORCE_USE_ABI}
CXXFLAGS += -O1 -w



IFRITPASS := $(IFRITPASSDIR)/IFRIT.so
# CFLAGS := -O1 -D_GLIBCXX_USE_CXX11_ABI=0 -I/usr/local/include -I/usr/local/include/c++/v1/
 # -stdlib=libc++ -I/home/linux/Desktop/IFRit/Runtime/libIFR_Runtime.so


# Linker flags.
LDFLAGS = -shared -Wl,-undefined,dynamic_lookup

$(PLUGIN).so : $(PLUGIN).o
	$(CXX) $(LDFLAGS) -o $(PLUGIN_OUT).so $(PLUGIN).o

$(PLUGIN).o : $(PLUGIN).cpp
	$(CXX) $(CXXFLAGS) -c $(PLUGIN).cpp -o $(PLUGIN).o

clean :
	rm -fv $(PLUGIN).o $(PLUGIN_OUT).so