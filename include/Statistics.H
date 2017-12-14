/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef STATISTICS_H_
#define STATISTICS_H_

#include "Error.H"
#include "Types.H"

#include <list>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <functional>
#include <cmath>

//#include <cassert>

using namespace std;

class StatBase;

typedef  list<StatBase*> StatList;
typedef StatList::iterator StatListIter;

class StatBase {
protected:
	/*
	 * Name of the statistic
	 */
	string _name;

	/*
	 * Description of the statistic
	 */
	string _desc;

public:
	StatBase(const string& name, const string& desc) : _name(name), _desc(desc) {}

	/*
	 * Destructor (needed because of virtual functions)
	 */
	virtual ~StatBase() {};

	/*
	 * Returns the name of the statistic
	 */
	virtual const string& getName() const {return _name;}

	/*
	 * Returns the description of the statistic
	 */
	virtual const string& getDesc() const {return _desc;}

	/*
	 * Reset the statistic
	 */
	virtual void reset() {}

	/*
	 *
	 */
	virtual void startInterval() {}

	/*
	 * Generates dependend statistics
	 */
	virtual StatListIter generate(StatListIter it) {return it;}

	/*
	 * Returns the value of the statistic as a string
	 */
	virtual string getValueAsString() const = 0;

	/*
	 * Return the value of the statistic during the last interval as a string
	 */
	virtual string getIntervalValueAsString() const = 0;

	/*
	 * Print the value of the statistic during the last interval to the given output stream
	 */
	virtual void printIntervalValue(ostream& os) const = 0;
};


class StatContainer {
private:
	list<StatBase*> stats;

public:
	void insert(StatBase *stat);
	StatListIter insertAfter(StatBase *stat, StatListIter parent);
	StatListIter erase(StatListIter iter);
	void reset();
	void startInterval();
	void genListStats();
	void print(ostream& os);
	void printNames(ostream& os);
	void printInterval(ostream& os);
};

template<class T> class StatTemplateBase : public StatBase {
public:
	StatTemplateBase(const string& name, const string& desc) : StatBase(name, desc) {}
	string getValueAsString() const {
		ostringstream oss;
		oss.setf(std::ios::fixed);
		oss.precision(2);
		oss << getValue();
		return oss.str();
	}

	string getIntervalValueAsString() const {
		ostringstream oss;
		oss.setf(std::ios::fixed);
		oss.precision(2);
		oss << getIntervalValue();
		return oss.str();
	}

	void printIntervalValue(ostream& os) const {
		os << getIntervalValue();
	}

	virtual T getValue() const = 0;
	virtual T getIntervalValue() const = 0;
};

template<class T> class Stat : public StatTemplateBase<T> {
protected:
	/*
	 * Current value of the statistic
	 */
	T _value;

	/*
	 * Value of statistic the last time it was printed (for printing interval statistics)
	 */
	T _intervalValue;

	/**
	 * Initial value of the statistic (for initializing and resetting)
	 */
	T _initialValue;

	/*
	 * Whether to return the interval value relative to the initial value (true) or relative to the last start of an interval (false)
	 */
	bool absolute;

public:
	Stat(StatContainer *cont, const string& name, const string& desc, const T& initialValue, bool absoluteArg = false) : StatTemplateBase<T>(name, desc), _value(initialValue), _intervalValue(initialValue), _initialValue(initialValue), absolute(absoluteArg) {
		cont->insert(this);
	}

	void reset() {_value = _intervalValue = _initialValue;}
	void startInterval() {_intervalValue = _value;}

	T getValue() const {return _value;}
	T getIntervalValue() const {return absolute ? _value :_value -_intervalValue;}

	//Note: the following overloaded operator do not exhibit the normal behavior of operators
	void operator++() {_value++;}
	void operator++(int) {_value++;}
	void operator+=(const T& rhs) {_value += rhs;}
	void operator--() {_value--;}
	void operator--(int) {_value--;}
	void operator-=(const T& rhs) {_value -= rhs;}
	void operator=(const T& rhs) {_value = rhs;}
	operator T() const {return _value;}
};

//template<class T> class AggregateStat : public StatTemplateBase<T> {
//protected:
//	vector<StatTemplateBase<T> *> stats;
//
//	T _initialValue;
//
//public:
//	AggregateStat(StatContainer *cont, const string& name, const string& desc, const T& initialValue) : StatTemplateBase<T>(name, desc), _initialValue(initialValue) {
//		cont->insert(this);
//	}
//
//	AggregateStat(
//		StatContainer *cont,
//		const string& name,
//		const string& desc,
//		const T& initialValue,
//		StatTemplateBase<T> *first,
//		StatTemplateBase<T> *second,
//		StatTemplateBase<T> *third = 0,
//		StatTemplateBase<T> *forth = 0) :
//			StatTemplateBase<T>(name, desc),
//			_initialValue(initialValue) {
//		stats.emplace_back(first);
//		stats.emplace_back(second);
//		if (third != 0){
//			stats.emplace_back(third);
//		}
//		if (forth != 0){
//			stats.emplace_back(forth);
//		}
//		cont->insert(this);
//	}
//
//	T getValue() const {
//		T value = _initialValue;
//		for (typename vector<StatTemplateBase<T> *>::const_iterator it = stats.begin(); it != stats.end(); ++it){
//			value += (*it)->getValue();
//		}
//		return value;
//	}
//
//	T getIntervalValue() const {
//		T value = _initialValue;
//		for (typename vector<StatTemplateBase<T> *>::const_iterator it = stats.begin(); it != stats.end(); ++it){
//			value += (*it)->getIntervalValue();
//		}
//		return value;
//	}
//
//	void addStat(StatTemplateBase<T> *stat) {
//		stats.emplace_back(stat);
//	}
//
//};

template<class T>
struct Sum {
	T operator()(typename vector<StatTemplateBase<T> *>::const_iterator first, typename vector<StatTemplateBase<T> *>::const_iterator last, function<T(StatTemplateBase<T>*)> fun) const {
		T sum = 0;
		while (first != last){
			sum += fun(*first);
			++first;
		}
		return sum;
	}
};

template<class T>
struct Max {
	T operator()(typename vector<StatTemplateBase<T> *>::const_iterator first, typename vector<StatTemplateBase<T> *>::const_iterator last, function<T(StatTemplateBase<T>*)> fun) const {
		T cm = numeric_limits<T>::min();
		while (first != last){
			cm = max(cm, fun(*first));
			++first;
		}
		return cm;
	}
};

template<class T>
struct Min {
	T operator()(typename vector<StatTemplateBase<T> *>::const_iterator first, typename vector<StatTemplateBase<T> *>::const_iterator last, function<T(StatTemplateBase<T>*)> fun) const {
		T cm = numeric_limits<T>::max();
		while (first != last){
			cm = min(cm, fun(*first));
			++first;
		}
		return cm;
	}
};

template<class T, class S>
struct Avg {
	T operator()(typename vector<StatTemplateBase<S> *>::const_iterator first, typename vector<StatTemplateBase<S> *>::const_iterator last, function<S(StatTemplateBase<S>*)> fun) const {
		S sum = 0;
		unsigned count = 0;
		while (first != last){
			sum += fun(*first);
			count++;
			++first;
		}
		return static_cast<T>(sum)/static_cast<T>(count);
	}
};

template<class T, class S>
struct Stddev {
	T operator()(typename vector<StatTemplateBase<S> *>::const_iterator first, typename vector<StatTemplateBase<S> *>::const_iterator last, function<S(StatTemplateBase<S>*)> fun) const {
		auto first2 = first;
		S sum = 0;
		unsigned count = 0;
		while (first != last){
			sum += fun(*first);
			count++;
			++first;
		}
		T mean = static_cast<T>(sum)/static_cast<T>(count);
		S sum2 = 0;
		while (first2 != last){
			auto v = fun(*first2);
			auto dif = mean < v ? v-mean : mean-v;
			sum2 += dif*dif;
			++first2;
		}
		return sqrt(static_cast<T>(sum2)/static_cast<T>(count));
	}
};

template<class T, class Func, class S = T> class AggregateStat : public StatTemplateBase<T> {
protected:
	vector<StatTemplateBase<S> *> stats;

	Func func;

public:
	AggregateStat(StatContainer *cont, const string& name, const string& desc) : StatTemplateBase<T>(name, desc) {
		cont->insert(this);
	}

	AggregateStat(
		StatContainer *cont,
		const string& name,
		const string& desc,
		StatTemplateBase<S> *first,
		StatTemplateBase<S> *second,
		StatTemplateBase<S> *third = 0,
		StatTemplateBase<S> *forth = 0) :
			StatTemplateBase<S>(name, desc){
		stats.emplace_back(first);
		stats.emplace_back(second);
		if (third != 0){
			stats.emplace_back(third);
		}
		if (forth != 0){
			stats.emplace_back(forth);
		}
		cont->insert(this);
	}

	T getValue() const {
		return func(stats.cbegin(), stats.cend(), mem_fn(&StatTemplateBase<S>::getValue));
	}

	T getIntervalValue() const {
		return func(stats.cbegin(), stats.cend(), mem_fn(&StatTemplateBase<S>::getIntervalValue));
	}

	void addStat(StatTemplateBase<S> *stat) {
		stats.emplace_back(stat);
	}




};




template<class T, class BinaryOperator, class FirstOperatorType = T, class SecondOperatorType = FirstOperatorType> class BinaryStat : public StatTemplateBase<T> {
protected:
	StatTemplateBase<FirstOperatorType> *_first;
	StatTemplateBase<SecondOperatorType> *_second;
	BinaryOperator _function;
public:
	BinaryStat(StatContainer *cont, const string& name, const string& desc, StatTemplateBase<FirstOperatorType> *first, StatTemplateBase<SecondOperatorType> *second) :
		StatTemplateBase<T>(name, desc), _first(first), _second(second){
		cont->insert(this);
	}

	T getValue() const {
		return _function(static_cast<T>(_first->getValue()), static_cast<T>(_second->getValue()));
	}

	T getIntervalValue() const {
		return _function(static_cast<T>(_first->getIntervalValue()), static_cast<T>(_second->getIntervalValue()));
	}

};

template<class T, class R> class CalcStat: public StatTemplateBase<T> {
public:
	typedef T (R::*StatFunPtr)(void);
private:
	R *objPtr;
	StatFunPtr funPtr;
public:

	CalcStat(StatContainer *cont, const string& name, const string& desc, R *objPtrArg, StatFunPtr funPtrArg) :
		StatTemplateBase<T>(name, desc), objPtr(objPtrArg), funPtr(funPtrArg) {

		cont->insert(this);
	}

	T getValue() const {return (objPtr->*funPtr)();}
	T getIntervalValue() const {return (objPtr->*funPtr)();}

};

template<class T> class ListStatBase : public StatTemplateBase<uint64> {
protected:
	StatContainer *cont;
	uint64 numStats;
	vector< StatTemplateBase<T>* > stats;
	ListStatBase(const string& name, const string& desc, StatContainer *contArg, uint64 numStatsArg) : StatTemplateBase<uint64>(name + "_size", desc + " (number of generated statistics)"), cont(contArg), numStats(numStatsArg) {}
public:
	StatTemplateBase<T>* getStat(uint64 index){
		return stats[index];
	}

	const string& getName() const {return _name;}
	const string& getDesc() const {return _desc;}
	uint64 getValue() const {return numStats;}
	uint64 getIntervalValue() const {return numStats;}
};

template<class T> class ListStat : public ListStatBase<T> {
public:
	ListStat(
		StatContainer *contArg,
		uint64 numStatsArg,
		const string& name,
		const string& desc,
		bool absoluteArg = false) :
			ListStatBase<T>(name, desc, contArg, numStatsArg) {

		ListStatBase<T>::cont->insert(this);
		for (uint64 i = 0; i < ListStatBase<T>::numStats; i++){
			stringstream ssName, ssDesc;
			ssName << name << "_" << i;
			ssDesc << desc << " " << i;
			ListStatBase<T>::stats.emplace_back(new Stat<T>(contArg, ssName.str(), ssDesc.str(), 0, absoluteArg));
		}
	}

	Stat<T>& operator[](uint64 index){
		return static_cast<Stat<T>& >(*ListStatBase<T>::stats[index]);
	}
};

template<class T, class BinaryOperator, class OperatorType = T> class BinaryListStat : public ListStatBase<T> {
	ListStatBase<OperatorType> *first;
	ListStatBase<OperatorType> *second;
public:
	BinaryListStat(StatContainer *contArg, const string& name, const string& desc, ListStatBase<OperatorType> *firstArg, ListStatBase<OperatorType> *secondArg) :
		ListStatBase<T>(name, desc, contArg, firstArg->getValue()), first(firstArg), second(secondArg) {
		ListStatBase<T>::cont->insert(this);
		if (first->getValue() != second->getValue()){
			error("Number of generated statistics should be the same");
		}
		for (uint64 i = 0; i < ListStatBase<T>::numStats; i++){
			stringstream ssName, ssDesc;
			ssName << name << "_" << i;
			ssDesc << desc << " " << i;
			ListStatBase<T>::stats.emplace_back(new BinaryStat<T, BinaryOperator, OperatorType>(contArg, ssName.str(), ssDesc.str(), first->getStat(i), second->getStat(i)));
		}
	}

};

#endif /* STATISTICS_H_ */
