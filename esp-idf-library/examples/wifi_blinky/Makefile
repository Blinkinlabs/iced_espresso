#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

.PHONY: build
build:
	idf.py build


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
