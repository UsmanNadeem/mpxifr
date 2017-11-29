# might need to change this
IFRITDIR := /root/IFRit


CC := clang
IFRITPASS := $(IFRITDIR)/LLVM/IFRit/IFRIT.so
IFRITRTDIR   := $(IFRITDIR)/Runtime 


GLIB_CFLAGS=-pthread `pkg-config --cflags glib-2.0`
GLIB_LIBS=-lpthread  -lgthread-2.0 -lglib-2.0

CFLAGS := -O0 -D_GLIBCXX_USE_CXX11_ABI=0 -I/usr/local/include -I/usr/local/include/c++/v1/ -g
#  -stdlib=libc++ 
LDFLAGS := -L$(IFRITRTDIR) -lIFR_Runtime

LOADPASS := -Xclang -load -Xclang $(IFRITPASS)

SOURCES := test.c
OBJECTS := $(SOURCES:.c=.ll) $(SOURCES:.c=.elf)

all: $(OBJECTS)

# %.o: %.c
	@# @$(CC) $(CFLAGS) $(GLIB_CFLAGS) $(LOADPASS) -c -o $@ $^
%.ll: %.c
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) $(LOADPASS) -S -emit-llvm -o $@ $^
	 @$(CC) -O2 -g -S -emit-llvm -o $@ $@

%.elf: %.ll
	$(CC) $(LDFLAGS) $(GLIB_LIBS) -o $@ $^ 

# ll: $(SOURCES:.c=.ll)
# %.ll: %.c
# 	@$(CC) -O0 $(CFLAGS) $(GLIB_CFLAGS) $(LOADPASS) -S -emit-llvm -o $@ $^

.DUMMY: clean
clean:
	@$(RM) *.elf *.bc *.ll *.s *.dot *.pdf *.a *.o *.ll *.txt
