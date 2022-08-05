#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

.PHONY: build
build:
	idf.py build

.PHONY: ota
ota: build
	curl -X POST --data-binary @build/wifi-blinky.bin http://192.168.178.91/ota > /dev/null

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
