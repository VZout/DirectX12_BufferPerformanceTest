#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>
#include <fstream>

namespace profiler
{

	using Precision = long double;
	using Duration = std::chrono::duration<Precision, std::milli>;
	using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

	struct Result
	{
		std::vector<TimePoint> start;
		std::vector<TimePoint> end;
	};

	extern std::map<std::string, Result> cpu_results;

	static TimePoint Now()
	{
		return std::chrono::high_resolution_clock::now();
	}

	static void StartCPU(std::string name)
	{
		auto it =  cpu_results.find(name);

		if (it == cpu_results.end())
			cpu_results[name] = Result();

		cpu_results[name].start.push_back(Now());
	}

	static void EndCPU(std::string name)
	{
		cpu_results[name].end.push_back(Now());
	}

	static void PrintResult(std::string name)
	{
		auto it = cpu_results.find(name);
		if (it == cpu_results.end())
			throw "Can't print a result that doesn't exist";

		Precision sum = 0;
		unsigned int num_results = it->second.end.size();
		for (auto i = 0; i < num_results; i++) {
			Duration diff = it->second.end[i] - it->second.start[i];
			sum += diff.count();
		}
		Precision average = (Precision)sum / (Precision)num_results;

		std::ofstream file;
		file.open("perf_" + name + ".txt");
		file << name << ":\n";
		file << "\tSamples: " << num_results << "ms\n";
		file << "\tAverage: " << average << "ms\n";
		file.close();
	}

} /* profiler */

#define ENABLE_PROFILER

#ifdef ENABLE_PROFILER
#define PROFILER_BEGIN_CPU(name) profiler::StartCPU(name);
#define PROFILER_END_CPU(name) profiler::EndCPU(name);
#else
#define PROFILER_BEGIN_CPU()
#define PROFILER_END_CPU()
#endif