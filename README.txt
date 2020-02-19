Compilation instructions:
	server:
		gcc -pthread -o server server.c vec.o -lrt
	array (unitl it's changed into static library):
		gcc -o array array.c -lrt
