# Two targets the engine's own Makefile has no reason to carry.
#
# Its archive rule names every object on one command line, which on Windows is
# longer than a shell is given -- 171 objects is about nine kilobytes and the
# limit is eight. Nothing here needs the archive: the shared object is linked
# from the objects themselves, which is what unpacking the archive with
# --whole-archive would have amounted to anyway.
#
# Used as `make -f Makefile -f native/android.mk', so everything below is read
# after the engine's own definitions and inherits all of them.

.PHONY: android-objects android-objdir

android-objects: $(OBJECTS)

android-objdir:
	@echo $(OBJDIR)

# The same pair for a desktop Windows build, which is how a fault gets pinned
# on this port rather than on the engine: the same text spoken by the engine's
# own front end, built by the mingw on this machine.
.PHONY: desktop-objects desktop-objdir

desktop-objects: $(OBJECTSWIN)

desktop-objdir:
	@echo $(OBJDIRWIN)
