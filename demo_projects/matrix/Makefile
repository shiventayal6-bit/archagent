all:
	gcc -O2 -o bench_matrix matrix.c bench_matrix.c

bench: all
	./bench_matrix

test: all
	./bench_matrix

clean:
	rm -f bench_matrix
