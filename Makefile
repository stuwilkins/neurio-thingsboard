INSTALL = /usr/bin/install -c
INSTALLDATA = /usr/bin/install -c -m 644
SUBDIRS = src

ifeq ($(PREFIX),)
	PREFIX := /usr/local
endif

all: $(SUBDIRS)

.PHONY: $(SUBDIRS)
$(SUBDIRS):
		$(MAKE) -C $@

.PHONY: install
install: neurio-thingsboard
	$(INSTALL) -m 0755 bin/neurio-thingsboard $(PREFIX)/bin
