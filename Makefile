all: clean test
test:
	$(CC) -o test test.c -L. -L.. -lpthread -lusbcanfd
clean:
	rm -vf test
