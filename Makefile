CPPFLAGS=-pthread
SHELL=bash

all: main

test: main
#	sudo perf stat -e cpu-cycles,task-clock ./main 1
	@for i in 1 2 3 4 5 6 12 120; do \
		TIME=`/usr/bin/time -o >(cat) -f "%e" ./main $$i > /dev/null`; \
		echo -e "$$i: \t$$TIME"; \
	done

clean:
	rm main
