OBJS := main.o
BUILDDIR := build

CXXFLAGS ?=
CXXFLAGS += -g -Wall -Wextra -std=c++11 -stdlib=libc++
CXXFLAGS += $(ARCHFLAGS)

LDFLAGS ?=
LDFLAGS += -lssl -lcrypto

# TODO: make these find or build OpenSSL properly
OPENSSL_VER := 1.0.1f
CXXFLAGS += -Iopenssl-$(OPENSSL_VER)/include
LDFLAGS := -Lopenssl-$(OPENSSL_VER) $(LDFLAGS)


.PHONY: all
all: $(BUILDDIR)/sslscan

$(BUILDDIR)/sslscan: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) -o $@

# Pull in dependency info for *existing* .o files
-include $(OBJS:.o=.d)

# This does the actual work of building things
%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $*.cpp -o $*.o
	$(CXX) -MM $(CXXFLAGS) $*.cpp > $*.d
	@mv -f $*.d $*.d.tmp
	@sed -e 's|.*:|$*.o:|' < $*.d.tmp > $*.d
	@sed -e 's/.*://' -e 's/\\$$//' < $*.d.tmp | fmt -1 | \
	  sed -e 's/^ *//' -e 's/$$/:/' >> $*.d
	@rm -f $*.d.tmp


.PHONY: run
run: $(BUILDDIR)/sslscan
	@$(BUILDDIR)/sslscan $(ARGS)

.PHONY: valgrind
valgrind: $(BUILDDIR)/sslscan
	@valgrind --leak-check=full \
			  --dsymutil=yes \
			  --suppressions=valgrind.supp \
			  --malloc-fill=AD \
			  --free-fill=DE \
			  -- \
			  $(BUILDDIR)/sslscan $(ARGS)

.PHONY: clean
clean:
	$(RM) $(BUILDDIR)/sslscan *.o *.d
