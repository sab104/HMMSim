/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef ENGINE_H_
#define ENGINE_H_

#include "Statistics.H"
#include "Types.H"

#include <sys/time.h>

#include <queue>
#include <map>
#include <unordered_map>
#include <fstream>

using namespace std;

class Event;

class IEventHandler{
public:
	virtual void process(const Event * event) = 0;
	virtual ~IEventHandler() {}
};


struct Event{
	IEventHandler *handler;
	uintptr_t data;

	Event(IEventHandler *handlerArg, uint64 dataArg) : handler(handlerArg), data(dataArg){}
	~Event(){}
	void execute() const {handler->process(this);}
	uint64 getData() const {return data;}
};

class Engine : public IEventHandler{
private:
	map<uint64, vector<Event> > events;

	static const uint64 currentSize = 4;
	vector<Event> currentEvents[currentSize];

	StatContainer *stats;
	uint64 statsPeriod;
	uint64 progressPeriod;

	uint64 currentInterval;

	uint64 statsNextEvent;
	uint64 progressNextEvent;

	ofstream statsOut;

	bool done;
	unsigned curIndex;
	uint64 timestamp;
	uint64 lastTimestamp;
	uint64 numEvents;
	uint64 lastNumEvents;

	struct timeval start;
	struct timeval end;
	struct timeval last;


	//Statistics
	CalcStat<uint64, Engine> finalTimestamp;
	uint64 getFinalTimestamp() {return timestamp;}

	Stat<uint64> totalEvents;
	Stat<double> executionTime;
	BinaryStat<double, divides<double>, uint64, double> eventRate;


public:
	Engine(StatContainer *statsArg, uint64 statsPeriodArg, const string& statsFilename, uint64 progressPeriodArg);
	void run();
	void quit();
	void addEvent(uint64 delay, IEventHandler *handler, addrint addr = 0);

	uint64 getTimestamp() const {return timestamp;}
	uint64 getNumEvents() const {return numEvents;}

	bool currentEventsEmpty();

	void process(const Event * event);

private:
	void updateStats();

};

#endif /* ENGINE_H_ */
