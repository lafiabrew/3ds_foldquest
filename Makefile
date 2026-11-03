# Shared-runtime bootstrap signal; NOT completed app build targets.
.PHONY: journal-check test 3ds verify-3ds
journal-check:
	mkdir -p build/host
	gcc -std=c11 -Wall -Wextra -Werror -O2 -g -fsanitize=address,undefined -Ivendor/lafiabrew_journal/include vendor/lafiabrew_journal/src/native_log_store.c vendor/lafiabrew_journal/src/native_fs_host.c tests/journal_smoke.c -o build/host/journal_smoke
	./build/host/journal_smoke
test: journal-check
	@echo 'FAIL: app behavior/native integration not implemented yet; runtime smoke is not app acceptance' >&2
	@exit 1
3ds verify-3ds:
	@echo 'FAIL: genuine app native artifacts are not implemented yet' >&2
	@exit 1
