// libcurl_drv.cpp : Defines the exported functions for the DLL application.

//use this or add in "Project Properties (Alt+Enter) > Configuration Properties > Linker > Input > Additional Dependancies"
//#pragma comment(lib, "Ws2_32.lib")
//#pragma comment(lib, "rpcrt4.lib")

//#pragma comment(lib, "lib/libcurl-d.lib")
#pragma comment(lib, "lib/libeay32.lib")
#pragma comment(lib, "lib/ssleay32.lib")

/**
	for errors: expected an identifier | ws2def.h
	https://stackoverflow.com/questions/22517036/socket-errors-cant-get-functions-in-winsock2-h
	https://stackoverflow.com/a/22517174

*/
#include <WinSock2.h>
#include <WS2tcpip.h>
//#include <Windows.h>
#include <windows.h>

#include <fstream>   // std::fstream
#include <iostream>
#include <string>
#include <list>

#include <curl/curl.h>
#include "libcurl_drv.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/error/en.h"

using namespace std;
using namespace rapidjson;

namespace fs = std::experimental::filesystem;

static bool erl_drv_debug = true;


/** 
	Configure to print ASCII or HEX in verbose mode
	https://curl.haxx.se/libcurl/c/debug.html
*/
struct debug_config {
	debug_config() {
		trace_ascii = 0;
	}
	char trace_ascii; /* 1 or 0 */
} config;

enum curl_switch {
	//curl parameters
	eUnknown,
	eRequest,		//--request
	eUrl,			//url
	eHeader,		//-H
	eData,			//--data
	eVerbose,
	eClientCert,	//-E, --cert
	eOutFile,		//-o, --output
	ePinnedPubKey,	//--pinnedpubkey 
	eInsecure,		//-k, --insecure
	eMaxTime,		//-m, --max-time

	//erl driver parameters
	eErlDebug,
	eErlDriverLog
};

static curl_switch hashit(std::string const& inString) {
	if (inString == "-X") return eRequest;	//GET | POST
	if (inString == "--request") return eRequest;	//GET | POST
	if (inString == "-H") return eHeader;
	if (inString == "--header") return eHeader;
	if (inString == "-d") return eData;
	if (inString == "--data") return eData;
	if (inString == "-v") return eVerbose;
	if (inString == "--verbose") return eVerbose;
	if (inString == "-E") return eClientCert;
	if (inString == "--cert") return eClientCert;
	if (inString == "-o") return eOutFile;
	if (inString == "--output") return eOutFile;
	if (inString == "--pinnedpubkey") return ePinnedPubKey;
	if (inString == "-k") return eInsecure;
	if (inString == "--insecure") return eInsecure;
	if (inString == "-m") return eMaxTime;
	if (inString == "--max-time") return eMaxTime;

	if (inString == "url") return eUrl;
	if (inString == "erl_drv_debug") return eErlDebug;
	if (inString == "erl_drv_log") return eErlDriverLog;

	return eUnknown;
}

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
	std:string data ((char*)contents, size * nmemb); 
	data += '\0';
	//println("ss", "Write Received: ", data);
	cout << "Write Received: " << data << endl;
	((std::string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}


size_t write_binary(void *ptr, size_t size, size_t nmemb, FILE *stream) {
	if (!stream) {
		//println("s", "File stream is invalid");
		cout << "File stream is invalid" << endl;
		return 0;
	} else {
		size_t written = fwrite(ptr, size, nmemb, stream);
		size_t shouldBeWritten = size * nmemb;
		if (written != shouldBeWritten) {
			//println("sssi", "write_binary::should be written: ", shouldBeWritten, ", but actually written: ", written);
			cout << "write_binary::should be written: " << shouldBeWritten << ", but actually written: " << written << endl;
		}
		fflush(stream);
		return written;
	}
}


//https://rapidjson.org/md_doc_sax.html
struct JSON_SAX_Handler : public BaseReaderHandler<UTF8<>, JSON_SAX_Handler> {
	JSON_SAX_Handler() {
		curl_global_init(CURL_GLOBAL_ALL);
		curl = curl_easy_init();
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bodyBuffer);

		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, WriteCallback);
		curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headBuffer);
		
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, &errorBuffer);

		filename = "C://temp//libcurl_" + generate_uuid(16) + ".log";
	}

	~JSON_SAX_Handler() {
		//for (auto i = string_list.begin(); i != string_list.end(); ++i) { cout << "deleting: " << *i << endl; }
	}

	bool Null() { cout << "Null()" << endl; return true; }
	bool Bool(bool b) { cout << "Bool(" << boolalpha << b << ")" << endl; return true; }
	bool Int(int i) { cout << "Int(" << i << ")" << endl; return true; }
	bool Uint(unsigned u) { cout << "Uint(" << u << ")" << endl; return true; }
	bool Int64(int64_t i) { cout << "Int64(" << i << ")" << endl; return true; }
	bool Uint64(uint64_t u) { cout << "Uint64(" << u << ")" << endl; return true; }
	bool Double(double d) { cout << "Double(" << d << ")" << endl; return true; }

	//bool copy == true => means we have to make a copy of the 'str'.
	bool String(const char* str, SizeType length, bool copy) {
		//println("sssisbs", "Org String(", str, ", ", length, ", ", copy, ")");
		cout << "Org String(" << str << ", " << length << ", " << copy << ")" << endl;

		//Reference to the strings has to be kept alive until curl ends its operation. Otherwise, HTTP request get corrupted once reference to strings are lost when this function scope out.
		//Possibly as explained here: https://stackoverflow.com/questions/5947129/how-to-set-char-value-from-std-string-c-str-not-working
		const std::string persistant_str = std::string(str, length);
		string_list.push_back(persistant_str);
		const char* tmp_str = string_list.back().c_str();
		CURLcode res = CURL_LAST;

		std::vector<std::string> words;
		std::string certPath, password, tempStr;
		std::string::size_type sz;
		long timeout = 30000L;

		switch (hashit(key_)) {
		case eInsecure:
			//println("s", "turning off security validtion");
			cout << "turning off security validtion" << endl;
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
			return true;

		case eRequest:
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, tmp_str);
			return true;

		case eUrl:
			//println("ss", "eUrl: ", tmp_str);
			cout << "eUrl: " << tmp_str << endl;;
			curl_easy_setopt(curl, CURLOPT_URL, tmp_str);
			return true;

		case eHeader:
			slist1 = curl_slist_append(slist1, tmp_str);
			return true;

		case eData:
			//println("ss", "eData: ", string_list.back());
			cout << "eData: " << string_list.back() << endl;
			if (postFields.size() == 0) {
				postFields += string_list.back();
			} else {
				postFields += "&" + string_list.back();
			}
			//println("ss", "postFields: ", postFields);
			cout << "postFields: " << postFields << endl;
			return true;

		case eVerbose:
			//debug
			config.trace_ascii = 1; /* enable ascii tracing */
			curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, my_trace);
			curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &config);
			/* the DEBUGFUNCTION has no effect until we enable VERBOSE */
			curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
			return true;

		case eClientCert:
			split(tmp_str, words, ':');
			//println("si", "words.size: ", words.size());
			cout << "words.size: " << words.size() << endl;
			/*
				This would split: "c:/ProgramData/XXX/user.pem"
				to 
				words[0] = "c
				words[1] = /ProgramData/XXX/user.pem"
				words[2] = CE93A057B298C37645F05B3C004B5AF6
			*/
			if (words.size() == 3) { 
				certPath = words[0] + ":" + words[1];
				password = words[2];
			} else {
				certPath = words[0] + ":" + words[1];
			}
			cout <<"1. certPath :" << certPath << ", password: " << password << endl;
			//Crashes the print fun
			//println("ssss", "2. certPath :", certPath, ", password: ", password);
			//certPath = certPath.substr(1, (certPath.length() - 2)); //to remove double qoutoes around the path
			
			res = curl_easy_setopt(curl, CURLOPT_CAINFO, certPath.c_str());
			cout << "CURLOPT_CAINFO result = " << res << endl;
			res = curl_easy_setopt(curl, CURLOPT_SSLCERT, certPath);
			cout << "CURLOPT_SSLCERT result = " << res << endl;
			if (password.size() > 0) {
				res = curl_easy_setopt(curl, CURLOPT_KEYPASSWD, password);
				cout << "CURLOPT_KEYPASSWD result = " << res << endl;
			}
			return true;
		
		case eMaxTime:
			timeout = std::stol(tmp_str, &sz);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout);
			return true;

		case eOutFile:
			cout << "download file: " << tmp_str << endl;
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
			curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L);
			
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.55.1");
			curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L);
			curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
			
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_binary);
			fp = fopen(tmp_str, "wb");
			//curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
			cout << "file opened" << endl;
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
			return true;

		case ePinnedPubKey:
			cout << "1. Pinned PubKey: " << tmp_str << endl;
			tempStr = tmp_str;
			//tempStr = tempStr.substr(1, (tempStr.length() - 2)); //to remove double qoutoes around hash
			cout << "2. Pinned PubKey: " << tempStr << endl;
			curl_easy_setopt(curl, CURLOPT_PINNEDPUBLICKEY, tempStr.c_str());
			return true;

		case eErlDebug:
			if (persistant_str.compare("true") == 0) {
				erl_drv_debug = true;
			} else {
				erl_drv_debug = false;
			}
			return true;

		case eErlDriverLog:
			cout << "curl_info::---- Creating log file ----" << endl;
			filename = tmp_str;
			//println("ss", "Logfile name: ", filename);
			cout << "Logfile name: " << filename << endl;
			libcurl_drv_log.open(filename, std::fstream::app);
			//println("sss", "curl_info::---- Created Log file: ", filename, " ----");
			cout << "curl_info::---- Created Log file: " << filename << " ----" << endl;
			libcurl_drv_log.flush();

			return true;
		default:
			//cout << "curl_info::Unknown data received: " << tmp_str << endl;
			//println("ss", "curl_info::Unknown data received: ", tmp_str);
			cout << "curl_info::Unknown data received: " << tmp_str << endl;
		}
		return false; //parsing failed
	}

	bool StartObject() { 
		//cout << "curl_info::StartObject()" << endl; 
		return true; 
	}
	bool Key(const char* str, SizeType length, bool copy) {
		cout << "curl_info::Key(" << str << ", length: " << length << ", copy: " << boolalpha << copy << ")" << endl;
		//println("sssisbs", "curl_info::Key(", str, ", ", length, ", ", copy, ")");
		key_ = std::string(str);
		return true;
	}
	bool EndObject(SizeType memberCount) { 
		//cout << "curl_info::EndObject(" << memberCount << ")" << endl; 
		return true;
	}
	bool StartArray() { 
		//cout << "curl_info::StartArray()" << endl; 
		return true;
	}
	bool EndArray(SizeType elementCount) {
		cout << "curl_info::EndArray(" << elementCount << ")" << endl;
		//println("sis", "curl_info::EndArray(", elementCount, ")");
		if (slist1 != NULL) {
			//cout << "curl_info::slist1(" << slist1 << ")" << endl;
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist1);
		}
		return true;
	}
	std::string key_;
	std::string bodyBuffer;
	std::string headBuffer;
	std::string postFields;
	char errorBuffer[CURL_ERROR_SIZE];
	std::list<string> string_list;

	std::string filename = "C://temp//libcurl_" + generate_uuid(16) + ".log";
	std::ofstream libcurl_drv_log;

	FILE *fp;

	CURL *curl;
	struct curl_slist *slist1 = NULL;
};


// Driver Start
static ErlDrvData start(ErlDrvPort port, char* cmd) {

	//libcurl_drv_t* retval = (libcurl_drv_t*)driver_alloc(sizeof(libcurl_drv_t)); => 
	//'driver_alloc' is basically a malloc wrapper and using that way will prevent the constructor of the log file object being called.
	//Since we are not calling 'driver_alloc', we cannot call 'driver_free' either. Has to call delete instead.
	//This using new and it will call the constructor of the log file.
	libcurl_drv_t* retval = new libcurl_drv_t();

	//cout << "curl_info::---- Creating log file ----" << endl;
	////std::string filename = "C://temp//libcurl_" + GetNewGUID() + ".log";
	//std::string filename = "C://temp//libcurl_" + generate_uuid(16) + ".log";
	//cout << "Logfile name: " << filename << endl;
	//retval->libcurl_drv_log.open(filename, std::fstream::app);
	//cout << "curl_info::---- Created Log file: " << filename << " ----" << endl;
	//logme(&(retval->libcurl_drv_log), "--- Start erlang_libcurl_drv driver ---\nCommand line", cmd);
	//retval->libcurl_drv_log.flush();

	//Set the state for the driver
	retval->port = port;
	return (ErlDrvData)retval;
}


// Driver Stop
static void stop(ErlDrvData handle) {
	//println("s", "stop() called");
	cout << "stop() called" << endl;
	libcurl_drv_t* driver_data = (libcurl_drv_t*)handle;
	curl_global_cleanup();
	//println("s", "curl_global_cleanup() success");
	cout << "curl_global_cleanup() success" << endl;
	if (driver_data->libcurl_drv_log) {
		//println("s", "closing libcurl_drv_log ");
		cout << "closing libcurl_drv_log" << endl;
		//driver_data->libcurl_drv_log.close();
		(*driver_data->libcurl_drv_log).close();
		//println("s", "closed libcurl_drv_log");
		cout << "closed libcurl_drv_log" << endl;
	}
	//println("s", "freeing driver_data");
	cout << "freeing driver_data" << endl;
	//driver_free(handle); Since we not calling 'driver_alloc', we cannot call 'driver_free' either. Has to call delete instead.
	delete driver_data;
	//println("s", "freed driver_data");
	cout << "freed driver_data" << endl;
}


//static void show_cert_info(JSON_SAX_Handler *handler) {
//	struct curl_certinfo *ci;
//	cout << "show_cert_info :: about to get CERTINFO" << endl;
//	CURLcode res = curl_easy_getinfo(handler->curl, CURLINFO_CERTINFO, &ci);
//	cout << "show_cert_info :: got CERTINFO" << endl;
//	if (!res) {
//		printf("show_cert_info :: %d certs in cert chain\n", ci->num_of_certs);
//
//		for (int i = 0; i < ci->num_of_certs; i++) {
//			struct curl_slist *slist;
//
//			for (slist = ci->certinfo[i]; slist; slist = slist->next)
//				printf("%s\n", slist->data);
//		}
//	} else {
//		cout << "show_cert_info :: null re	s" << endl;
//	}
//}

// Handle input from Erlang VM
static int control(ErlDrvData drv_data, unsigned int command, char *command_content, int command_size, char **rbuf, int rlen) {
	cout << "command_content: " << std::string(command_content,command_size) << endl;
	libcurl_drv_t* driver_data = (libcurl_drv_t*)drv_data;
	char *command_content_new = new char[command_size + 1];
	strncpy(command_content_new, command_content, command_size);
	command_content_new[command_size] = '\0';

	//logme(&(driver_data->libcurl_drv_log), "drv_data", drv_data);
	//logme(&(driver_data->libcurl_drv_log), "command", command);
	//logme(&(driver_data->libcurl_drv_log), "command_content", command_content_new);
	//logme(&(driver_data->libcurl_drv_log), "command_size", command_size);

	int dataset_term_count; ErlDrvTermData *dataset = new ErlDrvTermData[0];
	CURLcode curlResult;
	JSON_SAX_Handler handler;
	Reader reader;
	StringStream ss(command_content_new);
	ParseResult parseResult = reader.Parse(ss, handler);
	if (parseResult) {
	//if (parseResult.Code == kParseErrorNone) {

		std::ofstream* libcurl_drv_log = &handler.libcurl_drv_log;
		logme(libcurl_drv_log, "drv_data", drv_data);
		logme(libcurl_drv_log, "command", command);
		logme(libcurl_drv_log, "command_content", command_content_new);
		logme(libcurl_drv_log, "command_size", command_size);

		logme(libcurl_drv_log, "handler.postFields", handler.postFields);
		logme(libcurl_drv_log, "handler.postFields::length", handler.postFields.length());
		logme(libcurl_drv_log, "handler.postFields::size", handler.postFields.size());
		if (handler.postFields.length() > 0) {
			curl_easy_setopt(handler.curl, CURLOPT_POSTFIELDS, handler.postFields);
			//curl_easy_setopt(handler.curl, CURLOPT_POSTFIELDSIZE, (long)strlen(postthis));
			curl_easy_setopt(handler.curl, CURLOPT_POSTFIELDSIZE, (long)handler.postFields.size());
		}

		cout << "curl_easy_perform: begin" << endl;
		curlResult = curl_easy_perform(handler.curl);
		cout << "curl_easy_perform: end" << endl;
		logme(libcurl_drv_log, "curlResult", curlResult);
		//show_cert_info(&handler);

		if (curlResult != CURLE_OK) {
			logme(libcurl_drv_log, "curl_easy_perform() failed(curl_easy_strerror)", curl_easy_strerror(curlResult));
			logme(libcurl_drv_log, "curl_easy_perform() failed(errorBuffer)", handler.errorBuffer);
			
			std::string err_msg;
			err_msg.append(curl_easy_strerror(curlResult));
			err_msg.append(" - ");
			err_msg.append(handler.errorBuffer, strlen(handler.errorBuffer));

#ifdef NDEBUG
			logme(libcurl_drv_log, "curl_easy_perform() failed(returningError)", err_msg);
			return_error(driver_data, err_msg.c_str(), &dataset, &dataset_term_count);
			driver_output_term(driver_data->port, dataset, dataset_term_count);
#endif
		} else {
			logme(libcurl_drv_log, "====================================== ", "======================================");
			logme(libcurl_drv_log, "HTTP Header", "");
			logme(libcurl_drv_log, "====================================== ", "======================================");
			logme(libcurl_drv_log, "", handler.headBuffer);
			logme(libcurl_drv_log, "====================================== ", "======================================");
			logme(libcurl_drv_log, "HTTP Response", "");
			logme(libcurl_drv_log, "====================================== ", "======================================");
			logme(libcurl_drv_log, "", handler.bodyBuffer);
			logme(libcurl_drv_log, "====================================== ", "======================================");

#ifdef NDEBUG
			dataset_term_count = 21;
			dataset = new ErlDrvTermData[dataset_term_count];
			dataset[0] = ERL_DRV_PORT;	//start of a port
			dataset[1] = driver_mk_port(driver_data->port);
			//start of the proplist
			dataset[2] = ERL_DRV_ATOM;	//Start of an atom
			dataset[3] = driver_mk_atom((char*)"header");
			dataset[4] = ERL_DRV_STRING;
			dataset[5] = (ErlDrvTermData)handler.headBuffer.c_str();
			dataset[6] = strlen(handler.headBuffer.c_str()); //try handler.bodyBuffer.length;
			dataset[7] = ERL_DRV_TUPLE;
			dataset[8] = 2;				//2 tuple elements {header, String}

			dataset[9] = ERL_DRV_ATOM;	//Start of an atom
			dataset[10] = driver_mk_atom((char*)"body");
			dataset[11] = ERL_DRV_STRING;
			dataset[12] = (ErlDrvTermData)handler.bodyBuffer.c_str();
			dataset[13] = strlen(handler.bodyBuffer.c_str()); //try handler.bodyBuffer.length;
			dataset[14] = ERL_DRV_TUPLE;
			dataset[15] = 2;			//2 tuple elements {body, String}

			dataset[16] = ERL_DRV_NIL;
			dataset[17] = ERL_DRV_LIST;	//End of a list
			dataset[18] = 2 + 1;		//2 list elements + 1 (for the ERL_DRV_NIL may be???)

			dataset[19] = ERL_DRV_TUPLE;
			dataset[20] = 2;			//2 tuple elements: {Port, PropList}

			//for (int i = 0; i < term_count; i++) cout << "dataset: " << dataset[i] << endl;
			driver_output_term(driver_data->port, dataset, dataset_term_count);
#endif
		}

		cout << "download file::about to close download file" << endl;
		if (!handler.fp) {
			cout << "download file::closing download file" << endl;
			fclose(handler.fp);
			cout << "download file::closed" << endl;
		}
		else {
			cout << "download file::fp was NULL: " << handler.fp << endl;
		}
		curl_easy_cleanup(handler.curl);
		if (handler.slist1 != NULL) {
			curl_slist_free_all(handler.slist1);
			handler.slist1 = NULL;
		}
		
	} else { //JSON parsing failed
		ParseErrorCode e = reader.GetParseErrorCode();
		size_t o = reader.GetErrorOffset();
		logme(NULL, "curl_erl_driver::error", "Input JSON parse failed");
		logme(NULL, "curl_erl_driver::JSON", command_content_new);
		logme(NULL, "curl_erl_driver::CommandSize", command_size);
		logme(NULL, "curl_erl_driver::Parse Error", GetParseError_En(e));
		logme(NULL, "curl_erl_driver::offset", o);

		std::string err_msg;
		err_msg.append(GetParseError_En(e));
		err_msg.append(" offset(");
		err_msg.append(std::to_string(o));
		err_msg.append(")");

		//https://stackoverflow.com/questions/7352099/stdstring-to-char
		return_error(driver_data, err_msg.c_str(), &dataset, &dataset_term_count);
		driver_output_term(driver_data->port, dataset, dataset_term_count);
	}
	delete dataset;
	delete command_content_new;
	return 0;
}


// Unkown Command
static int unknown(libcurl_drv_t *driver_data, unsigned int command, char *content, int command_size) {
	logme(driver_data->libcurl_drv_log, "curl_info::unknown drv_data", driver_data);
	logme(driver_data->libcurl_drv_log, "curl_info::unknown command", command);
	logme(driver_data->libcurl_drv_log, "curl_info::unknown content", content);
	logme(driver_data->libcurl_drv_log, "curl_info::unknown command_size", command_size);
	return driver_output_term(driver_data->port, NULL, sizeof(NULL) / sizeof(NULL));
}


static int return_error(libcurl_drv_t *drv, const char *error, ErlDrvTermData **spec, int *term_count) {
	//*spec = (ErlDrvTermData *)calloc(7, sizeof(ErlDrvTermData));
	*term_count = 7;
	ErlDrvTermData *dataset = new ErlDrvTermData[*term_count];
	(*spec)[0] = ERL_DRV_ATOM;
	(*spec)[1] = driver_mk_atom((char*)"error");
	(*spec)[2] = ERL_DRV_STRING;
	(*spec)[3] = (ErlDrvTermData)error;
	(*spec)[4] = strlen(error);
	(*spec)[5] = ERL_DRV_TUPLE;
	(*spec)[6] = 2;	
	return 0;
}


//https://curl.haxx.se/libcurl/c/debug.html
static void dump(const char *text, FILE *stream, unsigned char *ptr, size_t size, char nohex) {
	size_t i; size_t c;
	unsigned int width = 0x10;

	if (nohex)
		/* without the hex output, we can fit more on screen */
		width = 0x80;

	fprintf(stream, "%s, %10.10lu bytes (0x%8.8lx)\n",
		text, (unsigned long)size, (unsigned long)size);

	for (i = 0; i < size; i += width) {
		fprintf(stream, "%4.4lx: ", (unsigned long)i);
		if (!nohex) {
			/* hex not disabled, show it */
			for (c = 0; c < width; c++)
				if (i + c < size)
					fprintf(stream, "%02x ", ptr[i + c]);
				else
					fputs("   ", stream);
		}

		for (c = 0; (c < width) && (i + c < size); c++) {
			/* check for 0D0A; if found, skip past and start a new line of output */
			if (nohex && (i + c + 1 < size) && ptr[i + c] == 0x0D &&
				ptr[i + c + 1] == 0x0A) {
				i += (c + 2 - width);
				break;
			}
			fprintf(stream, "%c",
				(ptr[i + c] >= 0x20) && (ptr[i + c] < 0x80) ? ptr[i + c] : '.');
			/* check again for 0D0A, to avoid an extra \n if it's at width */
			if (nohex && (i + c + 2 < size) && ptr[i + c + 1] == 0x0D &&
				ptr[i + c + 2] == 0x0A) {
				i += (c + 3 - width);
				break;
			}
		}
		fputc('\n', stream); /* newline */
	}
	fflush(stream);
}


static int my_trace(CURL *handle, curl_infotype type, char *data, size_t size, void *userp) {
	struct debug_config *config = (struct debug_config *)userp;
	const char *text;
	(void)handle; /* prevent compiler warning */

	switch (type) {
	case CURLINFO_TEXT:
		fprintf(stderr, "== Info: %s", data);
		/* FALLTHROUGH */
	default: /* in case a new one is introduced to shock us */
		return 0;

	case CURLINFO_HEADER_OUT:
		text = "=> Send header";
		break;
	case CURLINFO_DATA_OUT:
		text = "=> Send data";
		break;
	case CURLINFO_SSL_DATA_OUT:
		text = "=> Send SSL data";
		break;
	case CURLINFO_HEADER_IN:
		text = "<= Recv header";
		break;
	case CURLINFO_DATA_IN:
		text = "<= Recv data";
		break;
	case CURLINFO_SSL_DATA_IN:
		text = "<= Recv SSL data";
		break;
	}
	dump(text, stderr, (unsigned char *)data, size, config->trace_ascii);
	return 0;
}

static int test_get() {
	CURL *curl;
	CURLcode res;
	std::string bodyBuffer;
	static const char *capath = "cacert.pem";

	curl = curl_easy_init();
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_URL, "https://example.com");
		/* example.com is redirected, so we tell libcurl to follow redirection */
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bodyBuffer);
		curl_easy_setopt(curl, CURLOPT_CAPATH, capath);

		//https://stackoverflow.com/questions/9786150/save-curl-content-result-into-a-string-in-c/9786295#9786295
		//For multi-threaded
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

		/* Perform the request, res will get the return code */
		res = curl_easy_perform(curl);
		/* Check for errors */
		if (res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n",
				curl_easy_strerror(res));

		/* always cleanup */
		curl_easy_cleanup(curl);

		std::cout << bodyBuffer << std::endl;
	}
	return 0;
}


int main(void) {
	test_get();

	//char* command_content = (char *)"{\"--request\": [\"POST\"], \"url\": [\"http://localhost:666/long_req1\"], \"-H\": [\"Authorization:Bearer d7dde44216da11eaa5000242ac1aaaa\", \"Content-Type: application/json\"],\"--data\": [\"{\\\"modules\\\":{\\\"BCM\\\":[{\\\"error\\\":\\\"B1609-15\\\"},{\\\"error\\\":\\\"B178E-15\\\"},{\\\"error\\\":\\\"B1A90-51\\\"}],\\\"PCM\\\":[{\\\"error\\\":\\\"P1302-00\\\"},{\\\"error\\\":\\\"P0615-00\\\"},{\\\"error\\\":\\\"P0107-00\\\"},{\\\"error\\\":\\\"P2122-00\\\"},{\\\"error\\\":\\\"P2127-00\\\"},{\\\"error\\\":\\\"P0123-00\\\"},{\\\"error\\\":\\\"P0222-00\\\"},{\\\"error\\\":\\\"P0237-00\\\"},{\\\"error\\\":\\\"P0627-00\\\"},{\\\"error\\\":\\\"P0118-00\\\"}]},\\\"meta\\\":{\\\"user_id\\\":\\\"XXX\\\"},\\\"vin\\\":\\\"1C6SRFFTXLNXXXXXX\\\"}\"]}";	
	//char **rbuf = NULL;	
	//libcurl_drv_t* retval = new libcurl_drv_t();
	//std::ofstream libcurl_drv_log;
	//libcurl_drv_log.open("C:/temp/libcurl_drv.log");
	//retval->libcurl_drv_log = &libcurl_drv_log;
	//ErlDrvData drv_data = (ErlDrvData)retval;

	//control(drv_data, 2, command_content, 747, rbuf, 0);
	//stop(drv_data);

	getchar();
	return 0;
}
