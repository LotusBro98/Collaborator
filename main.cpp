#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sched.h>
#include <pthread.h>

#include <iostream>
#include <thread>
#include <mutex>
#include <cmath>
#include <iomanip>

#include <errno.h>

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

		push(Seg{from, to});
	}


	friend std::ostream& operator << (std::ostream & os, Task * task)
	{
		os << std::setprecision(8) << task->I << "; " << std::setprecision(2);
		for (int i = task->head; i != task->tail; i = (i + 1) % task->size)
			os << "[" << task->queue[i].from << ", " << task->queue[i].to << "] ";
		os << std::endl;

		return os;
	}

	void printProgress()
	{
		lock.lock();
		if (firstPrint)
			firstPrint = false;
		else
			printf("\033M");
		printf("%d%%\n", (int)(done * 100));
		lock.unlock();
	}

	bool commit(Seg seg, double s, double S)
	{
		lock.lock();
		taken--;
		if (S - s < eps * (seg.to - seg.from) / (to - from))
		{
			I += (s + S) / 2;
			done += (seg.to - seg.from) / (to - from);
			
			lock.unlock();
			waiting.unlock();
			return true;
		}
		else
		{
			splitAndPush(seg);
			lock.unlock();
			waiting.unlock();
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

	double getI()
	{
		return I;
	}

	friend void threadFunc(Task * task, int thr, int nThr);



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

	inline bool isEmpty()
	{
		return (head == tail);
	}

	inline bool isFull()
	{
		return ((tail + 1) % size == head);
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



	int head;
	int tail;
	int taken;
	static const int size = 0x100;
	std::mutex lock;
	std::mutex waiting;

	bool firstPrint = true;

	double I;
	double done;

	double (*f)(double x);
	double eps;
	double from;
	double to;
	
	Seg queue[size];
};

void calcSums(double (*f)(double x), double from, double to, double * s_out, double * S_out)
{
	const int nSeg = 0x1000;
	const int nSubSeg = 0x100;

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
		double k = Df / Dx;

		double fx = 0;
		double fx_dx;

		for (int j = 1; j <= nSubSeg; j++)
		{
			double x = x0 + j * dx;	
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

	*s_out = s;
	*S_out = S;
}

int cpus[4] = {0, 1, 2, 3};



void threadFunc(Task * task, int thr, int nThr)
{
	double s = 0;
	double S = 0;

	int cpu = cpus[thr % 4];

	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	
	pthread_setaffinity_np(pthread_self(), sizeof(set), &set);

	Task::Seg seg;

	int i = 0;

	while (task->take(&seg))
	{
		calcSums(task->f, seg.from, seg.to, &s, &S);
		task->commit(seg, s, S);
		i++;
		if (i % 1 == 0)
			task->printProgress();
	}
	task->waiting.unlock();

	printf("thread %d: %d iterations\n", thr, i);
}

double integrate(Task * task, int nThreads)
{
	std::thread ** threads = new std::thread*[nThreads];

	for (int i = 0; i < nThreads; i++)
		threads[i] = new std::thread(threadFunc, task, i, nThreads);

	for (int i = 0; i < nThreads; i++)
	{
		threads[i]->join();
		delete threads[i];
	}

	delete [] threads;

	return task->getI();
}




inline double func(double x)
{
	return std::sin(x);
}




int main(int argc, char* argv[])
{
	int N = 1;
	if (argc == 2)
		N = atoi(argv[1]);

	double eps = 1e-13;
	double I;
	
	double from = 0;
	double to = 3 * 3.141592653;

	Task * task = new Task(func, from, to, eps);

	I = integrate(task, N);

/*
	double s, S;
	calcSums(func, from, to, &s, &S);
	I = (s + S) / 2;
*/

	std::cout << "I = " << I << std::endl;

	return 0;
}
