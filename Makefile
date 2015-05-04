all: server

server: Server.cpp record.h
	c++ -g -lpthread -o server Server.cpp



