CFLAGS = -Wall -Werror -g
CC = gcc $(CFLAGS)

all: shell run_terminal_session

shell: shell.c string_vector.o shell_funcs.o shell_funcs_helper.o
	$(CC) -o $@ $^

string_vector.o: string_vector.h string_vector.c
	$(CC) -c string_vector.c

shell_funcs.o: string_vector.o shell_funcs.c
	$(CC) -c shell_funcs.c

run_terminal_session: run_terminal_session.c
	$(CC) -o $@ $^ -lutil

clean:
	rm -f string_vector.o shell_funcs.o shell run_terminal_session

test-setup:
	@chmod u+x testy

test: test-setup shell run_terminal_session
	./testy test_shell.org $(testnum)

clean-tests:
	rm -rf test-results

zip:
	@echo "ERROR: You cannot run 'make zip' from the part2 subdirectory. Change to the main proj3-code directory and run 'make zip' there."
