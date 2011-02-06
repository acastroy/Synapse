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


/** \file
The play list module.

This module can dynamicly generate playlists from directory's. It also can cache per path metadata.



*/


#include "synapse.h"
#include "cconfig.h"
#include <string.h>
#include <errno.h>
#include <map>
#include <boost/shared_ptr.hpp>

#include "exception/cexception.h"

#include "boost/filesystem.hpp"


namespace pl
{
	using namespace std;
	using namespace boost::filesystem;
	using namespace boost;

	bool shutdown;
	int defaultId=-1;

	SYNAPSE_REGISTER(module_Init)
	{
		Cmsg out;
		shutdown=false;
		defaultId=msg.dst;

		//load config file
		synapse::Cconfig config;
		config.load("etc/synapse/pl.conf");

		out.clear();
		out.event="core_ChangeModule";
		out["maxThreads"]=1;
		out.send();

		out.clear();
		out.event="core_ChangeSession";
		out["maxThreads"]=1;
		out.send();

		out.clear();
		out.event="core_LoadModule";
		out["path"]="modules/http_json.module/libhttp_json.so";
		out.send();


		//tell the rest of the world we are ready for duty
		out.clear();
		out.event="core_Ready";
		out.send();

	}

	SYNAPSE_REGISTER(module_SessionStart)
	{
	}


	typedef shared_ptr<directory_iterator> directory_iterator_ptr;
	typedef shared_ptr<recursive_directory_iterator> recursive_directory_iterator_ptr;


	class Citer
	{
		private:
		path mBasePath;
		path mCurrentPath;
		directory_iterator_ptr mIterDir;
		recursive_directory_iterator_ptr mIterFile;


		string mId;

		//reset iterators to currentpath.
		void reset()
		{
			mIterDir=directory_iterator_ptr(new directory_iterator(mCurrentPath));
			mIterFile=recursive_directory_iterator_ptr(new recursive_directory_iterator(mCurrentPath));
			next();
		}

		public:

		//next file
		void next()
		{
			 recursive_directory_iterator end_itr;
			 if ((*mIterFile)!=end_itr)
				 (*mIterFile)++;

			 //skip stuff until we got what we want
			 while ((*mIterFile)!=end_itr && is_directory(mIterFile->status()))
			 {
				 (*mIterFile)++;
			 }
		}

		void create(string id, string basePath)
		{
			mId=id;
			mBasePath=basePath;
			mCurrentPath=basePath;
			reset();
		}


		void send(int dst)
		{
			Cmsg out;
			out.event="pl_Entry";
			out.dst=dst;
			out["id"]=mId;
			out["basePath"]=mBasePath.directory_string();
			out["currentPath"]=mCurrentPath.directory_string();

			if (*mIterDir!= directory_iterator())
				out["selectedDir"]=(*mIterDir)->path().directory_string();

			if (*mIterFile!= recursive_directory_iterator())
				out["selectedFile"]=(*mIterFile)->path().file_string();

			out.send();
		}

		void destroy()
		{

		}

	};

	class CiterMan
	{
		private:
		typedef map<string,Citer> CiterMap;
		CiterMap mIterMap;

		public:

		Citer & get(string id)
		{
			if (mIterMap.find(id)==mIterMap.end())
				throw(synapse::runtime_error("Playlist not found"));
			return(mIterMap[id]);
		}

		void create(string id, string basePath)
		{
			if (mIterMap.find(id)!=mIterMap.end())
				throw(synapse::runtime_error("Playlist already exists"));

			mIterMap[id].create(id, basePath);
		}

		void destroy(string id)
		{
			get(id).destroy();
			mIterMap.erase(id);
		}


	};

	CiterMan iterMan;


	/** Create a new iterator
		\param id Traverser id
		\path path Base path. Iterator can never 'escape' this directory.

		SECURITY WARNING: Its possible to traverse the whole filesystem for users that have permission to send pl_Create!

	\par Replies pl_Entry:
		\param id Traverser id
		\param path Current path
		\param file Current file, selected according to search criteria

	*/
	SYNAPSE_REGISTER(pl_Create)
	{
		iterMan.create(msg["id"], msg["path"]);
		iterMan.get(msg["id"]).send(msg.src);
	}


	/** Delete specified iterator
		\param id Traverser id
	*/
	SYNAPSE_REGISTER(pl_Destroy)
	{
		iterMan.destroy(msg["id"]);
	}


	/** Change selection/search criteria for files. Initalise a new iterator
		\param id Traverser id
		\param recurse Recurse level (-1 is infinite depth)
		\param fileOrder Order in which to traverse files (date, random, name)
		\param dirOrder  Order in which to directorys files (date, name)
		\param search	Search parameters for metadata or filename (details later!)

	\par Replies pl_Entry:
	 */
	SYNAPSE_REGISTER(pl_Mode)
	{

	}

	/** Get current directory and file
		\param id Traverser id

	\par Replies pl_Entry.
	*/
	SYNAPSE_REGISTER(pl_Current)
	{
		iterMan.get(msg["id"]).send(msg.src);
	}

	/** Select next directory entry in list
		\param id Traverser id

	\par Replies pl_Entry:
	 */
	SYNAPSE_REGISTER(pl_NextDir)
	{

	}


	/** Select previous entry directory in list
		\param id Traverser id

	\par Replies pl_Entry:
	 */
	SYNAPSE_REGISTER(pl_PreviousDir)
	{

	}

	/** Enters selected directory
		\param id Traverser id

	\par Replies pl_Entry:
	 */
	SYNAPSE_REGISTER(pl_EnterDir)
	{

	}

	/** Exits directory, selecting directory on higher up the hierarchy
		\param id Traverser id

	\par Replies pl_Entry:
	 */
	SYNAPSE_REGISTER(pl_ExitDir)
	{

	}

	/** Next song
		\param id Traverser id

	\par Replies pl_Entry:
	 */
	SYNAPSE_REGISTER(pl_Next)
	{
		iterMan.get(msg["id"]).next();
		iterMan.get(msg["id"]).send(msg.src);

	}

	/** Previous song
		\param id Traverser id

	\par Replies pl_Entry:
	 */
	SYNAPSE_REGISTER(pl_Previous)
	{

	}




	SYNAPSE_REGISTER(module_Shutdown)
	{
		shutdown=true;
	}

}
