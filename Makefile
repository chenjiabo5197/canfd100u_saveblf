all: clean test
test:
	$(CC) -o saveblf saveblf.c -L. -L.. -lpthread -lusbcanfd -lbinlog
clean:
	rm -vf saveblf
