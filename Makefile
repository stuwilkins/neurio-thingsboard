INSTALL = /usr/bin/install -c
INSTALLDATA = /usr/bin/install -c -m 644
SUBDIRS = src

ifeq ($(PREFIX),)
	PREFIX := /usr/local
endif

.PHONY: $(SUBDIRS) all install clean

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean:
	$(MAKE) -C $(SUBDIRS) clean

install: neurio-thingsboard
	$(INSTALL) -m 0755 bin/neurio-thingsboard $(PREFIX)/bin
