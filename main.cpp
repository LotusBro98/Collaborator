#include <sched.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <iostream>
#include <thread>

int cpus[4]= {0, 2, 1, 3};

int child(int n)
{	
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(cpus[n % 4], &set);

	sched_setaffinity(0, sizeof(set), &set);

	long int max = 0x10000000;
	for (long int i = 0; i < max; i++)
		n++;

	std::cout << n << std::endl;
}

int main(int argc, char* argv[])
{
	int N = 1;
	if (argc == 2)
		N = atoi(argv[1]);

	std::thread ** threads = new std::thread*[N];

	for (int i = 0; i < N; i++)
		threads[i] = new std::thread(child, i);

	for (int i = 0; i < N; i++)
		threads[i]->join();

	return 0;
}
