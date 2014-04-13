all: tx

tx: tx.c
	gcc -Wall -Wextra -lbladerf -o tx tx.c
