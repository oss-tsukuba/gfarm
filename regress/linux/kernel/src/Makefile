TARGETS = readwrite truncate flock mmap benchmark
all: $(TARGETS)

readwrite: readwrite.c
	gcc -Wall -o readwrite readwrite.c

truncate: truncate.c
	gcc -Wall -o truncate truncate.c

flock: flock.c
	gcc -Wall -o flock flock.c

mmap: mmap.c
	gcc -Wall -o mmap mmap.c

benchmark: benchmark.c
	gcc -Wall -o benchmark benchmark.c

clean:
	rm -f a.out $(TARGETS)
