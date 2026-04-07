//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/profiler.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/chrono.hpp"
#include "duckdb/common/helper.hpp"

#ifndef _WIN32
#include <time.h>
#endif

namespace duckdb {

//! The profiler can be used to measure elapsed time
template <typename T>
class BaseProfiler {
public:
	//! Starts the timer
	void Start() {
		finished = false;
		start = Tick();
	}
	//! Finishes timing
	void End() {
		end = Tick();
		finished = true;
	}

	//! Returns the elapsed time in seconds. If End() has been called, returns
	//! the total elapsed time. Otherwise returns how far along the timer is
	//! right now.
	double Elapsed() const {
		auto measured_end = finished ? end : Tick();
		return std::chrono::duration_cast<std::chrono::duration<double>>(measured_end - start).count();
	}

private:
	time_point<T> Tick() const {
		return T::now();
	}
	time_point<T> start;
	time_point<T> end;
	bool finished = false;
};

using Profiler = BaseProfiler<steady_clock>;

//! Measures actual CPU time consumed by the calling thread (user + system),
//! excluding time spent sleeping or blocked. Uses POSIX clock_gettime with
//! CLOCK_THREAD_CPUTIME_ID. Falls back to wall-clock on non-POSIX systems.
class ThreadCPUProfiler {
public:
	void Start() {
		finished = false;
#ifndef _WIN32
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
#else
		wall.Start();
#endif
	}

	void End() {
		finished = true;
#ifndef _WIN32
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
#else
		wall.End();
#endif
	}

	//! Returns elapsed thread CPU time in seconds.
	double Elapsed() const {
#ifndef _WIN32
		auto measured_end = finished ? end : ([]() {
			struct timespec now;
			clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
			return now;
		})();
		return static_cast<double>(measured_end.tv_sec - start.tv_sec) +
		       static_cast<double>(measured_end.tv_nsec - start.tv_nsec) / 1e9;
#else
		return wall.Elapsed();
#endif
	}

private:
#ifndef _WIN32
	struct timespec start = {};
	struct timespec end = {};
#else
	Profiler wall;
#endif
	bool finished = false;
};

} // namespace duckdb
