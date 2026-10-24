# Public commands. Compiler/library rules live in Makefile.eeglobal.
SHELL := /bin/sh
.DEFAULT_GOAL := all
REGION ?= all
LIBRARY ?= none
REGIONS := $(if $(filter all,$(REGION)),ntscu pal ntscj ntsck,$(REGION))
export LC_ALL := C

.PHONY: all modules module catalog release check-release new-module clean clean-release test _region _assets
all modules module: catalog
	+@set -eu; for region in $(REGIONS); do $(MAKE) --no-print-directory _region REGION=$$region REQUESTED_REGION=$(REGION); done
	$(MAKE) --no-print-directory _assets

# Build metadata comes directly from each module's Makefile.defs.
catalog:
	@mkdir -p build
	+@set -eu; for defs in modules/*/Makefile.defs; do \
	  test -f "$$defs" || continue; \
	  $(MAKE) --no-print-directory -s -f Makefile.eeglobal MODULE=$${defs%/Makefile.defs} describe; \
	done > build/catalog.new
	@mv build/catalog.new build/catalog.txt

# Every module uses the same link base. The runtime chooses its actual address.
_region:
	+@set -eu; work=build/$(REGION); mkdir -p "$$work/publish"; \
	if test -z '$(NAME)'; then \
	  $(MAKE) --no-print-directory -f Makefile.eeglobal REGION=$(REGION) image; \
	  mkdir -p build/dist; cp "$$work/publish/loader.pnach" build/dist/mod-loader.$(REGION).pnach; \
	fi; \
	while IFS='|' read -r folder name lib mode supported objects; do \
	  test -z '$(NAME)' || test "$$folder" = 'modules/$(NAME)' || continue; \
	  case " $$supported " in *" $(REGION) "*) ;; *) continue;; esac; \
	  $(MAKE) --no-print-directory -f Makefile.eeglobal REGION=$(REGION) MODULE="$$folder" IMAGE="$$name" image; \
	  mkdir -p hostfs/dl/mods build/dist/hostfs/dl/mods; \
	  cp "$$work/publish/$$name.$(REGION).bin" hostfs/dl/mods/; \
	  cp "$$work/publish/$$name.$(REGION).bin" build/dist/hostfs/dl/mods/; \
	done < build/catalog.txt
_assets: build/mapscan/mapscan.irx
	@mkdir -p hostfs/dl/mods/utils build/dist/hostfs/dl/mods/utils build/dist/hostfs/dl/mods/debug build/dist/hostfs/dl/config
	cp $< hostfs/dl/mods/utils/
	cp $< build/dist/hostfs/dl/mods/utils/
	@set -eu; dest=build/dist/hostfs/dl; \
	for list in modules.txt config/modules.txt mods/debug/exceptionhandler.bin; do \
	  if test -f "hostfs/dl/$$list"; then cp "hostfs/dl/$$list" "$$dest/$$list"; else rm -f "$$dest/$$list"; fi; \
	done; \
	cp hostfs/dl/maploader-online.txt "$$dest/"; \
	for asset in hostfs/dl/*.version hostfs/dl/*.wad hostfs/dl/*.world hostfs/dl/*.sound hostfs/dl/*.map hostfs/dl/*.bg hostfs/dl/*.thumb hostfs/dl/*.code; do \
	  test ! -f "$$asset" || cp "$$asset" "$$dest/"; \
	done

build/mapscan/mapscan.irx: $(wildcard iop/mapscan/*)
	mkdir -p build/mapscan
	cp -R iop/mapscan/. build/mapscan/
	$(MAKE) -C build/mapscan

release:
	$(MAKE) all REGION=all NAME=
	sh tools/release.sh
check-release:
	sh tools/release.sh check
clean-release:
	sh tools/release.sh clean
new-module:
	sh tools/new-module.sh "$(NAME)" "$(LIBRARY)"
clean:
	@case '$(NAME)' in *[!a-z0-9_-]*) echo 'Invalid module name' >&2; exit 1;; esac
ifeq ($(strip $(NAME)),)
	rm -rf build
	rm -f hostfs/dl/mods/*.ntscu.bin hostfs/dl/mods/*.pal.bin hostfs/dl/mods/*.ntscj.bin
else
	$(MAKE) --no-print-directory -f Makefile.eeglobal MODULE=modules/$(NAME) clean-module
endif

test:
	$(MAKE) -f tests/Makefile
