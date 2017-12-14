/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef MIGRATION_H_
#define MIGRATION_H_


#include "Counter.H"
#include "Engine.H"
#include "Error.H"
#include "Statistics.H"
#include "Types.H"

#include <set>
#include <debug/list>

using namespace std;

enum AllocationPolicy {
	DRAM_FIRST,
	PCM_ONLY,
	CUSTOM
};

class IMigrationPolicy {
public:
	virtual PageType allocate(unsigned pid, addrint addr, bool read, bool instr) = 0;
	virtual bool migrate(unsigned pid, addrint addr) = 0;	//migrate on demand; returns whether page is a candidate
	virtual bool complete(unsigned *pid, addrint *addr) = 0;	//complete on demand migration; determine which page, if any, should have its migration completed
	virtual bool rollback(unsigned *pid, addrint *addr) = 0; //rollback on demand migration; determine which page, if any, should have its migration rolled back
	virtual bool demote(unsigned *pid, addrint *addr) = 0;	//demote DRAM page to PCM; determine which page, if any, should be evicted from DRAM
	virtual void done(unsigned pid, addrint addr) = 0;		//tells migration policy that a migration (or rollback) has finished
	virtual void monitor(vector<CountEntry>& counts, const vector<ProgressEntry>& progress) = 0;	//update monitoring information internal to the policy
	virtual void setNumDramPages(uint64 dramPagesNew) = 0;
	virtual void setInstrCounter(Counter* counter) = 0;
	virtual void finish() = 0;
	virtual ~IMigrationPolicy() {}
};

class BaseMigrationPolicy : public IMigrationPolicy {
protected:

	string name;
	Engine *engine;

	uint64 debugStart;

	Counter *instrCounter;

	uint64 dramPages;

	AllocationPolicy allocPolicy;

	unsigned numPids; 	//Number of processes served by this policy. Must be 1 or larger.
						//If equal to 1, the policy belongs to one process and 0 is used as the pid.
						//If greater than 1, the policy is shared accross processes and the pid passed as parameter in method calls is used.

	double maxFreeDram;

	int64 dramPagesLeft;
	int64 maxFreeDramPages;

	bool dramFull; //For use with DRAM_FIRST allocation policy only

	vector<ProgressEntry> progress;

public:
	BaseMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugStartArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		unsigned numPidsArg,
		double maxFreeDramArg);
	PageType allocate(unsigned pid, addrint addr, bool read, bool instr);
//	bool complete(unsigned *pid, addrint *addr);
//	bool rollback(unsigned *pid, addrint *addr);
	bool demote(unsigned *pid, addrint *addr);
	void monitor(vector<CountEntry>& counts, const vector<ProgressEntry>& progress);
	void setNumDramPages(uint64 dramPagesNew);
	void setInstrCounter(Counter* counter);
	void finish();
	virtual bool selectDemotionPage(unsigned *pid, addrint *addr) = 0;
};

class NoMigrationPolicy : public BaseMigrationPolicy {

public:
	NoMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugStartArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		unsigned numPidsArg);
	bool migrate(unsigned pid, addrint addr) {return false;}
	bool complete(unsigned *pid, addrint *addr) {return false;}
	bool rollback(unsigned *pid, addrint *addr) {return false;}
	void done(unsigned pid, addrint addr) {}
	void monitor(vector<CountEntry>& counts, const vector<ProgressEntry>& progress) {}
	bool selectDemotionPage(unsigned *pid, addrint *addr) {return false;}

};

class MultiQueueMigrationPolicy : public BaseMigrationPolicy {
	unsigned numQueues;
	int thresholdQueue;
	uint64 lifetime;
	bool logicalTime;
	uint64 filterThreshold;
	bool secondDemotionEviction;
	bool aging;
	bool useHistory;
	bool usePendingList;
	bool enableRollback;
	bool promotionFilter;
	unsigned demotionAttempts;
	uint64 rollbackExpiration;

	struct AccessEntry {
		unsigned pid;
		addrint addr;
		uint64 expirationTime;
		uint64 count;
		bool demoted;
		bool migrating;
		bool completing;
		uint64 startTime;
		AccessEntry(unsigned pidArg, addrint addrArg, uint64 expirationTimeArg, uint64 countArg, bool demotedArg, bool migratingArg, bool completingArg, uint64 startTimeArg) : pid(pidArg), addr(addrArg), expirationTime(expirationTimeArg), count(countArg), demoted(demotedArg), migrating(migratingArg), completing(completingArg), startTime(startTimeArg) {}
	};

	typedef list<AccessEntry> AccessQueue;

	struct PageEntry {
		PageType type;
		int queue;  //-1 means the victim list
					//-2 means this history list
		AccessQueue::iterator accessIt;
		PageEntry(PageType typeArg, int queueArg, const AccessQueue::iterator& accessItArg) : type(typeArg), queue(queueArg), accessIt(accessItArg) {}
	};

	//typedef map<addrint, PageEntry> PageMap;
	typedef unordered_map<addrint, PageEntry> PageMap;


	//2 queues: one for DRAM and one for PCM (index is type)
	vector<AccessQueue> queues[2];
	AccessQueue victims;
	AccessQueue history;
	vector<uint64> thresholds;

	PageMap *pages;

	typedef std::pair<unsigned, addrint> PidAddrPair;

	list<PidAddrPair> pending;

	uint64 currentTime;

	unsigned tries;

public:
	MultiQueueMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugStartArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		unsigned numPidsArg,
		double maxFreeDramArg,
		unsigned numQueuesArg,
		unsigned thresholdQueueArg,
		uint64 lifetimeArg,
		bool logicalTimeArg,
		uint64 filterThresholdArg,
		bool secondDemotionEvictionArg,
		bool agingArg,
		bool useHistoryArg,
		bool usePendingListArg,
		bool enableRollbackArg,
		bool promotionFilterArg,
		unsigned demotionAttemptsArg,
		uint64 rollbackExpirationArg);
	PageType allocate(unsigned pid, addrint addr, bool read, bool instr);
	bool migrate(unsigned pid, addrint addr);
	bool complete(unsigned *pid, addrint *addr);
	bool rollback(unsigned *pid, addrint *addr);
	void done(unsigned pid, addrint addr);
	void monitor(vector<CountEntry>& counts, const vector<ProgressEntry>& progress);
	bool selectDemotionPage(unsigned *pid, addrint *addr);
};

class NewPolicy : public BaseMigrationPolicy, public IEventHandler {
	//Algorithm: keep track of the number of blocks migrated on demand and the time since the start of
	//migration. Calculate the rate of blocks accessed (number of blocks migrated on demand
	//divided by time since start of migration).
	//Complete: choose page with the highest rate that has migrated at least some blocks.
	//Roll back: choose page with the lowest rate that has migrated at least some blocks.
	//Demote: choose least recently accessed page.

	//Possible improvement:when updating progress, choose whether to complete or rollback but not both.

	//Possible improvement: keep track of whether page was recently migrated on demand and then
	//demoted, and of its rate of blocks accessed. While deciding whether to migrate a page on demand,
	//compare its rate with the rate of recently fully migrated pages.

	//Parameters
	uint64 addedBlocksThreshold;
	uint64 rollbackThreshold;
	bool rollbackLogicalTime;
	unsigned numAgingPeriods;
	uint64 agingPeriod;
	bool agingLogicalTime;
	bool rollbackAsDemotion;

	//Data structures for algorithm

	bool finished;

	enum AccessListType {
		INDRAM,
		INPCM,
		PROMOTING,
		DEMOTING,
		SIZE
	};

	struct AccessEntry {
		unsigned pid;
		addrint page;
		uint64 count;
		AccessEntry(unsigned pidArg, addrint pageArg) : pid(pidArg), page(pageArg), count(0) {}
		AccessEntry(const AccessEntry& rhs) : pid(rhs.pid), page(rhs.page), count(rhs.count) {}
		bool operator<(const AccessEntry& rhs) const{
			return this->count < rhs.count;
		}
	};

	typedef list<AccessEntry> AccessList;

	struct LRUEntry {
		unsigned pid;
		addrint page;
		LRUEntry(unsigned pidArg, addrint pageArg) : pid(pidArg), page(pageArg) {}
		LRUEntry(const LRUEntry& entry) : pid(entry.pid), page(entry.page) {}
	};

	typedef list<LRUEntry> LRUList;

	struct CountMapEntry {
		unsigned pid;
		addrint page;
		bool inSort; //whether countIt refers to a entry in the sorted part of countList
		CountMapEntry(unsigned pidArg, addrint pageArg, bool inSortArg) : pid(pidArg), page(pageArg), inSort(inSortArg) {}
	};

	//This has to be a ordered multi map
	typedef multimap<uint64, CountMapEntry> CountMap;

	struct PageEntry {
		PageType type;
		bool promoting;
		bool completing;
		bool rollingBack;
		bool demoting;
		uint64 startTime;
		uint64 lastReadTime;
		uint64 countSinceStart;
		vector<uint64> periods;
		AccessListType accessType;
		AccessList::iterator accessIt;
		bool inLru;
		LRUList::iterator lruIt;
		CountMap::iterator countIt;
		PageEntry(
			PageType typeArg,
			unsigned numPeriodsArg,
			AccessListType accessTypeArg,
			const AccessList::iterator& accessItArg,
			bool inLruArg,
			const LRUList::iterator& lruItArg,
			const CountMap::iterator& countItArg) :
					type(typeArg),
					promoting(false),
					completing(false),
					rollingBack(false),
					demoting(false),
					lastReadTime(0),
					periods(numPeriodsArg),
					accessType(accessTypeArg),
					accessIt(accessItArg),
					inLru(inLruArg),
					lruIt(lruItArg),
					countIt(countItArg){
		}
	};

	//typedef map<addrint, PageEntry> PageMap;
	typedef unordered_map<addrint, PageEntry> PageMap;

	PageMap *pages;

	struct RollbackEntry {
		unsigned pid;
		addrint page;
		double rate;
		RollbackEntry(unsigned pidArg, addrint pageArg, double rateArg) : pid(pidArg), page(pageArg), rate(rateArg) {}
		bool operator<(const RollbackEntry& rhs) const{
			return this->rate < rhs.rate;
		}
	};

	typedef vector<RollbackEntry> RollbackList;

	RollbackList rollbackList; //list for pages considered for rollback

	unsigned currentPeriod;

	AccessList accessLists[AccessListType::SIZE];
	LRUList lruList;

	uint64 threshold;
	CountMap countMap;
	CountMap::iterator thresholdIt;

	uint64 currentTime;
	uint64 currentTimeInPeriod;

public:
	NewPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugStartArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		unsigned numPidsArg,
		double maxFreeDramArg,
		uint64 addedBlocksThresholdArg,
		uint64 rollbackThresholdArg,
		bool rollbackLogicalTimeArg,
		unsigned numAgingPeriodsArg,
		uint64 agingPeriodArg,
		bool agingLogicalTimeArg,
		bool rollbackAsDemotionArg);
	PageType allocate(unsigned pid, addrint addr, bool read, bool instr);
	bool migrate(unsigned pid, addrint addr);
	bool complete(unsigned *pid, addrint *addr);
	bool rollback(unsigned *pid, addrint *addr);
	void done(unsigned pid, addrint addr);
	void monitor(vector<CountEntry>& counts, const vector<ProgressEntry>& progress);
	bool selectDemotionPage(unsigned *pid, addrint *addr);

	void process(const Event * event);
	void finish();

private:
	void calculateThreshold();

};

class ThresholdMigrationPolicy: public BaseMigrationPolicy, public IEventHandler{
	//Algorithm: divide time (cycles, requests?) in intervals. Look at the previous N intervals, and count the number of times each
	//page is accessed. Possibly give more weight to more recent intervals (linear, exponential, ???). Order pages by count and
	//determine the cutoff value for the count (the value of count for the ith page, where i is the number of pages that fit in DRAM).
	//Promotion: if the page being accessed is in PCM and its count is higher than the cutoff, migrate it to DRAM.
	//Demotion: migrate the least accessed page in DRAM that has a count lower than the cutoff value.
	//Keep doing this until the end of the interval. At the start of the next interval, recompute and start over.

	//Parameters

	uint64 numIntervals;	//Number of intervals to look back
	uint64 intervalLength;	//Number of cycles each interval lasts



	//Data structures for algorithm


	struct PageEntry {
		const unsigned pid;
		PageType type;
		size_t countIndex;
		vector<int64> intervalCounts;
		size_t head; //points to current interval. head + 1 is tail (the oldest interval)
		bool migrating;
		bool completing;
		PageEntry(int pidArg, PageType typeArg, size_t countIndexArg, uint64 numIntervalsArg) : pid(pidArg), type(typeArg), countIndex(countIndexArg), intervalCounts(numIntervalsArg), head(0), migrating(false), completing(false) {}
	};

	//typedef map<addrint, PageEntry> PageMap;
	typedef unordered_map<addrint, PageEntry> PageMap;

	vector<PageMap> pages;

	struct PromotionEntry {
		unsigned pid;
		addrint addr;
		PromotionEntry(int pidArg, addrint addrArg) : pid(pidArg), addr(addrArg) {}
		bool operator==(const PromotionEntry& rhs) const {
			return this->pid == rhs.pid && this->addr == rhs.addr;
		}
	};

	vector<PromotionEntry> promotions;

	struct Count {
		PageMap::pointer entry;
		int64 count;
		uint64 lastAccess;
		Count(PageMap::pointer entryArg, int64 countArg, uint64 lastAccessArg) : entry(entryArg), count(countArg), lastAccess(lastAccessArg) {}
		bool operator>(const Count& rhs) const{
			return this->count > rhs.count;
		}
	};

	int64 pivot;
	vector<Count> counts;
	vector<Count> dramCounts;

	bool finished;

public:
	ThresholdMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugStartArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		unsigned numPidsArg,
		double maxFreeDramArg,
		unsigned numIntervalsArg,
		uint64 intervalLengthArg);

	PageType allocate(unsigned pid, addrint addr, bool read, bool instr);
	bool migrate(unsigned pid, addrint addr);
	bool complete(unsigned *pid, addrint *addr);
	bool rollback(unsigned *pid, addrint *addr) {return false;}
	void done(unsigned pid, addrint addr);
	void monitor(vector<CountEntry>& counts, const vector<ProgressEntry>& progress);
	bool selectDemotionPage(unsigned *pid, addrint *addr);

	void process(const Event * event);
	void finish();

	void updateCounts();
};


//Old Migration Policies:

class IAllocator {
public:
	virtual PageType hint(unsigned pid, addrint addr, bool read, bool instr) = 0;
	virtual ~IAllocator() {}
};

class IOldMigrationPolicy {
public:
	virtual void setInstrCounter(Counter* counter) = 0;
	virtual void monitor(unsigned pid, addrint addr) = 0;
	virtual PageType allocate(unsigned pid, addrint addr, bool read, bool instr) = 0;
	virtual bool migrate(unsigned *pid, addrint *addr) = 0;
	virtual void changeNumDramPages(uint64 dramPagesNew) = 0;
	virtual ~IOldMigrationPolicy() {}
};

class OldBaseMigrationPolicy : public IOldMigrationPolicy {
protected:

	string name;
	Engine *engine;

	uint64 debugStart;

	Counter *instrCounter;

	uint64 dramPages;

	AllocationPolicy allocPolicy;
	IAllocator *allocator;

	unsigned numPids; 	//Number of processes served by this policy. Must be 1 or larger.
						//If equal to 1, the policy belongs to one process and 0 is used as the pid.
						//If greater than 1, the policy is shared accross processes and the pid passed as parameter in method calls is used.

	int64 dramPagesLeft;

	bool dramFull; //For use with DRAM_FIRST allocation policy only

public:
	OldBaseMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugStartArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		IAllocator *allocatorArg,
		unsigned numPidsArg);
	void setInstrCounter(Counter* counter);
	PageType allocate(unsigned pid, addrint addr, bool read, bool instr);
	bool migrate(unsigned *pid, addrint *addr);
	void changeNumDramPages(uint64 dramPagesNew);
	virtual bool selectPage(unsigned *pid, addrint *addr) = 0;
};

class OldNoMigrationPolicy : public OldBaseMigrationPolicy {

public:
	OldNoMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugStartArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		IAllocator *allocatorArg,
		unsigned numPidsArg);
	void monitor(unsigned pid, addrint addr) {}
	bool selectPage(unsigned *pid, addrint *addr) {return false;}
};

class OldOfflineMigrationPolicy : public OldBaseMigrationPolicy {

	unsigned thisPid;

	enum MetricType {
		ACCESSED,
		ACCESS_COUNT,
		TOUCH_COUNT
	};

	MetricType metricType;

	enum AccessType {
		READS,
		WRITES,
		ACCESSES
	};

	AccessType accessType;

	enum WeightType {
		UNIFORM,
		LINEAR,
		EXPONENTIAL
	};

	WeightType weightType;

	uint64 intervalCount;

	vector<uint64> weights;

	uint64 metricThreshold; //minumum difference between 2 pages to consider swapping them

//	struct PageInfo {
//		uint32 reads;
//		uint32 writes;
//		uint8 readBlocks;
//		uint8 writtenBlocks;
//		uint8 accessedBlocks;
//	};


	uint64 period;

	uint64 previousInterval;

	struct Entry {
		uint32 interval;
		uint32 count;
		Entry(uint32 intervalArg, uint32 countArg) : interval(intervalArg), count(countArg) {}
	};

	typedef vector<Entry> Counters;

	struct PageEntry {
		PageType type;
		uint64 cur;
		Counters counters;
		PageEntry(PageType typeArg) : type(typeArg), cur(0), counters() {}
	};

	typedef map<addrint, PageEntry> PageMap;

	PageMap pages;

	typedef multimap<uint64, addrint, less<uint64> > DramMetricMap;
	typedef multimap<uint64, addrint, greater<uint64> > PcmMetricMap;

	DramMetricMap dramMetricMap;
	PcmMetricMap pcmMetricMap;

public:
	OldOfflineMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugStartArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		IAllocator *allocatorArg,
		unsigned numPidsArg,
		unsigned thisPidArg,
		const string& filenameArg,
		const string& metricTypeArg,
		const string& accessTypeArg,
		const string& weightTypeArg,
		uint64 intervalCountArg,
		uint64 metricThresholdArg);

	void monitor(unsigned pid, addrint addr);
	PageType allocate(unsigned pid, addrint addr, bool read, bool instr);
	bool selectPage(unsigned *pid, addrint *addr);
};


class OldMultiQueueMigrationPolicy : public OldBaseMigrationPolicy, public IEventHandler {
protected:
	unsigned numQueues;
	int thresholdQueue;
	uint64 lifetime;
	bool logicalTime;
	uint64 filterThreshold;
	bool secondDemotionEviction;
	bool aging;
	bool useHistory;
	bool usePendingList;

	struct AccessEntry {
		unsigned pid;
		addrint addr;
		uint64 expirationTime;
		uint64 count;
		bool demoted;
		AccessEntry(unsigned pidArg, addrint addrArg, uint64 expirationTimeArg, uint64 countArg, bool demotedArg) : pid(pidArg), addr(addrArg), expirationTime(expirationTimeArg), count(countArg), demoted(demotedArg) {}
	};

	typedef list<AccessEntry> AccessQueue;

	struct PageEntry {
		PageType type;
		int queue;  //-1 means the victim list
					//-2 means this history list
		AccessQueue::iterator accessIt;
		bool firstMonitor; //whether this page has been monitored for the first time
		uint64 lastMonitor;
		PageEntry(PageType typeArg, int queueArg, const AccessQueue::iterator& accessItArg) : type(typeArg), queue(queueArg), accessIt(accessItArg), firstMonitor(false), lastMonitor(0) {}
	};

	//typedef map<addrint, PageEntry> PageMap;
	typedef unordered_map<addrint, PageEntry> PageMap;


	vector<AccessQueue> queues[2];
	AccessQueue victims;
	AccessQueue history;
	vector<uint64> thresholds;

	PageMap *pages;

	typedef std::pair<int, addrint> PidAddrPair;

	list<PidAddrPair> pending;

	bool dramFull; //For use with DRAM_FIRST allocation policy only

	uint64 currentTime;


public:
	OldMultiQueueMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugStartArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		IAllocator *allocatorArg,
		unsigned numPids,
		unsigned numQueuesArg,
		unsigned thresholdQueueArg,
		uint64 lifetimeArg,
		bool logicalTimeArg,
		uint64 filterThresholdArg,
		bool secondDemotionEvictionArg,
		bool agingArg,
		bool useHistoryArg,
		bool usePendingListArg);
	void monitor(unsigned pid, addrint addr);
	PageType allocate(unsigned pid, addrint addr, bool read, bool instr);
	bool selectPage(unsigned *pid, addrint *addr);

	void process(const Event * event);

private:
	void printQueue(ostream& os);
};

//First touch uses clock as the demotion policy
class OldFirstTouchMigrationPolicy : public OldBaseMigrationPolicy {
private:
	struct AccessEntry {
		unsigned pid;
		addrint addr;
		bool ref;
		AccessEntry(unsigned pidArg, addrint addrArg, bool refArg = false) : pid(pidArg), addr(addrArg), ref(refArg) {}
	};

	typedef list<AccessEntry> AccessQueue;

	AccessQueue queue;
	AccessQueue::iterator currentIt;

	struct PageEntry {
		PageType type;
		AccessQueue::iterator accessIt;
		PageEntry(PageType typeArg, AccessQueue::iterator accessItArg) : type(typeArg), accessIt(accessItArg) {}
	};

	typedef map<addrint, PageEntry> PageMap;

	PageMap *pages;

	bool lastPcmAccessValid;
	unsigned lastPcmAccessPid;
	addrint lastPcmAccessAddr;

public:
	OldFirstTouchMigrationPolicy(
			const string& nameArg,
			Engine *engineArg,
			uint64 debugStartArg,
			uint64 dramPagesArg,
			AllocationPolicy allocPolicyArg,
			IAllocator *allocatorArg,
			unsigned numPidsArg);
	PageType allocate(unsigned pid, addrint addr, bool read, bool instr);
	void monitor(unsigned pid, addrint addr);
	bool selectPage(unsigned *pid, addrint *addr);
};


class OldDoubleClockMigrationPolicy : public OldBaseMigrationPolicy {
private:
	struct AccessEntry {
		int pid;
		addrint addr;
		bool ref;
		AccessEntry(int pidArg, addrint addrArg, bool refArg = false) : pid(pidArg), addr(addrArg), ref(refArg) {}
	};

	typedef list<AccessEntry> AccessQueue;

	AccessQueue dramQueue;
	AccessQueue::iterator currentDramIt;

	AccessQueue pcmActiveQueue;
	AccessQueue pcmInactiveQueue;

	enum ListType {
		DRAM_LIST,
		PCM_ACTIVE_LIST,
		PCM_INACTIVE_LIST
	};

	struct PageEntry {
		ListType type;
		AccessQueue::iterator accessIt;
		PageEntry(ListType typeArg, AccessQueue::iterator accessItArg) : type(typeArg), accessIt(accessItArg) {}
	};

	typedef map<addrint, PageEntry> PageMap;

	PageMap *pages;

public:
	OldDoubleClockMigrationPolicy(
			const string& nameArg,
			Engine *engineArg,
			uint64 debugStartArg,
			uint64 dramPagesArg,
			AllocationPolicy allocPolicyArg,
			IAllocator *allocatorArg,
			unsigned numPidsArg);
	PageType allocate(unsigned pid, addrint addr, bool read, bool instr);
	void monitor(unsigned pid, addrint addr);
	bool selectPage(unsigned *pid, addrint *addr);
};


istream& operator>>(istream& lhs, AllocationPolicy& rhs);

ostream& operator<<(ostream& lhs, AllocationPolicy rhs);

#endif /* MIGRATION_H_ */
