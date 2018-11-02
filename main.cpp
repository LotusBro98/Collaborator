#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sched.h>
#include <pthread.h>

#include <iostream>
#include <thread>
#include <mutex>
#include <cmath>

inline double func(double x)
{
	return x * x * x / 4;
}

void calcSums(double (*f)(double x), double from, double to, double dx, int nSeg, double * s_out, double * S_out)
{
	//const int nSeg = 0x1000000;

	double s = 0;
	double S = 0;

	double Dx = (to - from) / nSeg;

	for (int i = 0; i < nSeg; i++)
	{
		double x0 = from + i * Dx;
		double x1 = x0 + Dx;
		double f0 = f(x0);
		double f1 = f(x1);
		double Df = f1 - f0;
		double k = Df / Dx;

		double fx = 0;
		double fx_dx;

		for (double x = x0 + dx; x < x1; x += dx)
		{
			fx_dx = fx;
			fx = f(x) - f0 - k * (x - x0);

			if (fx > fx_dx) {
				s += fx_dx * dx;
				S += fx * dx;
			}  else {
				s += fx * dx;
				S += fx_dx * dx;
			}
		}

		s += Dx * (f0 + f1) / 2;
		S += Dx * (f0 + f1) / 2;
	}

	*s_out += s;
	*S_out += S;
}

int cpus[4] = {0, 1, 2, 3};

void threadFunc(double (*f)(double x), double from, double to, double dx, int nSeg, double * s_out, double * S_out, std::mutex * sumLock, int thr, int nThr)
{
	double s = 0;
	double S = 0;

	double Dx = (to - from) / nThr;

	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(cpus[thr % 4], &set);

	pthread_setaffinity_np(pthread_self(), sizeof(set), &set);

	calcSums(f, from + thr * Dx, from + (thr + 1) * Dx, dx, nSeg / nThr, &s, &S);

	sumLock->lock();

	*s_out += s;
	*S_out += S;

	sumLock->unlock();
}

int main(int argc, char* argv[])
{
	int N = 1;
	if (argc == 2)
		N = atoi(argv[1]);

	double eps = 1e-13;
	double I;
	
	double from = 0;
	double to = 1;
	
	const int nSeg = 0x1000000;

	double (*func)(double x) = std::sin;

	double Dx = (to - from) / N;

	std::thread ** threads = new std::thread*[N];
	std::mutex Ilock;
	
	double s, S;
	double dx = 1e-7;

	do
	{
		for (int i = 0; i < N; i++)
			threads[i] = new std::thread(threadFunc, func, from, to, dx, nSeg, &s, &S, &Ilock, i, N);

		for (int i = 0; i < N; i++)
			threads[i]->join();
	}
	while (S - s > eps);


	I = (s + S) / 2;

	std::cout << I << std::endl;

	return 0;
}
