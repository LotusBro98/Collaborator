#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sched.h>
#include <pthread.h>

#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <cmath>
#include <iomanip>
#include <stdexcept>
#include <chrono>

#include <errno.h>

#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>

void calcSums(double (*f)(double x), double from, double to, double * s_out, double * S_out)
{
	const int nSeg = 0x1000;
	const int nSubSeg = 0x10;

	double Dx = (to - from) / nSeg;
	double dx = Dx / nSubSeg;

	double s = 0;
	double S = 0;


	for (int i = 0; i < nSeg; i++)
	{
		double x0 = from + i * Dx;
		double x1 = x0 + Dx;
		double f0 = f(x0);
		double f1 = f(x1);
		double Df = f1 - f0;
		double k = Df / nSubSeg;

		double gm = f((x0 + x1) / 2) - (f0 + f1) / 2;
		double a = 4 * gm / nSubSeg / nSubSeg;
		
		double fx = 0;
		double fx_dx;

		for (int j = 1; j <= nSubSeg; j++)
		{
			double x = x0 + j * dx;	
			fx_dx = fx;
			fx = f(x) - f0 - k * j - a * j * (j - nSubSeg);

			if ((fx > fx_dx) == (dx > 0)) {
				s += fx_dx * dx;
				S += fx * dx;
			}  else {
				s += fx * dx;
				S += fx_dx * dx;
			}
		}

		double DI = 0;
		DI += Dx * (f0 + f1) / 2;
		DI += a / dx / dx * Dx * Dx * Dx / 6;

		s += DI;
		S += DI;
	}

	*s_out = s;
	*S_out = S;
}

auto startTime = std::chrono::high_resolution_clock::now();

long long getUsec()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() -startTime).count();
}


class Task
{
public:

	struct Seg
	{
		double from;
		double to;
	};

	Task(double (*f)(double x), double from, double to, double eps)
	{
		head = 0;
		tail = 0;
		taken = 0;
		done = 0;
	
		this->f = f;
		this->eps = eps;

		this->I = 0;

		this->from = from;
		this->to = to;

		loadCpuData();

		push(Seg{from, to});
	}


	friend std::ostream& operator << (std::ostream & os, Task * task)
	{
		os << std::setprecision(8) << task->I << "; " << task->getStored() << "\n" << std::setprecision(8);
		for (int i = task->head; i != task->tail; i = (i + 1) % task->size)
			os << "\t[" << task->queue[i].from << ", " << task->queue[i].to << "]\n";
		os << std::endl;

		return os;
	}

	void printProgress()
	{
		lock.lock();
		fprintf(stderr, "%d%%\n", (int)(done * 100));
		fprintf(stderr, "\033M");
		lock.unlock();
	}

	bool commit(Seg seg, double s, double S)
	{
		lock.lock();
		taken--;
		if ((S - s) < eps * (seg.to - seg.from) / (to - from))
		{
			I += (s + S) / 2;
			done += (seg.to - seg.from) / (to - from);
			
			waiting.unlock();
			lock.unlock();
			return true;
		}
		else
		{
			if (!splitAndPush(seg))
			{
				findAdjacentAndMerge();
				if (!splitAndPush(seg))
				{
					std::cerr << this;
					lock.unlock();
					throw std::overflow_error("Segment queue overflow. Try specifieng higher eps.");
				}
			}
			
			waiting.unlock();
			lock.unlock();
			return false;
		}
	}

	bool take(Seg * seg)
	{
		check:

		lock.lock();

		if (pop(seg))
		{
			taken++;
			lock.unlock();
			return true;
		}
		else
		{
			if (taken > 0)
			{
				lock.unlock();
				waiting.lock();
				goto check;
			}
			else
			{
				lock.unlock();
				return false;
			}
		}
	}

	void loadCpuData()
	{
		FILE * lscpu;
		
		int maxCore = -1;
		int core;
		nCpus = 0;

		lscpu = popen("lscpu -p=core | tail +5", "r");
		while (fscanf(lscpu, "%d", &core) == 1)
		{
			nCpus++;
			if (core > maxCore)
				maxCore = core;
		}
		pclose(lscpu);
	
		int * cores = new int[nCpus];

		lscpu = popen("lscpu -p=core | tail +5", "r");
		for (int i = 0; i < nCpus; i++)
		{
			fscanf(lscpu, "%d", &(cores[i]));
		}
		pclose(lscpu);

		cpus = new int[nCpus];
		
		core = 0;
		for (int i = 0; i < nCpus;)
		{
			for (int j = 0; j < nCpus; j++)
			{
				if (cores[j] == core)
				{
					cpus[i] = j;
					cores[j] = -1;
					i++;
					break;
				}
			}
			core = (core + 1) % (maxCore + 1);
		}

		delete [] cores;

	}

	int getCpu(int thr)
	{
		return cpus[thr % nCpus];
	}

	int getStored()
	{
		return (tail - head + size) % size;
	}

	friend double integrate(double (*f)(double x), double from, double to, double eps, int nThreads);

private:

	bool push(Seg seg)
	{
		if ((tail + 1) % size == head)
			return false;

		queue[tail] = seg;
		tail = (tail + 1) % size;

		return true;
	}

	bool pop(Seg * seg)
	{
		if (head == tail)
			return false;

		*seg = queue[head];
		head = (head + 1) % size;

		return true;
	}

	bool splitAndPush(Seg seg)
	{
		if ((tail + 1) % size == head || (tail + 2) % size == head)
			return false;

		double from = seg.from;
		double to = seg.to;
		double middle = (from + to) / 2;

		Seg seg1 = Seg{from, middle};
		Seg seg2 = Seg{middle, to};

		queue[tail] = seg1;
		tail = (tail + 1) % size;

		queue[tail] = seg2;
		tail = (tail + 1) % size;
		
		return true;
	}

	bool findAdjacentAndMerge()
	{
		int attempt = 0;
		int successful = 0;
		for (; attempt < getStored(); attempt++)
		{
			Seg seg1;
			pop(&seg1);
			
			int i = head;
			for (; i != tail; i = (i + 1) % size)
			{
				if (seg1.from == queue[i].to)
				{
					queue[i].to = seg1.to;
					break;
				}
				else if (seg1.to == queue[i].from)
				{
					queue[i].from = seg1.from;
					break;
				}
			}

			if (i != tail)
				successful++;
			else
				push(seg1);
		}
		
		return successful > 0;
	}

	void threadIntegrate(int thr)
	{
		double s = 0;
		double S = 0;

		cpu_set_t set;
		CPU_ZERO(&set);
		CPU_SET(getCpu(thr), &set);
		pthread_setaffinity_np(pthread_self(), sizeof(set), &set);

		Task::Seg seg;

		long wasted = -getUsec();
		int iter  = 0;

		while (take(&seg))
		{
			wasted += getUsec();
			
			calcSums(f, seg.from, seg.to, &s, &S);
			
			wasted -= getUsec();
			commit(seg, s, S);
			printProgress();
			iter++;
		}
		waiting.unlock();
		wasted += getUsec();

		fprintf(stderr, "thread %d: %d iterations, %ld ms wasted\n", thr, iter, wasted / 1000);
	}


	int head;
	int tail;
	int taken;
	static const int size = 0x10000;
	std::mutex lock;
	std::mutex waiting;

	long double I;
	double done;

	double (*f)(double x);
	double eps;
	double from;
	double to;

	int * cpus;
	int nCpus;
	
	Seg queue[size];
};



double integrate(double (*f)(double x), double from, double to, double eps, int nThreads)
{
	Task * task = new Task(f, from, to, eps);

	std::thread ** threads = new std::thread*[nThreads];

	for (int i = 0; i < nThreads; i++)
	{
		threads[i] = new std::thread(&Task::threadIntegrate, task, i);
	}

	for (int i = 0; i < nThreads; i++)
	{
		threads[i]->join();
		delete threads[i];
	}

	delete [] threads;

	return task->I;
}





inline double func(double x)
{
	return std::sin(x);
}

int main(int argc, char* argv[])
{
	const char * usage = "Usage: ./main <n_threads> [<from> <to>] [eps]";

	if (argc != 2 && argc != 4 && argc != 5)
	{
		std::cerr << usage << std::endl;
		return 1;
	}
		
	int N = atoi(argv[1]);

	double eps = 1e-13;
	double I;
	
	double from = 0.5 * M_PI;
	double to = 9 * M_PI;

	if (argc > 2)
	{
		from = atof(argv[2]);
		to = atof(argv[3]);
	}

	if (argc > 4)
	{
		eps = std::atof(argv[4]);
		if (eps < 1e-13)
			throw std::underflow_error( "Too small eps for type double. Try specifying higher eps.");
	}

	if (errno != 0)
	{
		std::cerr << "Failed to parse arguments" << std::endl;
		std::cerr << usage << std::endl;
		return 1;
	}

	I = integrate(func, from, to, eps, N);

	std::cout << 
		std::fixed << std::setprecision(-std::log10(eps)) << "I = " << I << " +/- " << 
		std::scientific << std::setprecision(0) << eps << std::endl;

	return 0;
}
