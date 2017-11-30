# might need to change this
IFRITDIR := /root/IFRit


CC := clang
IFRITPASS := $(IFRITDIR)/LLVM/IFRit/IFRIT.so
IFRITRTDIR   := $(IFRITDIR)/Runtime 


GLIB_CFLAGS=-pthread `pkg-config --cflags glib-2.0`
GLIB_LIBS=-lpthread  -lgthread-2.0 -lglib-2.0

CFLAGS := -O0 -D_GLIBCXX_USE_CXX11_ABI=0 -I/usr/local/include -I/usr/local/include/c++/v1/ -g
#  -stdlib=libc++ 
LDFLAGS_MPX := -L$(IFRITRTDIR) -lIFR_Runtime
LDFLAGS_IFRIT := -L$(IFRITRTDIR) -lIFR_Runtime_orig

LOADPASS := -Xclang -load -Xclang $(IFRITPASS)

SOURCES := test.c test2.c test3.c
OBJECTS := $(SOURCES:.c=.ll) $(SOURCES:.c=.o) $(SOURCES:.c=.mpx)  $(SOURCES:.c=.elf) $(SOURCES:.c=.ifrit)

all:  .DUMMY $(OBJECTS)

.DUMMY: clean

%.ll: %.c
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) $(LOADPASS) -S -emit-llvm -o $@ $^
	 @$(CC) -O2 -g -S -emit-llvm -o $@ $@
%.mpx: %.ll
	$(CC) $(LDFLAGS_MPX) $(GLIB_LIBS) -o $@ $^ 
%.ifrit: %.ll
	$(CC) $(LDFLAGS_IFRIT) $(GLIB_LIBS) -o $@ $^ 

%.o: %.c
	@$(CC) $(CFLAGS) $(GLIB_CFLAGS) -c -o $@ $^
%.elf: %.o
	$(CC) $(GLIB_LIBS) -o $@ $^ 

# ll: $(SOURCES:.c=.ll)
# %.ll: %.c
# 	@$(CC) -O0 $(CFLAGS) $(GLIB_CFLAGS) $(LOADPASS) -S -emit-llvm -o $@ $^

clean:
	@$(RM) *.elf *.bc *.ll *.s *.dot *.pdf *.a *.o *.ll *.txt  *.mpx
