/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef BUS_H_
#define BUS_H_

#include "Engine.H"
#include "Error.H"
#include "MemoryHierarchy.H"
#include "Statistics.H"
#include "Types.H"



class Bus : public IEventHandler{
	string name;
	string desc;
	Engine *engine;
	uint64 debugStart;

	uint64 latency;

	map<uint64, IBusCallback*> queue; //this must be a map

	map<uint64, uint64> gaps; //this must be a map

public:
	Bus(const string& nameArg,
			const string& descArg,
			Engine *engineArg,
			StatContainer *statCont,
			uint64 debugStartArg,
			uint64 latencyArg);
	~Bus();
	uint64 schedule(uint64 delay, IBusCallback *caller);
	void process(const Event *event);

	uint64 getLatency() const {return latency;}
	const char* getName() const {return name.c_str();}
private:
	void clearGaps();
};

#endif /* BUS_H_ */
