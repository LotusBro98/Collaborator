CPPFLAGS=-pthread

all: main

test: main
	sudo perf stat -e cpu-cycles,task-clock ./main 1
	sudo perf stat -e cpu-cycles,task-clock ./main 2

clean:
	rm main
