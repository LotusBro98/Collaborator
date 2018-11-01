#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <iostream>
#include <thread>
#include <mutex>
#include <cmath>

void calcSums(double (*f)(double x), double from, double to, double dx, double * s_out, double * S_out)
{
	const int nSeg = 0x100000;

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

void threadFunc(double (*f)(double x), double from, double to, double eps, double * I, std::mutex * Ilock)
{
	double s;
	double S;

	double dx = 1e-5;
	
	do
	{
		S = s = 0;

		calcSums(f, from, to, dx, &s, &S);
		dx /= 2;
	}
	while (S - s > eps);

	Ilock->lock();

	*I += (S + s) / 2;

	Ilock->unlock();
}

int main(int argc, char* argv[])
{
	int N = 1;
	if (argc == 2)
		N = atoi(argv[1]);

	double eps = 1e-13;
	double I;
	
	double from = -3.141592653 * 3;
	double to = 3.141592653 * 3;

	double (*func)(double x) = std::sin;

	double Dx = (to - from) / N;

	std::thread ** threads = new std::thread*[N];
	std::mutex Ilock;

	for (int i = 0; i < N; i++)
		threads[i] = new std::thread(threadFunc, static_cast<double (*)(double)>(std::sin), from + i * Dx, to + i * Dx, eps / N, &I, &Ilock);

	for (int i = 0; i < N; i++)
		threads[i]->join();
	

	std::cout << I << std::endl;

	return 0;
}
