/*  Copyright 2008,2009,2010 Edwin Eefting (edwin@datux.nl) 

    This file is part of Synapse.

    Synapse is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Synapse is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Synapse.  If not, see <http://www.gnu.org/licenses/>. */













#ifndef CVAR_H
#define CVAR_H
#define BOOST_VARIANT_USE_RELAXED_GET_BY_DEFAULT 

#include "clog.h"
#include <string>
#include "boost/variant.hpp"
#include <map>
#include "json_spirit.h"

namespace synapse
{
	using namespace std;
	using namespace boost;
	using namespace json_spirit;

	/**
		@author
	*/

	//this has to corospond with the variant order in the Cvar class below:
	#define CVAR_EMPTY 0
	#define CVAR_MAP 1
	#define CVAR_LONG_DOUBLE 2
	#define CVAR_STRING 3
	#define CVAR_LIST 4

	typedef map< string,class Cvar> CvarMap;
	typedef list<class Cvar> CvarList;

	//NOTE: we look at CvarMap as the "default" type, and CvarList as an extra type. so we make the CvarMap as easy accessible as possible. but the consequence is that using a CvarList will take a little more work. (involving a call to .list() and using CvarList::iterator)
	//also look at the #defines below, containing easy to use FOREACH... macros.

	class Cvar {
	public:
		typedef CvarMap::iterator iterator;
		typedef CvarMap::value_type value_type;

		//common stuff
		int which();
		string getPrint(string prefix="");
		Cvar();
		~Cvar();
		void clear();
		bool isEmpty();
		void castError(const char *txt);
		bool operator==( Cvar & other);
		bool operator!=( Cvar & other);

		//CVAR_STRING stuff
		Cvar(const string & value);
//		Cvar(const char * value);
		string & operator=(const string & value);
//		void operator=(const char * value);
		operator string &();
		bool operator==(const string & other);
		bool operator!=(const string & other);
//		bool operator==(const char * other);
//		bool operator!=(const char * other);
		string & str();

		//CVAR_LONG_DOUBLE stuff
		long double & operator=(const long double & value);
		Cvar(const long double & value);
		operator long double & ();
//		bool operator==(long double & other);
//		bool operator!=(long double & other);

		//CVAR_MAP stuff
//		void operator=(CvarMap & value);
		operator CvarMap & ();
        Cvar & operator [](const string & key);
        Cvar & operator [](const char *);
		bool isSet(const char * key);
		bool isSet(const string & key);
		CvarMap::iterator begin();
		CvarMap::iterator end();
		void erase(const char * key);
		CvarMap & map();

		//CVAR_LIST stuff
		//void operator=(CvarList & value);
		operator CvarList & ();
		CvarList & list();

		//json conversion stuff
		void toJson(string & jsonStr);
		void toJsonFormatted(string & jsonStr);
		void fromJson(string & jsonStr);

		static bool selfTest();

	protected:

		//json spirit specific conversion stuff, used internally
		//We might decide to use a safer/faster json parser in the future!
		void toJsonSpirit(Value &spiritValue);
		void fromJsonSpirit(Value &spiritValue);
		void readJsonSpirit(const string & jsonStr, Value & spiritValue);
	
	private:
		variant <void *,CvarMap , long double, string, CvarList> value;
	};
}

//the main type:
typedef synapse::Cvar Cvar;

//easy acces to lists and maps:
typedef synapse::CvarList CvarList;
typedef synapse::CvarMap CvarMap;

//traverse over a Cvar of type map. pair is a reference to the current item: pair.first will be the key (always a string), pair.second will be the value. (always a Cvar)
#define FOREACH_VARMAP(pair, var) BOOST_FOREACH(CvarMap::value_type &pair, var.map())
#define FOREACH_REVERSE_VARMAP(pair, var) BOOST_REVERSE_FOREACH(CvarMap::value_type &pair, var.map())

//traverse over a Cvar of type list. item will be a reference to the current item. (a Cvar)
#define FOREACH_VARLIST(item, var) BOOST_FOREACH(CvarList::value_type &item, var.list())
#define FOREACH_REVERSE_VARLIST(item, var) BOOST_REVERSE_FOREACH(CvarList::value_type &item, var.list())

//traverse over a Cvar of type map. iter will contain a iterator to the current item.
#define FOREACH_VARMAP_ITER(iter, var) for(CvarMap::iterator iter=var.map().begin(); iter!=var.map().end(); iter++)
#define FOREACH_REVERSE_VARMAP_ITER(iter, var) for(CvarMap::iterator iter=var.map().rbegin(); iter!=var.map().rend(); iter++)

//traverse over a Cvar of type list. iter will contain a iterator to the current item.
#define FOREACH_VARLIST_ITER(iter, var) for(CvarList::iterator iter=var.list().begin(); iter!=var.list().end(); iter++)
#define FOREACH_REVERSE_VARLIST_ITER(iter, var) for(CvarList::iterator iter=var.list().rbegin(); iter!=var.list().rend(); iter++)

#endif
