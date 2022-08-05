#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

.PHONY: build
build:
	idf.py build

.PHONY: ota
ota: build
	curl -o .upload.tmp -X PUT --data-binary @build/wifi-blinky.bin http://172.16.1.125/ota && cat .upload.tmp && rm .upload.tmp

.PHONY: lint
lint:
	cppcheck \
		--platform=unix32 \
		--template=gcc \
		--enable=warning,style \
		--inconclusive \
		main/ \
		> /dev/null
	clang-format -i -style webkit \
		main/*.h \
		main/*.c
