#pragma once
#pragma comment(lib, "ws2_32.lib")

//#include <stdio.h>
//#include <string.h>
#include <fstream>   // std::fstream
#include <sstream>
#include <random>
#include <string>
#include <iostream>

#include <erl_driver.h>
#include <erl_interface.h>
#include <ei.h>

#include <curl/curl.h>

using namespace std;

// Binary commands between Erlang VM and Driver
#define CMD_HTTP_GET 1
#define CMD_HTTP_POST 2
#define CMD_HTTP_PUT 3
#define CMD_HTTP_PATCH 4
#define CMD_HTTP_DELETE 5

// Define struct to hold state across calls
/* 
	https://stackoverflow.com/questions/31269728/string-in-struct
	https://stackoverflow.com/a/31269784: Don't use malloc: the constructor for std::string will not be called and so the object created will be in an undefined state.
										  Use new instead. The C++ runtime will then call the default constructor for the std::string member. Don't forget to match the new with a delete.
	https://stackoverflow.com/questions/5914422/proper-way-to-initialize-c-structs
*/
typedef struct libcurl_drv_t {
	ErlDrvPort port;
	//std::ofstream libcurl_drv_log;
	std::ofstream* libcurl_drv_log = NULL;
} libcurl_drv_t;


static ErlDrvData start(ErlDrvPort port, char* cmd);
static void output(ErlDrvData handle, char *buff, int len);
static void stop(ErlDrvData handle);
static int control(ErlDrvData drv_data, unsigned int command, char *buf, int len, char **rbuf, int rlen);
static void ready_async(ErlDrvData drv_data, ErlDrvThreadData thread_data);
static int unknown(libcurl_drv_t *drv, unsigned int command, char *buf, int len);
static int return_error(libcurl_drv_t *drv, const char *error, ErlDrvTermData **spec, int *terms_count);
static int my_trace(CURL *handle, curl_infotype type, char *data, size_t size, void *userp);


template<typename T>
inline void logme(std::ofstream* log, std::string param, T const value) {
	if(erl_drv_debug && (*log)) (*log) << "\ncurl_info::" << param << ": " << value << endl;
	else if (erl_drv_debug) cout << "\ncurl_info::" << param << ": " << value << endl;
}


//template <class T>
//void split(const std::string& str, T& container) {
//	std::istringstream iss(str);
//	std::copy(std::istream_iterator<std::string>(iss),
//		std::istream_iterator<std::string>(),
//		std::back_inserter(container));
//}

template <class Container>
void split(const std::string& str, Container& cont, char delim) {
	cout << "String: " << str << endl << "Container: " << cont.size() << endl;
	std::stringstream ss(str);
	//std::stringstream ss(str.c_str());
	cout << "1 -------------" << endl;
	std::string token;
	while (std::getline(ss, token, delim)) {
		cout << "token = " << token << endl;
		cont.push_back(token);
	}
}

//https://lowrey.me/guid-generation-in-c-11/
unsigned int random_char() {
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dis(0, 255);
	return dis(gen);
}

std::string generate_uuid(const unsigned int len) {
	std::stringstream ss;
	for (auto i = 0; i < len; i++) {
		const auto rc = random_char();
		std::stringstream hexstream;
		hexstream << std::hex << rc;
		auto hex = hexstream.str();
		ss << (hex.length() < 2 ? '0' + hex : hex);
	}
	return ss.str();
}

inline const char * const boolToString(bool b) {
	return b ? "true" : "false";
}



////https://stackoverflow.com/questions/1657883/variable-number-of-arguments-in-c
//inline void println2(int n_args, ...) {
//	//va_list ap;
//	//va_start(ap, n_args);
//	//for (int i = 0; i <= n_args; i++) {
//	//	std::string a = va_arg(ap, std::string);
//	//	cout << a;
//	//}
//	//va_end(ap);
//	//cout << endl;
//	//----------
//
//	va_list arguments;
//	va_start(arguments, n_args);	// Initializing arguments to store all values after num
//	for (int x = 0; x < n_args; x++)// Loop until all numbers are added
//		cout << va_arg(arguments, string);
//	va_end(arguments);
//}



//DOES NOT WORK PERFECTLY!!!
//https://docs.microsoft.com/en-us/cpp/cpp/functions-with-variable-argument-lists-cpp?view=vs-2019
//  ShowVar takes a format string of the form
//   "ifcs", where each character specifies the
//   type of the argument in that position.
//
//	b = bool
//  c = char
//  f = float
//  i = int
//  s = string (char *)
//
//  Following the format specification is a variable
//  list of arguments. Each argument corresponds to
//  a format character in the format string to which
// the szTypes parameter points
inline void println(const char *szTypes, ...) {
	std::exception_ptr p;
	try {
		va_list vl;
		int i;
		//  szTypes is the last argument specified; you must access all others using the variable-argument macros.
		va_start(vl, szTypes);

		cout << "curl_info::";
		// Step through the list.
		for (i = 0; szTypes[i] != '\0'; ++i) {
			union Printable_t {
				int     i;
				float   f;
				char    c;
				char   *s;
				bool    b;
			} Printable;

			
			switch (szTypes[i]) {   // Type to expect.
			case 'i':
				Printable.i = va_arg(vl, int);
				cout << Printable.i;
				break;
			case 'f':
				Printable.f = va_arg(vl, double);
				cout << Printable.f;
				break;
			case 'c':
				Printable.c = va_arg(vl, char);
				cout << Printable.c;
				break;
			case 's':
				Printable.s = va_arg(vl, char *);
				cout << Printable.s;
				break;
			case 'b':
				Printable.b = va_arg(vl, bool);
				cout << (Printable.b ? "true" : "false");
				break;
			default:
				break;
			}
		}
		cout << endl;
		va_end(vl);
	} catch (const std::exception& e) {
		cout << "curl_info::exception caught while logging:\n" << e.what() << endl;
	} catch (...) {
		p = std::current_exception();
		cout << "curl_info::unknown exception caught while logging" << endl;
	}
}


// Callback Array
static ErlDrvEntry basic_driver_entry = {
	NULL,							/* init */
	start,							/* startup (defined below) */
	stop,							/* shutdown (defined below) */
	
	NULL,
	//output,						/* output */
	
	NULL,							/* ready_input */
	NULL,							/* ready_output */
	(char*)"libcurl_drv",			/* the name of the driver */
	NULL,							/* finish */
	NULL,							/* handle */
	control,						/* control */
	NULL,							/* timeout */
	NULL,							/* outputv (defined below) */
	//ready_async,                  /* ready_async - Instead of this, I'm going to use 'output' in the initial version */
	NULL,							/* F_PTR ready_async, only for async drivers */

	NULL,							/* flush */
	NULL,							/* call */
	NULL,							/* event */
	ERL_DRV_EXTENDED_MARKER,		/* ERL_DRV_EXTENDED_MARKER */
	ERL_DRV_EXTENDED_MAJOR_VERSION,	/* ERL_DRV_EXTENDED_MAJOR_VERSION */
	ERL_DRV_EXTENDED_MAJOR_VERSION,	/* ERL_DRV_EXTENDED_MINOR_VERSION */
	ERL_DRV_FLAG_USE_PORT_LOCKING	/* ERL_DRV_FLAGs */
};

/**
with default mangling:
ordinal hint RVA      name
1    0 00001140 ?driver_init@@YAPAUerl_drv_entry@@PAUTWinDynDriverCallbacks@@@Z

With "extern "C" __declspec(dllexport) DRIVER_INIT(basic_driver) {"
ordinal hint RVA      name
1    0 00001140 ?driver_init@@YAPAUerl_drv_entry@@PAUTWinDynDriverCallbacks@@@Z
2    1 00001160 erl_dyndriver_real_driver_init

With "__declspec(dllexport) DRIVER_INIT(basic_driver) {"
ordinal hint RVA      name
1    0 00001140 ?driver_init@@YAPAUerl_drv_entry@@PAUTWinDynDriverCallbacks@@@Z
2    1 00001160 ?erl_dyndriver_real_driver_init@@YAPAUerl_drv_entry@@XZ
*/

extern "C" { //To prevent C++ function name mangling
	DRIVER_INIT(basic_driver) {
		return &basic_driver_entry;
	}
}
