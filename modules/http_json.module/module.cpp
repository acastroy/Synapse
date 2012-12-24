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

//remove this to enable debugging in this module:
#define NDEBUG

#include "cnetman.h"
#include "synapse.h"
#include <boost/regex.hpp>

#include <fstream>
#include <ostream>

#include <sys/stat.h>

#include <cstdlib>


#include "chttpsessionman.h"

#include <boost/lexical_cast.hpp>

#include "../../csession.h"

#include <queue>

#include <boost/asio/buffer.hpp>

#include <algorithm>

#include <boost/thread/condition.hpp>

#include "cconfig.h"

#include <time.h>



/**

	Sessions are identified with a authCookie. This cookie is NOT a regular browser cookie, its generated by the server for every java-script instance.
	The java script in the browser uses XMLhttprequest POST to post events.
	The browser uses XMLhttprequest GET to receive events.
	If there are no events to receive, the servers waits and the GET request 'hangs', until a event arrives. This is called long polling.

	Also see: http://en.wikipedia.org/wiki/Push_technology Long polling.
	To be implemented: HTML5 HTTP server push, since its more efficient.




*/


//globals
bool stop=false;
int moduleSessionId=0;
int netIdCounter=0;
ChttpSessionMan httpSessionMan;

//global, read from config file
synapse::Cconfig config;


void getHttpDate(string & s)
{
	//(all this crap just to get the right date format :( )
//ARG!! CRASHES:
//		using namespace boost::posix_time;
//		using namespace boost::local_time;
//		stringstream s;
//		ptime now = second_clock::local_time();
//		local_time_facet *output_facet=new local_time_facet();
//		s.imbue(locale(locale::classic(), output_facet));
//        //Mon, 04 Oct 2010 22:36:51 GMT
//		output_facet->format("%a, %d %b %Y %H:%M:%S");
//		s << now;
//		extraHeaders["Cache-Control"]="public, max-age=36000000";
//		extraHeaders["Date"]=s.str();
//		delete output_facet;
	//lets use the linux way for now..
	time_t currTime;
	tm currTm;
	currTime=time(NULL);
	if (gmtime_r(&currTime,&currTm)!=NULL)
	{
		char outstr[200];
		if (strftime(outstr, 200, "%a, %d %b %Y %H:%M:%S GMT", &currTm) != 0)
		{
			s=outstr;
		}

	}
}





// We extent the Cnet class with our own network handlers.
// Every connection will get its own unique Cnet object.
// As soon as something with a network connection 'happens', these handlers will be called.
// We do syncronised writing instead of using the asyncronious doWrite()
class CnetHttp : public synapse::Cnet
{
	/// //////////////// PRIVATE STUFF FOR NETWORK CONNECTIONS

	private:

	//http basically has two states: The request-block and the content-body.
	//The states alter eachother continuesly during a connection.
	//We require different read, depending on the state we're in.
	//We have an additional state for synapse event polling
	enum Tstates {
		REQUEST,
		CONTENT,
		WAIT_LONGPOLL,
	};

	ThttpCookie mAuthCookie;
	Tstates mState;
	string mRequestType;
	string mRequestUrl;
	string mRequestQuery;
	string mRequestVersion;
	Cvar mHeaders;


	void init_server(int id, CacceptorPtr acceptorPtr)
	{
		mState=REQUEST;
		delimiter="\r\n\r\n";
		mAuthCookie=0;
	}

	/** Server connection 'id' is established.
	*/
 	void connected_server(int id, const string &host, int port, int local_port)
 	{
		//fire off new acceptor thread
 		Cmsg out;
		out.dst=moduleSessionId;
		out.event="http_json_Accept";
		out["port"]=local_port;
 		out.send();
 	}


	void startAsyncRead()
	{
		if (mState==REQUEST || mState==WAIT_LONGPOLL)
		{
				DEB(id << " starting async read for REQUEST headers");
				//The request-block ends with a empty newline, so read until a double new-line:
				mHeaders.clear();
				asio::async_read_until(
					tcpSocket,
					readBuffer,
					delimiter,
					bind(&Cnet::readHandler, this, _1, _2));
		}
		else
		if (mState==CONTENT)
		{
				//the buffer might already contain the data, so calculate how much more bytes we need:
				int bytesToTransfer=((int)mHeaders["content-length"]-readBuffer.size());
				if (bytesToTransfer<0)
					bytesToTransfer=0;

				DEB(id << " starting async read for CONTENT, still need to receive " << bytesToTransfer << " of " << mHeaders["content-length"].str() << " bytes.");

				asio::async_read(
					tcpSocket,
					readBuffer,
					asio::transfer_at_least(bytesToTransfer),
					bind(&Cnet::readHandler, this, _1, _2));

		}
	}

	/** Sends syncronious data to the tcpSocket and does error handling
    * NOTE: what happens if sendbuffer is full and client doesnt read data anymore? will Cnet hang forever?
	*/
	bool sendData(const asio::const_buffers_1 & buffers)
	{
		if (write(tcpSocket,buffers) != buffer_size(buffers))
		{
			ERROR(id << " had write while sending data, disconnecting.");
			doDisconnect();
			return(false);
		}
		return(true);
	}

	bool sendData(const asio::mutable_buffers_1 & buffers)
	{
		if (write(tcpSocket,buffers) != buffer_size(buffers))
		{
			ERROR(id << " had write while sending data, disconnecting.");
			doDisconnect();
			return(false);
		}
		return(true);
	}

	void sendHeaders(int status, Cvar & extraHeaders)
	{
		stringstream statusStr;
		statusStr << status;

		string responseStr;
		responseStr+="HTTP/1.1 ";
		responseStr+=statusStr.str()+="\r\n";
		responseStr+="Server: synapse_http_json\r\n";

		//just echo the keep-alive header. response() will disconnect if neccesary
		if (mHeaders.isSet("connection"))
			responseStr+="Connection: "+mHeaders["connection"].str()+"\r\n";

// 		for (Cvar::iterator varI=cookies.begin(); varI!=cookies.end(); varI++)
// 		{
// 			responseStr+="Set-Cookie: "+(string)(varI->first)+"="+(string)(varI->second)+"\r\n";
// 		}

		for (Cvar::iterator varI=extraHeaders.begin(); varI!=extraHeaders.end(); varI++)
		{
			responseStr+=(string)(varI->first)+": "+(string)(varI->second)+"\r\n";
		}

		responseStr+="\r\n";
		DEB(id << " sending HEADERS: \n" << responseStr);

		sendData(asio::buffer(responseStr));

	}

	void respondString(int status, string data)
	{
		Cvar extraHeaders;
		extraHeaders["Content-Length"]=data.length();
		extraHeaders["Content-Type"]="text/html";

		sendHeaders(status, extraHeaders);
		sendData(asio::buffer(data));
	}

	void respondError(int status, string error)
	{
		WARNING(id << " responding with error: " << error);
		respondString(status, error);
	}



	/** Respond by sending a file relative to the wwwdir/
	*/
	bool respondFile(string path)
	{
		Cvar extraHeaders;

		//FIXME: do a better way of checking/securing the path. Inode verification? Or compare with a filelist?
		if (path.find("..")!=string::npos)
		{
			respondError(403, "Path contains illegal characters");
			return(true);
		}

		//default path?
		if (path=="/")
		{
			path=config["indexFile"].str();
		}

		string localPath;
		localPath="wwwdir/"+path;

		struct stat statResults;
		int statError=stat(localPath.c_str(), &statResults);
		if (statError)
		{
			respondError(404, "Error while statting or file not found: " + path);
			return(true);
		}

		if (! (S_ISREG(statResults.st_mode)) )
		{
			respondError(404, "Path " + path + " is not a regular file");
			return(true);
		}

		ifstream inputFile(localPath.c_str(), std::ios::in | std::ios::binary);
		if (!inputFile.good() )
		{
			respondError(404, "Error while opening " + path );
			return(true);
		}

		//determine content-type
		smatch what;
		if (regex_search(
			path,
			what,
			boost::regex("([^.]*$)")
		))
		{
			string extention=what[1];
			if (config["contentTypes"].isSet(extention))
			{
				extraHeaders["content-type"]=config["contentTypes"][extention];
				DEB("Content type of ." << extention << " is " << config["contentTypes"][extention].str());
			}
			else
			{
				WARNING("Cannot determine content-type of ." << extention << " files");
			}
		}

		//determine filesize
		inputFile.seekg (0, ios::end);
		int fileSize=inputFile.tellg();
		inputFile.seekg (0, ios::beg);
		extraHeaders["Content-Length"]=fileSize;

		//enable browser caching with the right headers:
		extraHeaders["Cache-Control"]="public, max-age=290304000";
		getHttpDate(extraHeaders["Date"].str());

		sendHeaders(200, extraHeaders);



		DEB(id << " sending CONTENT of " << path);
		char buf[4096];
		//TODO: is there a better way to do this?
		int sendSize=0;
		while (inputFile.good())
		{
			inputFile.read(buf	,sizeof(buf));
			if (!sendData(asio::buffer(buf, inputFile.gcount())))
			{
				break;
			}
			sendSize+=inputFile.gcount();
		}

		if (sendSize!=fileSize)
		{
			ERROR(id <<  " error during file transfer, disconnecting");
			doDisconnect();
		}

		return true;
	}

	/** Responds with messages that are in the json queue for this clients session
	* If the queue is empty, it doesnt send anything and returns false.
	* if it does respond with SOMETHING, even an error-response, it returns true.
    * Use abort to send an empty resonse without looking at the queue at all.
    * When longpoll is set to false, it always responds with data, even if the queue is empty. In this case if wont affect a waiting longpoll netid.
	*/
	bool respondJsonQueue(bool abort=false, bool longpoll=true)
	{
		string jsonStr;
		if (abort)
		{
			//to abort we need to reply with an empty json array:
			DEB(id << " cancelling longpoll");
			jsonStr="[]";
			httpSessionMan.endGet(id, mAuthCookie);
		}
		else
		{
			if (longpoll)
			{
				ThttpCookie authCookieClone=0;
				try
				{
					authCookieClone=mHeaders["x-synapse-authcookie-clone"];
				}
				catch(...){ };

				//check if there are messages in the queue, based on the authcookie from the current request:
				//This function will change authCookie if neccesary and fill jsonStr!
				httpSessionMan.getJsonQueue(id, mAuthCookie, jsonStr, authCookieClone);

				//authcookie was probably expired, respond with error
				if (!mAuthCookie)
				{
					respondError(400, "Session is expired, or session limit reached.");
					return(true);
				}
			}
			else
			{
				//we DO want to respond with something if its there, but we dont want to do long polling
				httpSessionMan.getJsonQueue(mAuthCookie, jsonStr);
				if (jsonStr=="")
					jsonStr="[]";
			}
		}

		if (jsonStr=="")
		{
			//nothing to reply (yet), return false
			return(false);
		}
		else
		{
			//send headers
			Cvar extraHeaders;
			extraHeaders["Content-Length"]=jsonStr.length();
			extraHeaders["Cache-Control"]="no-cache";
			extraHeaders["Content-Type"]="application/json";
			extraHeaders["X-Synapse-Authcookie"]=mAuthCookie;
			sendHeaders(200, extraHeaders);

			//write the json queue
			sendData(asio::buffer(jsonStr));
			return(true);
		}
	}


	/** This should be only called when the client is ready to receive a response to the requestUrl.
	* Returns true if something is sended.
	* Returns false if there is nothing to send yet. (longpoll mode)
    */
	bool respond(bool abort=false)
	{
		bool sended;

		//someone requested the special longpoll url:
		if (mRequestUrl=="/synapse/longpoll")
		{
			sended=(respondJsonQueue(abort));
		}
		//just respond with a normal file
		else
		{
			sended=(respondFile(mRequestUrl));
		}

		//what to do after the response?
		if (sended)
		{
			if (mHeaders.isSet("connection"))
			{
				if (mHeaders["connection"].str()=="close")
					doDisconnect();
			}
			else if (mRequestVersion=="1.0")
			{
				//http 1.0 should always disconnect if Connection: Keep-Alive is not specified.
				doDisconnect();
			}
		}

		return (sended);
	}

	// Received new data:
	void received(int id, asio::streambuf &readBuffer, std::size_t bytesTransferred)
	{
		string dataStr(boost::asio::buffer_cast<const char*>(readBuffer.data()), readBuffer.size());
		string error;

		//we're expecting a new request:
		if (mState==REQUEST || mState==WAIT_LONGPOLL)
		{
			//if there is still an outstanding longpoll, cancel it:
			if (mState==WAIT_LONGPOLL)
			{
				respond(true);
			}

			//resize data to first delimiter:
			dataStr.resize(dataStr.find(delimiter)+delimiter.length());
			readBuffer.consume(dataStr.length());

			DEB(id << " got http REQUEST: \n" << dataStr);

			//determine the kind of request:
 			smatch what;
 			if (!regex_search(
 				dataStr,
 				what,
 				boost::regex("^(GET|POST) ([^? ]*)([^ ]*) HTTP/(1..)$")
 			))
 			{
				error="Cant parse request.";
 			}
			else
			{
				mRequestType=what[1];
				mRequestUrl=what[2];
				mRequestQuery=what[3];
				mRequestVersion=what[4];
				DEB("REQUEST query: " << mRequestQuery);

				//create a regex iterator for http headers
				mHeaders.clear();
				boost::sregex_iterator headerI(
					dataStr.begin(),
					dataStr.end(),
					boost::regex("^([[:alnum:]-]*): (.*?)$")
				);

				//parse http headers
				while (headerI!=sregex_iterator())
				{
					string header=(*headerI)[1].str();
					string value=(*headerI)[2].str();

					//headers handling is lowercase in synapse!
					transform(header.begin(), header.end(), header.begin(), ::tolower);

					mHeaders[header]=value;
					headerI++;
				}

				//this header MUST be set on longpoll requests:
				//if the client doesnt has one yet, httpSessionMan will assign a new one.
				try
				{
					mAuthCookie=mHeaders["x-synapse-authcookie"];
				}
				catch(...)
				{
					mAuthCookie=0;
				}

				//proceed based on requestType:
				//a GET or empty POST:
				if (mRequestType=="GET" || (int)mHeaders["content-length"]==0)
				{
					if (respond())
					{
						mState=REQUEST;
					}
					else
					{
						DEB(id << " is now waiting for longpoll results");
						mState=WAIT_LONGPOLL;
					}
					return;
				}
				//a POST with content:
				else
				{
					if ( (int)mHeaders["content-length"]<0  || (int)mHeaders["content-length"] > config["maxContent"] )
					{
						error="Invalid Content-Length";
					}
					else
					{
						//change state to read the contents of the POST:
						mState=CONTENT;
						return;
					}
				}
			}
		}
		else
		//we're expecting the contents of a POST request.
		if (mState==CONTENT)
		{
			if (readBuffer.size() < mHeaders["content-length"])
			{
				error="Didn't receive enough content-bytes!";
				DEB(id <<  " ERROR: Expected " << mHeaders["content-length"].str() << " bytes, but only got: " << bytesTransferred);
			}
			else
			{
				dataStr.resize(mHeaders["content-length"]);
				readBuffer.consume(mHeaders["content-length"]);
				DEB(id << " got http CONTENT with length=" << dataStr.size() << ": \n" << dataStr);

				//POST to the special send url?
				if (mRequestUrl=="/synapse/send")
				{
					error=httpSessionMan.sendMessage(mAuthCookie, dataStr);
					if (error=="")
					{
						//DONT:respond with jsondata if we have something in the queue anyways
						//DONT:respondJsonQueue(false,false);
						//we cant do this, because then the order of messages isnt garanteed. so just respond with a boring empty string:
						respondString(200, "");
					}
					else
						respondError(400, error);

					mState=REQUEST;
					return;
				}
				else
				{
					//ignore whatever is posted, and just respond normally:
					DEB(id << " ignored POST content");
					if (respond())
						mState=REQUEST;
					else
						mState=WAIT_LONGPOLL;
					return;
				}
			}
		}

		//ERROR(id << " had error while processing http data: " << error);
		error="Bad request: "+error;
		respondError(400, error);
		doDisconnect();
		return;

	}


	/** Connection 'id' is disconnected, or a connect-attempt has failed.
	* Sends: conn_Disconnected
	*/
 	void disconnected(int id, const boost::system::error_code& ec)
	{
		httpSessionMan.endGet(id, mAuthCookie);
	}




	/// //////////////// PUBLIC INTERFACE FOR NETWORK CONNECTIONS
	/// these are called via io_service posts.
	public:

	/** We receive this when the queue is changed and we are (probably) waiting for a message.
	 * I say PROBABLY, because the client could have changed its mind in the meanwhile.
	 * Also its possible that the queue is already sended as a response to a synapse/send url.
	 */
	void queueChanged()
	{
		//are we really waiting for something?
		if (mState==WAIT_LONGPOLL)
		{
			//try to respond (this will call the httpSessionMan to see if there really is a message for our authCookie)
			if (respondJsonQueue())
			{
				//we responded, change our state back to REQUEST
				mState=REQUEST;
			}
		}
	}

	//for admin/debugging
	void getStatus(Cvar & var)
	{
		Cnet::getStatus(var);
		if (tcpSocket.is_open())
		{
			if (mState==REQUEST)
				var["httpStatus"]="idle";
			else if (mState==CONTENT)
				var["httpStatus"]="receiving";
			else if (mState==WAIT_LONGPOLL)
				var["httpStatus"]="longpoll";

			var["authCookie"]=mAuthCookie;
		}

	}

};


synapse::CnetMan<CnetHttp> net(100);


SYNAPSE_REGISTER(module_Init)
{
	moduleSessionId=msg.dst;

	Cmsg out;

	//load config file
	config.load("etc/synapse/http_json.conf");
//	configMaxContent=config["maxContent"];
//	configMaxConnections=config["maxConnections"];
	net.setMaxConnections(config["maxConnections"]);

	//set content types
//	for (Cvar::iterator I=config["contentTypes"].begin(); I!=config["contentTypes"].end(); I++)
//	{
//		configContentTypes[I->first]=I->second.str();
//	}

	//change module settings.
	//especially broadcastMulti is important for our "all"-handler
	out.clear();
	out.event="core_ChangeModule";
	out["maxThreads"]=config["maxConnections"]+10;
	//TODO: modify this module to use the more efficient broadcastCookie method?
	out["broadcastMulti"]=1;
	out.send();

	//we need multiple threads for network connection handling
	//(this is done with moduleThreads and sending core_ChangeModule events)
	out.clear();
	out.event="core_ChangeSession";
	out["maxThreads"]=config["maxConnections"]+10;
	out.send();

	//register a special handler without specified event
	//this will receive all events that are not handled elsewhere in this module.
	out.clear();
	out.event="core_Register";
	out["handler"]="all";
	out.send();

	//listen on configured ports
	//NOTE: applications may send additional Listen-events for other ports
	for (CvarList::iterator I=config["ports"].list().begin(); I!=config["ports"].list().end(); I++)
	{
		out.clear();
		out.event="http_json_Listen";
		out.dst=moduleSessionId;
		out["port"]=*I;
		out.send();
	}


	//tell the rest of the world we are ready for duty
	out.clear();
	out.dst=0;
	out.event="core_Ready";
	out.send();

}


/** Creates a new server and listens specified port
 */
SYNAPSE_REGISTER(http_json_Listen)
{
	if (msg.dst==moduleSessionId)
	{
		//starts a new thread to accept and handle the incomming connection.
		Cmsg out;


		//fire off first acceptor thread
		out.clear();
		out.dst=moduleSessionId;
		out.event="http_json_Accept";
		out["port"]=msg["port"];
 		out.send();

		//become the listening thread
		net.runListen(msg["port"]);

		//restore max module threads
//		moduleThreads-=(connections+1);
//		out.clear();
//		out.event="core_ChangeModule";
//		out["maxThreads"]=moduleThreads;
//		out.send();
	}
	else
		ERROR("Send to the wrong session id");

}


/** Runs an acceptor thread (only used internally)
 */
SYNAPSE_REGISTER(http_json_Accept)
{
	if (msg.dst==moduleSessionId)
	{
		while (1)
		{
			if  (net.runAccept(msg["port"], 0) || stop)
				break;

			//somehow failed, so accept again
			DEB("Acceptor for port " << msg["port"] << " failed, sleeping and trying again.");
			sleep(1);
		}
	}
}

/** Stop listening on a port
 */
SYNAPSE_REGISTER(http_json_Close)
{
	if (dst!=moduleSessionId)
		return;

	net.doClose(msg["port"]);
}


/** Called when synapse whats the module to shutdown completely
 * This makes sure that all ports and network connections are closed, so there wont be any 'hanging' threads left.
 * If you care about data-ordering, send this to session-id that sended you the net_Connected.
 */
SYNAPSE_REGISTER(module_Shutdown)
{
	if (dst!=moduleSessionId)
		return;

	//let the net module shut down to fix the rest
	stop=true;
	net.doShutdown();
}

/** Enqueues the message for the appropriate httpSession, and informs any waiting Cnet network connections.
*/
void enqueueMessage(Cmsg & msg, int dst)
{
	//ignore these sessions
	if (dst==moduleSessionId)
		return;

	//pass the message to the http session manager, he knows what to do with it!
	int waitingNetId=httpSessionMan.enqueueMessage(msg,dst);
	if (waitingNetId)
	{
		//a client is waiting for a message, lets inform him:
		lock_guard<mutex> lock(net.threadMutex);

		synapse::CnetMan<CnetHttp>::CnetMap::iterator netI=net.nets.find(waitingNetId);

		//client connection still exists?
		if (netI != net.nets.end())
		{
			DEB("Informing net " << waitingNetId << " of queue change.");
			netI->second->ioService.post(bind(&CnetHttp::queueChanged,netI->second));
		}
		else
		{
			DEB("Want to inform net " << waitingNetId << " of queue change, but it doesnt exist anymore?");
		}
	}
}


SYNAPSE_REGISTER(module_SessionStart)
{
	if (dst==moduleSessionId)
		return;

	httpSessionMan.sessionStart(msg);
	enqueueMessage(msg,dst);
}

SYNAPSE_REGISTER(module_NewSession_Error)
{
	if (dst==moduleSessionId)
		return;

	httpSessionMan.newSessionError(msg);
	//we cant: enqueueMessage(msg,dst);

}

SYNAPSE_REGISTER(module_SessionEnd)
{
	if (dst==moduleSessionId)
		return;

	httpSessionMan.sessionEnd(msg);
	//we cant: enqueueMessage(msg,dst);
}

SYNAPSE_REGISTER(http_json_GetStatus)
{
	if (dst!=moduleSessionId)
		return;

	Cmsg out;
	out.dst=msg.src;
	out.event="http_json_Status";
	net.getStatus(out);
	httpSessionMan.getStatus(out);
	out.send();
}


/** This handler is called for all events that:
 * -dont have a specific handler,
 * -are send to broadcast or to a session we manage.
 */
SYNAPSE_HANDLER(all)
{
	enqueueMessage(msg, dst);
}



