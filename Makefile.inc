include $(srcdir)/sim/Makefile.inc
include $(srcdir)/arch/Makefile.inc
include $(srcdir)/cli/Makefile.inc
include $(srcdir)/Makefile.cacti.inc

bin_PROGRAMS += mgsim
dist_man1_MANS += mgsim.1

mgsim_SOURCES = $(SIM_SOURCES) $(ARCH_SOURCES) $(CLI_SOURCES)
mgsim_CPPFLAGS = -I$(srcdir) $(SIM_EXTRA_CPPFLAGS) $(ARCH_EXTRA_CPPFLAGS) $(CLI_EXTRA_CPPFLAGS) $(AM_CPPFLAGS)  -DMGSIM_CONFIG_PATH=\"$(pkgdatadir)/config.ini\"

BASE_CXXFLAGS = $(SIM_EXTRA_CXXFLAGS) $(ARCH_EXTRA_CXXFLAGS) $(CLI_EXTRA_CXXFLAGS) $(AM_CXXFLAGS)
mgsim_CXXFLAGS = $(BASE_CXXFLAGS) $(WARN_CXXFLAGS)
mgsim_LDADD = 

if ENABLE_CACTI
BASE_CXXFLAGS += $(PTHREAD_CFLAGS)
noinst_LIBRARIES = libmgsimcacti.a
nodist_libmgsimcacti_a_SOURCES = $(CACTI_SOURCES)
libmgsimcacti_a_CPPFLAGS = $(mgsim_CPPFLAGS) $(CACTI_EXTRA_CPPFLAGS) 
libmgsimcacti_a_CXXFLAGS = $(BASE_CXXFLAGS) # No warnings
mgsim_CPPFLAGS += -DENABLE_CACTI=1
mgsim_CXXFLAGS += $(CACTI_EXTRA_CXXFLAGS)
mgsim_LDADD += libmgsimcacti.a $(PTHREAD_LIBS)
endif

if ENABLE_SDL
mgsim_CPPFLAGS += -DUSE_SDL=1
mgsim_CXXFLAGS += $(SDL_CFLAGS)
mgsim_LDADD += $(SDL_LIBS)
endif

## 
## Manual page rules
##
mgsim.1: mgsim
	$(AM_V_GEN)$(HELP2MAN) --no-discard-stderr -N --output=$@.tmp ./mgsim
	$(AM_V_at)if ! (test -r $(srcdir)/$@ && cmp $@.tmp $(srcdir)/$@) >/dev/null 2>&1; then mv -f $@.tmp $@; else rm -f $@.tmp; fi

MAINTAINERCLEANFILES += $(dist_man1_MANS)
