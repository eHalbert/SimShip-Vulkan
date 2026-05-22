// Copyright (C) 2012 - All Rights Reserved
// All rights reserved. http://www.equals-forty-two.com

#pragma once

// WIN
#include <iomanip>
#include <iostream>
#include <Windows.h>
#include <deque>
#include <chrono>
using namespace std;

namespace eh
{
	class Timer
	{
	public:

		// 'running' is initially false.  A timer needs to be explicitly started
		// using 'start' or 'restart'
		Timer();

		/// Check if the timer is running
		bool isRunning() const { return mRunning; }

		/// Return the elapsed seconds
		double getTime() const;

		/// Get the elapsed time on the last batch (start/stop)
		double getElapsedTime() const;
		
		/// Init the timer
		void reset();

		/// Start the timer
		void start();

		/// Restart the timer
		void restart();

		/// Stop the timer
		void stop();

		/// Display the timer state.
		void check(std::ostream& os = std::cout);

	private:

		friend std::ostream& operator<<(std::ostream& os, const Timer& t);

		// Data members

		bool     mRunning;
		double   mAccumulatedTime; ///> Accumulated time in seconds

		LARGE_INTEGER  mFrequency;
		LARGE_INTEGER  mStartTime;    ///< Start time of the timer.
	};

	class DelayTimer : private Timer
	{
	public:

		DelayTimer();

		DelayTimer(double delay);

		void set(double delay);

		bool check() const;

	private:

		double mDelay;
	};
}
class sChrono
{
public:
    string name;
    eh::Timer   _chrono;

    // System for averaging FPS over 1 second
    vector<double> frameTimes;  // All frame durations within the second
    chrono::steady_clock::time_point secondStart;
    bool secondInitialized = false;
    int result = 0;

    void NameAndStart(std::string _name)
    {
        name = _name;
        _chrono.restart();  // Reset + start en 1 appel
    }

    void Stop()
    {
        _chrono.stop();

        // Record the duration of THIS frame
        double frameDuration = _chrono.getElapsedTime();
        frameTimes.push_back(frameDuration);

        // Initialize the first second if not done
        if (!secondInitialized)
        {
            secondStart = chrono::steady_clock::now();
            secondInitialized = true;
        }

        // Check if 1 second has elapsed
        auto now = chrono::steady_clock::now();
        double elapsedSecond = chrono::duration<double>(now - secondStart).count();

        if (elapsedSecond >= 1.0)   // Compute and store the result
        {
            // RESET : new second
            secondStart = now;
            frameTimes.clear();
            frameTimes.push_back(frameDuration);
        
            double sum = 0.0;
            for (double t : frameTimes)
                sum += t;

            double averageSec = sum / frameTimes.size();
            result = int(1e6 * averageSec);  // us
        }
    }

    int GetMicroSecondes()
    {
        return result;
    }

    void Print()
    {
        int ns = GetMicroSecondes();
        cout << name << ": " << ns << " \xc2\xb5s (" << frameTimes.size() << " frames)" << endl;
    }
};