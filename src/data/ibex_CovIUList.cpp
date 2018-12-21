//============================================================================
//                                  I B E X
// File        : ibex_CovIUList.cpp
// Author      : Gilles Chabert
// Copyright   : IMT Atlantique (France)
// License     : See the LICENSE file
// Created     : Nov 07, 2018
// Last update : Dec 01, 2018
//============================================================================

#include "ibex_CovIUList.h"

#include <cassert>

using namespace std;

namespace ibex {

CovIUList::CovIUList(const CovIUListFactory& fac) : CovList(fac), nb_inner(0), nb_unknown(0),
		_IU_status(NULL), _IU_inner(NULL), _IU_unknown(NULL) {
	fac.build(*this);
}

CovIUList::CovIUList(const char* filename) : CovIUList(CovIUListFactory(filename)) {

}

void CovIUList::save(const char* filename) {
	stack<unsigned int> format_seq;
	ofstream* of=CovIUListFile::write(filename, *this, format_seq);
	of->close();
	delete of;
}

CovIUList::~CovIUList() {
	if (_IU_status) {
		delete[] _IU_status;
		delete[] _IU_inner;
		delete[] _IU_unknown;
	}
}

ostream& operator<<(ostream& os, const CovIUList& cov) {

	for (size_t i=0; i<cov.nb_inner; i++) {
		os << " inner n°" << (i+1) << " = " << cov.inner(i) << endl;
	}

	for (size_t i=0; i<cov.nb_unknown; i++) {
		os << " unknown n°" << (i+1) << " = " << cov.unknown(i) << endl;
	}

	return os;
}

//----------------------------------------------------------------------------------------------------

CovIUListFactory::CovIUListFactory(size_t n) : CovListFactory(n) {

}

CovIUListFactory::CovIUListFactory(const char* filename) : CovListFactory((size_t) 0 /* tmp*/) {
	stack<unsigned int> format_seq;
	ifstream* f = CovIUListFile::read(filename, *this, format_seq);
	f->close();
	delete f;
}

CovIUListFactory::~CovIUListFactory() {

}

void CovIUListFactory::add_inner(const IntervalVector& x) {
	add(x);
	inner.push_back(nb_boxes()-1);
}

void CovIUListFactory::add_unknown(const IntervalVector& x) {
	add(x);
}

void CovIUListFactory::build(CovIUList& set) const {
	//assert(set.size == boxes.size());
	(size_t&) set.nb_inner = inner.size();
	(size_t&) set.nb_unknown = set.size - set.nb_inner;
	set._IU_status = new CovIUList::BoxStatus[set.size];
	set._IU_inner    = new IntervalVector*[set.nb_inner];
	set._IU_unknown  = new IntervalVector*[set.nb_unknown];

	for (size_t i=0; i<set.size; i++) {
		set._IU_status[i]=CovIUList::UNKNOWN; // by default
	}

	for (vector<unsigned int>::const_iterator it=inner.begin(); it!=inner.end(); ++it) {
		set._IU_status[*it]=CovIUList::INNER;
	}

	size_t jin=0; // count inner boxes
	size_t juk=0; // count unknown boxes

	for (size_t i=0; i<set.size; i++) {
		if (set._IU_status[i]==CovIUList::INNER)
			set._IU_inner[jin++]=(IntervalVector*) &set[i];
		else
			set._IU_unknown[juk++]=(IntervalVector*) &set[i];
	}
	assert(jin==set.nb_inner);
	assert(juk==set.nb_unknown);
}

//----------------------------------------------------------------------------------------------------

const unsigned int CovIUListFile::subformat_level = 2;

const unsigned int CovIUListFile::subformat_number = 0;

ifstream* CovIUListFile::read(const char* filename, CovIUListFactory& factory, stack<unsigned int>& format_seq) {

	ifstream* f = CovListFile::read(filename, factory, format_seq);

	if (format_seq.empty() || format_seq.top()!=subformat_number) return f;
	else format_seq.pop();

	size_t nb_inner = read_pos_int(*f);

	if (nb_inner  > factory.nb_boxes())
		ibex_error("[CovIUListFile]: number of inner boxes exceeds total!");

	if (factory.nb_boxes()==0) return f;

	for (size_t i=0; i<nb_inner; i++) {
		unsigned int j=read_pos_int(*f);
		if (j>=factory.nb_boxes()) ibex_error("[CovIUListFile]: invalid inner box index.");
		factory.inner.push_back(j);
	}

	if (factory.inner.size() != nb_inner)
		ibex_error("[CovIUListFile]: number of inner boxes does not match");

	return f;
}

ofstream* CovIUListFile::write(const char* filename, const CovIUList& cov, stack<unsigned int>& format_seq) {

	format_seq.push(subformat_number);

	ofstream* f = CovListFile::write(filename, cov, format_seq);

	write_int(*f, cov.nb_inner);

	// TODO: a complete scan could be avoided
	for (size_t i=0; i<cov.size; i++) {
		if (cov.status(i)==CovIUList::INNER)
			write_int(*f, (uint32_t) i);
	}
	return f;
}

void CovIUListFile::format(stringstream& ss, const string& title, stack<unsigned int>& format_seq) {
	format_seq.push(subformat_number);

	CovListFile::format(ss, title, format_seq);

	ss
	<< space << " - 1 integer:     the number Ni of inner boxes (<= N)\n"
	<< "|     CovIUList     |"
	            " - Ni integers:   the indices of inner boxes\n"
	<< space << "                  (a subset of CovList boxes).\n"
	<< separator;
}

string CovIUListFile::format() {
	stringstream ss;
	stack<unsigned int> format_seq;
	format(ss, "CovIUList", format_seq);
	return ss.str();
}

} /* namespace ibex */
