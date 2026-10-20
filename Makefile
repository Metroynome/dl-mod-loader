PYTHON ?= python3
REGION ?= all
LIBRARY ?= none
.PHONY: all release modules new-module check test
all release modules:
	$(PYTHON) tools/build.py build --region $(REGION)
new-module:
	$(PYTHON) tools/build.py new "$(NAME)" --library "$(LIBRARY)"
check:
	$(PYTHON) tools/build.py check
test:
	mkdir -p build
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py'
	$(CC) -std=gnu99 -fno-builtin -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -I. tests/runtime.c strings.c -o build/runtime-test
	./build/runtime-test
	@set -e; for region in 1 2 3 4; do \
	  $(CC) -std=gnu99 -Wno-int-to-pointer-cast -DDL_MODULE_REGION=$$region -I. tests/platform.c -o build/platform-test-$$region; \
	  ./build/platform-test-$$region; \
	done
