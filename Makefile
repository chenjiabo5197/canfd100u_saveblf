all: clean test
test:
	$(CC) -o saveblf saveblf.c -L. -L.. -lpthread -lusbcanfd
clean:
	rm -vf saveblf
