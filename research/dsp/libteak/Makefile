# SPDX-License-Identifier: CC0-1.0
#
# SPDX-FileContributor: Antonio Niño Díaz, 2023

# Tools
# -----

CP		:= cp
INSTALL		:= install
MAKE		:= make
RM		:= rm -rf

# Verbose flag
# ------------

ifeq ($(VERBOSE),1)
V		:=
else
V		:= @
endif

# Targets
# -------

.PHONY: all clean docs install teak

all: teak

teak:
	@+$(MAKE) -f Makefile.teak --no-print-directory
	@+$(MAKE) -f Makefile.teak --no-print-directory DEBUG=1

clean:
	@echo "  CLEAN"
	@$(RM) lib build

docs:
	@echo "  DOXYGEN"
	@doxygen Doxyfile

INSTALLDIR	?= /opt/blocksds/core/libs/libteak
INSTALLDIR_ABS	:= $(abspath $(INSTALLDIR))

install: all
	@echo "  INSTALL $(INSTALLDIR_ABS)"
	@test $(INSTALLDIR_ABS)
	$(V)$(RM) $(INSTALLDIR_ABS)
	$(V)$(INSTALL) -d $(INSTALLDIR_ABS)
	$(V)$(CP) -r include lib licenses teak.ld $(INSTALLDIR_ABS)
